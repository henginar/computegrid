#include "workerprocesshost.h"
#include <QDir>
#include <QFile>
#include <QThread>
#include <QtConcurrent/qtconcurrentrun.h>
#include <QStandardPaths>
#include "JlCompress.h"

using namespace ComputeGrid;

WorkerProcessHost::WorkerProcessHost(int _keepAliveIntervalMs, QObject * _parent)
	: QObject(_parent),
	mProcess(nullptr),
	mNetClient(nullptr),
	mKeepAliveIntervalMs(_keepAliveIntervalMs)
{
	NetworkingGlobals::registerMetaTypes();

	mKeepAliveTimer = new QTimer(this);
	QObject::connect(mKeepAliveTimer, SIGNAL(timeout()), this, SLOT(keepAliveTimerTimeout()));
}

WorkerProcessHost::~WorkerProcessHost()
{
	stopProcess();
	disconnectFromNetworkServer();
}

bool WorkerProcessHost::connectToNetworkServer(QString _ip, quint16 _port, uint _timeOut)
{
	bool res = false;

	disconnectFromNetworkServer();

	mNetworkMutex.lock();

	mNetClient = new NetworkClient(_ip, _port);
	QObject::connect(mNetClient, SIGNAL(connected()), this, SLOT(networkConnected()));
	QObject::connect(mNetClient, SIGNAL(disconnected()), this, SLOT(networkDisconnected()));
	QObject::connect(mNetClient, SIGNAL(packetReceived(NetworkPacket)), this, SLOT(networkPacketReceived(NetworkPacket)));
	QObject::connect(mNetClient, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(networkError(QAbstractSocket::SocketError)));
	
	res = mNetClient->connectToServer(_timeOut);

	mNetworkMutex.unlock();

	return res;
}

bool WorkerProcessHost::disconnectFromNetworkServer()
{
	bool res = false;

	mNetworkMutex.lock();

	if (mNetClient)
	{
		if (mNetClient->state() != QAbstractSocket::UnconnectedState)
			res = mNetClient->disconnectFromServer();

		delete mNetClient;
		mNetClient = nullptr;
	}

	mNetworkMutex.unlock();

	return res;
}

bool WorkerProcessHost::startProcess()
{
	bool res = false;

	stopProcess();

	mProcessMutex.lock();
	mProcess = new QProcess();
	QObject::connect(mProcess, SIGNAL(started()), this, SLOT(processStarted()));
	QObject::connect(mProcess, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(processFinished(int, QProcess::ExitStatus)));
	mProcess->setWorkingDirectory(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worker/");
	mProcess->start(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worker/worker.exe", QStringList());
	res = mProcess->waitForStarted();
	mProcessMutex.unlock();

	if (res)
		mProcessReadFuture = QtConcurrent::run(this, &WorkerProcessHost::readProcessAsync);
	else
		stopProcess();

	return res;
}

bool WorkerProcessHost::stopProcess()
{
	bool res = false;

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

void WorkerProcessHost::writeToProcess(QString _cmd)
{
	mProcessMutex.lock();
	if (mProcess)
		mProcess->write((_cmd.simplified() + ComputeGridGlobals::ProcessCommandSuffix).toLocal8Bit());
	mProcessMutex.unlock();
}

bool WorkerProcessHost::loadProcessArchive()
{
	QString msg;
	bool res = false;

	QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worker");
	if (
		(dir.exists() && !dir.removeRecursively())
		|| (!dir.exists() && !dir.mkpath(dir.absolutePath())))
		msg = QString("File system I/O error! Directory:'%1' couldn't modify.").arg(dir.absolutePath());
	else if (!QFile::exists(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worker.zip"))
		msg = QString("File system I/O error! Archive:'%1' couldn't find.").arg(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "worker.zip");
	else
	{
		QStringList files = JlCompress::extractDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worker.zip", dir.absolutePath());
		if (files.isEmpty() || !files.contains(dir.absolutePath() + "/worker.exe"))
			msg = QString("Archive error! '%1' is invalid, doesn't contain executable: %2").arg("worker.zip").arg("worker.exe");
		else
		{
			QProcess p;
			p.start(dir.absolutePath() + "/worker.exe", QStringList() << "-test");

			if (!p.waitForFinished(10000))
			{
				p.kill();
				msg = QString("Executable is timed out.");
			}
			else if (p.exitCode() < 0)
				msg = QString("Executable exited with code: %1.").arg(p.exitCode());
			else
			{
				msg = QString("%1-Process has been successfully set.").arg("Worker");
				res = true;
			}
		}
	}

	emit log(msg, res ? LT_INFO : LT_ERROR);
	return res;
}

bool WorkerProcessHost::sendPacket(NetworkPacket & _np)
{
	bool res = false;

	mNetworkMutex.lock();

	if (mNetClient)
		res = mNetClient->sendPacket(_np);

	mNetworkMutex.unlock();

	return res;
}

bool WorkerProcessHost::isNetworkConnected()
{
	bool res = false;

	mNetworkMutex.lock();

	if (mNetClient)
		res = mNetClient->state() == QAbstractSocket::ConnectedState;

	mNetworkMutex.unlock();
	
	return res;
}

void WorkerProcessHost::readProcessAsync()
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

void WorkerProcessHost::handleProcessCommand(QString _command)
{
	ProcessCommand pc;
	QStringList args;

	if (ComputeGridGlobals::parseProcessCommand(_command, pc, args))
	{
		NetworkPacket np(NPT_DATA);

		switch (pc)
		{
		case ComputeGrid::PC_WORKER_DATA:
			np.setTypeId(DPT_WORKER_DATA);
			break;

		case ComputeGrid::PC_LOG:
			np.setTypeId(DPT_LOG);
			emit log(QString("%1").arg(args[2]), (LogType)(args[1].toUInt()), (LogSource)(args[0].toUInt()));
			break;

		case ComputeGrid::PC_STATUS_MESSAGE:
			emit statusMessage(args[0]);
			return; // RETURN!

		default:
			emit log("Unknown process command: " + _command, LT_WARNING);
			return; // RETURN!
		}

		QDataStream ds(np.dataPtr(), QIODevice::WriteOnly);
		ds << args;
		sendPacket(np);
	}
}

#pragma region Slots
void WorkerProcessHost::processStarted()
{
	emit log("Process started.");
}

void WorkerProcessHost::processFinished(int _exitCode, QProcess::ExitStatus _exitStatus)
{
	emit log(
		QString("Process finished. Exit-Code:%1 (%2)").arg(_exitCode).arg(_exitStatus == QProcess::NormalExit ? "Normal Exit" : "Crash Exit"),
		_exitStatus == QProcess::NormalExit ? LT_INFO : LT_ERROR
	);

	if (isNetworkConnected())
	{
		NetworkPacket np(NPT_DATA);
		np.setTypeId(DPT_WORKER_EXIT);
		QDataStream ds(np.dataPtr(), QIODevice::WriteOnly);
		ds << QString::number(_exitCode);
		ds << QString::number(_exitStatus);
		sendPacket(np);
	}
}

void WorkerProcessHost::networkConnected()
{
	emit log(QString("Connected to the Grid-Manager."));

	mIsAlive = true;
	mKeepAliveTimer->start(mKeepAliveIntervalMs);
}

void WorkerProcessHost::networkDisconnected()
{
	mKeepAliveTimer->stop();
	mIsAlive = false;

	emit log(QString("Disconnected from the Grid-Manager."), LT_WARNING);
	writeToProcess(ComputeGridGlobals::makeProcessCommand(PC_WORKER_EXIT, QString::number(-1)));

	stopProcess();

	emit workerOutGrid();
}

void WorkerProcessHost::networkPacketReceived(NetworkPacket _packet)
{
	mIsAlive = true;

	DataPacketType dpt = (DataPacketType)_packet.typeId();

	QStringList args;
	QDataStream dsIn(_packet.dataPtr(), QIODevice::ReadOnly);

	switch (dpt)
	{
	case ComputeGrid::DPT_HEARTHBEAT:
		break;

	case ComputeGrid::DPT_GRID_ATTACH:
	{
		QString err;
		QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
		if (!dir.exists() && !dir.mkpath(dir.absolutePath()))
			err = QString("File system I/O error! Directory:'%1' couldn't modify.").arg(dir.absolutePath());
		else
		{
			QFile f(dir.absolutePath() + "/worker.zip");
			if (f.open(QIODevice::WriteOnly))
			{
				f.write(*_packet.dataPtr());
				f.close();

				if (loadProcessArchive())
				{
					if (startProcess())
					{
						args.clear();
						args.append(QString::number(QThread::idealThreadCount()));

						//writeToProcess(ComputeGridGlobals::makeProcessCommand(PC_GRID_WORKER_IN, QString()));

						emit workerInGrid();

						NetworkPacket np(NPT_DATA);
						np.setTypeId(DPT_GRID_WORKER_READY);
						QDataStream dsOut(np.dataPtr(), QIODevice::WriteOnly);
						dsOut << args;
						sendPacket(np);
					}
					else
						err = "Worker process start error!";
				}
				else
					err = "Worker archive extract error!";
			}
			else
				err = QString("File system I/O error! Worker archive couldn't create at directory: %1").arg(dir.absolutePath());
		}

		if (!err.isEmpty())
		{
			args.clear();
			args.append(QString::number(LS_GW));
			args.append(QString::number(LT_ERROR));
			args.append(err);
			NetworkPacket np(NPT_DATA);
			np.setTypeId(DPT_LOG);
			QDataStream dsOut(np.dataPtr(), QIODevice::WriteOnly);
			dsOut << args;
			sendPacket(np);

			emit log(err, LT_ERROR);
		}
	}
	break;

	case ComputeGrid::DPT_WORKER_DATA:
		args.clear();
		dsIn >> args;
		args.removeFirst(); // remove worker info
		writeToProcess(ComputeGridGlobals::makeProcessCommand(PC_WORKER_DATA, args));
		break;

	case ComputeGrid::DPT_WORKER_EXIT:
		args.clear();
		dsIn >> args;
		args.removeFirst(); // remove worker info
		writeToProcess(ComputeGridGlobals::makeProcessCommand(PC_WORKER_EXIT, args));
		break;

	default:
		emit log(QString("Unknown network packet received from the Grid-Manager."), LT_WARNING);
		break;
	}
}

void WorkerProcessHost::networkError(QAbstractSocket::SocketError _socketError)
{
	emit log(QString("Socket error: %1").arg((_socketError >= 0 && _socketError < LiteralSocketError.count()) ? LiteralSocketError[_socketError] : "Unknown Network Error"), LT_ERROR);

	// to do: consider restart?
}

void WorkerProcessHost::keepAliveTimerTimeout()
{
	if (!mIsAlive)
		QMetaObject::invokeMethod(this, "disconnectFromNetworkServer");
	
	mIsAlive = false;
}
#pragma endregion
