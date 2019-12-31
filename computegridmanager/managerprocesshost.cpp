#include "managerprocesshost.h"
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QtConcurrent/qtconcurrentrun.h>
#include <QStandardPaths>
#include "JlCompress.h"

using namespace ComputeGrid;

ManagerProcessHost::ManagerProcessHost(int _keepAliveIntervalMs, QObject * _parent)
	: QObject(_parent),
	mProcess(nullptr),
	mNetServer(nullptr),
	mKeepAliveIntervalMs(_keepAliveIntervalMs)
{
	NetworkingGlobals::registerMetaTypes();

	mKeepAliveTimer = new QTimer(this);
	QObject::connect(mKeepAliveTimer, SIGNAL(timeout()), this, SLOT(keepAliveTimerTimeout()));
}

ManagerProcessHost::~ManagerProcessHost()
{
	stopNetworkServer();
	stopProcess();

	if (mKeepAliveTimer)
		delete mKeepAliveTimer;

	mKeepAliveTimer = nullptr;
}

bool ManagerProcessHost::startNetworkServer(quint16 _port)
{
	bool res = false;
	stopNetworkServer();

	mNetworkMutex.lock();
	
	mNetServer = new NetworkServer(_port);
	QObject::connect(mNetServer, SIGNAL(clientConnected(NetworkClientInfo)), this, SLOT(networkClientConnected(NetworkClientInfo)));
	QObject::connect(mNetServer, SIGNAL(clientDisconnected(NetworkClientInfo)), this, SLOT(networkClientDisconnected(NetworkClientInfo)));
	QObject::connect(mNetServer, SIGNAL(clientError(NetworkClientInfo, QAbstractSocket::SocketError)), this, SLOT(networkClientError(NetworkClientInfo, QAbstractSocket::SocketError)));
	QObject::connect(mNetServer, SIGNAL(packetReceived(NetworkClientInfo, NetworkPacket)), this, SLOT(networkPacketReceived(NetworkClientInfo, NetworkPacket)));
	QObject::connect(mNetServer, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(networkError(QAbstractSocket::SocketError)));
	
	if (res = mNetServer->startServer())
		mKeepAliveTimer->start(mKeepAliveIntervalMs);
	
	mNetworkMutex.unlock();

	return res;
}

bool ManagerProcessHost::stopNetworkServer()
{
	bool res = false;

	mNetworkMutex.lock();

	if (mNetServer)
	{
		if (mKeepAliveTimer->isActive())
			mKeepAliveTimer->stop();

		if(mNetServer->isListening())
			mNetServer->stopServer();

		delete mNetServer;
		mNetServer = nullptr;
		res = true;
	}

	mNetworkMutex.unlock();

	return res;
}

bool ManagerProcessHost::sendPacket(NetworkPacket & _np, NetworkClientInfo & _nci)
{
	bool res = false;

	mNetworkMutex.lock();

	if (mNetServer)
		res = mNetServer->sendPacket(_np, _nci);

	mNetworkMutex.unlock();

	return res;
}

bool ManagerProcessHost::isNetworkListening()
{
	bool res = false;

	mNetworkMutex.lock();

	res = mNetServer && mNetServer->isListening();

	mNetworkMutex.unlock();

	return res;
}

QList<NetworkClientInfo> ManagerProcessHost::networkClients()
{
	QList<NetworkClientInfo> clients;

	mNetworkMutex.lock();

	if(mNetServer)
		clients = mNetServer->clients();

	mNetworkMutex.unlock();

	return clients;
}

QString ManagerProcessHost::lastNetworkError()
{
	QString res;

	mNetworkMutex.lock();

	if (mNetServer)
		res = mNetServer->lastError();

	mNetworkMutex.unlock();

	return res;
}

bool ManagerProcessHost::startProcess(quint16 _port)
{
	bool res = false;

	stopProcess();

	mProcessMutex.lock();

	mProcess = new QProcess();
	mProcess->setReadChannel(QProcess::StandardOutput);
	QObject::connect(mProcess, SIGNAL(started()), this, SLOT(processStarted()));
	QObject::connect(mProcess, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(processFinished(int, QProcess::ExitStatus)));
	mProcess->setWorkingDirectory(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/manager/");
	mProcess->start(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/manager/manager.exe", QStringList());
	res = mProcess->waitForStarted();

	mProcessMutex.unlock();

	res = startNetworkServer(_port);

	if (res)
		mProcessReadFuture = QtConcurrent::run(this, &ManagerProcessHost::readProcessAsync);
	else
		stopProcess();

	return res;
}

bool ManagerProcessHost::stopProcess()
{
	bool res = false;

	stopNetworkServer();

	if (mProcessReadFuture.isRunning())
		mProcessReadFuture.cancel();

	mProcessMutex.lock();

	if (mProcess)
	{
		try
		{
			mProcess->kill();
		}
		catch (...)
		{
		}

		delete mProcess;
		mProcess = nullptr;
		res = true;
	}

	mProcessMutex.unlock();
	
	return res;
}

void ManagerProcessHost::writeToProcess(QString _cmd)
{
	mProcessMutex.lock();

	if(mProcess)
		mProcess->write((_cmd.simplified() + ComputeGridGlobals::ProcessCommandSuffix).toLocal8Bit());

	mProcessMutex.unlock();
}

bool ManagerProcessHost::loadProcessArchive(QString _archiveFile, bool _isManagerProcess)
{
	QString msg;
	bool res = false;

	QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + (_isManagerProcess ? "/manager" : "/worker"));
	if (
		(dir.exists() && !dir.removeRecursively())
		|| (!dir.exists() && !dir.mkpath(dir.absolutePath())))
		msg = QString("File system I/O error! Directory:'%1' couldn't modify.").arg(dir.absolutePath());
	else if (!QFile::exists(_archiveFile))
		msg = QString("File system I/O error! Archive:'%1' couldn't find.").arg(_archiveFile);
	else
	{
		QStringList files = JlCompress::extractDir(_archiveFile, dir.absolutePath());
		if (files.isEmpty() || !files.contains(dir.absolutePath() + (_isManagerProcess ? "/manager.exe" : "/worker.exe")))
			msg = QString("Archive error! '%1' is invalid, doesn't contain executable: %2").arg(_archiveFile).arg(_isManagerProcess ? "manager.exe" : "worker.exe");
		else
		{
			QStringList args;
			args.append("-test");
			QProcess p;
			p.start(dir.absolutePath() + (_isManagerProcess ? "/manager.exe" : "/worker.exe"), args);
			
			if (!p.waitForFinished(10000))
			{
				p.kill();
				msg = QString("Executable is timed out.");
			}
			else if (p.exitCode() < 0)
				msg = QString("Executable exited with code: %1.").arg(p.exitCode());

			res = msg.isEmpty();

			QString archiveAppData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + (_isManagerProcess ? "/manager.zip" : "/worker.zip");
			
			if (QFile::exists(archiveAppData))
				QFile::remove(archiveAppData);

			if (QFile::copy(_archiveFile, archiveAppData))
			{
				msg = QString("%1 process has been successfully set.").arg(_isManagerProcess ? "Manager" : "Worker");
				res = true;

				if (!_isManagerProcess)
				{
					if (!(res = attachWorkerArchive()))
						msg.clear();
				}
			}
			else
			{
				res = false;
				msg = QString("File system I/O error! Archive:'%1' couldn't copy to path:%2.").arg(_archiveFile).arg(archiveAppData);
			}
		}
	}

	if(!msg.isEmpty())
		emit log(msg, res ? LT_INFO : LT_ERROR);
	
	return res;
}

bool ManagerProcessHost::attachWorkerArchive()
{
	mWorkerProcessData.clear();

	QFile f(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/Worker.zip");
	if (f.open(QIODevice::ReadOnly))
	{
		mWorkerProcessData = f.readAll();
		f.close();
		return true;
	}
	else
		emit log(QString("File system I/O error! Archive:'%1' couldn't read.").arg(f.fileName()), LT_ERROR);

	return false;
}

void ManagerProcessHost::readProcessAsync()
{
	bool run = true;
	bool canRead = false;
	while (run)
	{
		canRead = false;
		while (run && !canRead)
		{
			mProcessMutex.lock();
			if (run = (mProcess && !mProcessReadFuture.isCanceled()))
				canRead = mProcess->canReadLine();
			mProcessMutex.unlock();
		}

		if (run)
		{
			QString cmd;

			mProcessMutex.lock();
			if (mProcess)
				cmd = mProcess->readLine();
			mProcessMutex.unlock();

			if (!cmd.isEmpty())
				QMetaObject::invokeMethod(this, "handleProcessCommand", Q_ARG(QString, cmd));
		}
	}
}

void ManagerProcessHost::handleProcessCommand(QString _command)
{
	ProcessCommand pc;
	QStringList args;

	if (ComputeGridGlobals::parseProcessCommand(_command, pc, args))
	{
		switch (pc)
		{
		case ComputeGrid::PC_WORKER_DATA:
		case ComputeGrid::PC_WORKER_EXIT:
		{
			NetworkPacket np(NPT_DATA);
			np.setTypeId(pc == PC_WORKER_DATA ? DPT_WORKER_DATA : DPT_WORKER_EXIT);
			QDataStream ds(np.dataPtr(), QIODevice::WriteOnly);
			ds << args;

			NetworkClientInfo nci;
			if (findWorkerClient(args.first(), &nci))
			{
				if (!sendPacket(np, nci))
					emit log(QString("Network error: %1").arg(lastNetworkError()), LT_ERROR);
			}
			else
				emit log(QString("Network client of worker %1 couldn't find.").arg(args.first()), LT_ERROR);
		}
		break;

		case ComputeGrid::PC_LOG:
			emit log(args[2], (LogType)(args[1].toUInt()), (LogSource)(args[0].toUInt()));
			break;

		case ComputeGrid::PC_STATUS_MESSAGE:
			emit statusMessage(args[0]);
			break;

		default:
			emit log("Unknown process command: " + _command, LT_WARNING);
			break;
		}
	}
}

void ManagerProcessHost::keepAliveClients()
{
	NetworkPacket np(NPT_DATA);
	np.setTypeId(DPT_HEARTHBEAT);
	np.setData(QByteArray::number(QDateTime::currentDateTime().toMSecsSinceEpoch()));

	QList<NetworkClientInfo> & clients = networkClients();
	for (QList<NetworkClientInfo>::iterator it = clients.begin(); it != clients.end(); ++it)
		sendPacket(np, *it);
}

bool ManagerProcessHost::findWorkerClient(const QString & _worker, NetworkClientInfo * _nci)
{
	QList<NetworkClientInfo> & clients = networkClients();
	for (QList<NetworkClientInfo>::iterator it = clients.begin(); it != clients.end(); ++it)
	{
		if ((*it).toString() == _worker)
		{
			if (_nci)
			{
				_nci->setAddress(it->getAddress());
				_nci->setPort(it->getPort());
			}
			return true;
		}
	}

	return false;
}

#pragma region Slots
void ManagerProcessHost::processStarted()
{
	emit log("Process started.");
}

void ManagerProcessHost::processFinished(int _exitCode, QProcess::ExitStatus _exitStatus)
{
	emit log(
		QString("Process finished. Exit-Code:%1 (%2)").arg(_exitCode).arg(_exitStatus == QProcess::NormalExit ? "Normal Exit" : "Crash Exit"),
		(_exitStatus == QProcess::NormalExit && _exitCode == 0) ? LT_INFO : LT_ERROR
	);

	if (isNetworkListening())
	{
		NetworkPacket np(NPT_DATA);
		np.setTypeId(DPT_WORKER_EXIT);

		QList<NetworkClientInfo> & clients = networkClients();
		for (QList<NetworkClientInfo>::iterator it = clients.begin(); it != clients.end(); ++it)
		{
			np.dataPtr()->clear();

			QDataStream ds(np.dataPtr(), QIODevice::WriteOnly);
			ds << (QStringList() << (*it).toString());
			sendPacket(np, *it);
		}
	}
}

void ManagerProcessHost::networkClientConnected(NetworkClientInfo _clientInfo)
{
	emit log(QString("Grid-Worker: %1 is connected.").arg(_clientInfo.toString()));

	NetworkPacket np(NPT_DATA);
	np.setTypeId(DPT_GRID_ATTACH);
	np.setData(mWorkerProcessData);
	sendPacket(np, _clientInfo);
}

void ManagerProcessHost::networkClientDisconnected(NetworkClientInfo _clientInfo)
{
	emit log(QString("Grid-Worker: %1 is disconnected.").arg(_clientInfo.toString()), LT_WARNING);

	writeToProcess(ComputeGridGlobals::makeProcessCommand(PC_GRID_WORKER_OUT, _clientInfo.toString()));
	emit workerOutGrid(_clientInfo.toString());
}

void ManagerProcessHost::networkClientError(NetworkClientInfo _clientInfo, QAbstractSocket::SocketError _socketError)
{
	emit log(QString("Grid-Worker: %1 threw network error: %2").arg(_clientInfo.toString()).arg((_socketError >= 0 && _socketError < LiteralSocketError.count()) ? LiteralSocketError[_socketError] : "Unknown Network Error"), LT_ERROR);

	// to do: consider reinit the client?
}

void ManagerProcessHost::networkPacketReceived(NetworkClientInfo _clientInfo, NetworkPacket _packet)
{
	DataPacketType dpt = (DataPacketType)_packet.typeId();

	QStringList args;
	QDataStream ds(&_packet.data(), QIODevice::ReadOnly);
	ds >> args;

	switch (dpt)
	{
	case ComputeGrid::DPT_GRID_WORKER_READY:
		args.insert(args.begin(), _clientInfo.toString());
		writeToProcess(ComputeGridGlobals::makeProcessCommand(PC_GRID_WORKER_IN, args));
		emit workerInGrid(_clientInfo.toString(), args.count() == 2 ? args[1].toInt() : 0);
		break;

	case ComputeGrid::DPT_WORKER_DATA:
		args.insert(args.begin(), _clientInfo.toString());
		writeToProcess(ComputeGridGlobals::makeProcessCommand(PC_WORKER_DATA, args));
		break;

	case ComputeGrid::DPT_WORKER_EXIT:
		args.insert(args.begin(), _clientInfo.toString());
		writeToProcess(ComputeGridGlobals::makeProcessCommand(PC_WORKER_EXIT, args));
		break;

	case ComputeGrid::DPT_LOG:
		emit log(QString("(%1)%2").arg(_clientInfo.toString()).arg(args[2]), (LogType)(args[1].toUInt()), (LogSource)(args[0].toUInt()));
		break;

	default:
		emit log(QString("Unknown network packet from Grid-Worker: %1").arg(_clientInfo.toString()), LT_WARNING);
		break;
	}
}

void ManagerProcessHost::networkError(QAbstractSocket::SocketError _socketError)
{
	emit log(QString("Socket error: %1").arg((_socketError >= 0 && _socketError < LiteralSocketError.count()) ? LiteralSocketError[_socketError] : "Unknown Network Error"), LT_ERROR);

	// to do: consider restart?
}
void ManagerProcessHost::keepAliveTimerTimeout()
{
	QMetaObject::invokeMethod(this, "keepAliveClients");
}
#pragma endregion
