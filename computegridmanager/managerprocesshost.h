#pragma once

#include <QObject>
#include <QProcess>
#include <QMutex>
#include <QFuture>
#include <QString> 
#include <QStringList>
#include <QByteArray>
#include <QTimer>
#include "computegridcommons.hpp"
#include "networkserver.h"

using namespace Networking;

class ManagerProcessHost : public QObject
{
	Q_OBJECT

public:
	ManagerProcessHost(int _keepAliveIntervalMs = NetworkingGlobals::DefaultTimeOut, QObject * _parent = nullptr);
	~ManagerProcessHost();

	bool startProcess(quint16 _port);
	bool stopProcess();
	void writeToProcess(QString _cmd);

	bool loadProcessArchive(QString _archiveFile, bool _isManagerProcess = true);
	bool attachWorkerArchive();

private:
	bool startNetworkServer(quint16 _port);
	bool stopNetworkServer();
	bool sendPacket(NetworkPacket & _np, NetworkClientInfo & _nci);
	bool isNetworkListening();
	QList<NetworkClientInfo> networkClients();
	QString lastNetworkError();

	void readProcessAsync();
	Q_INVOKABLE void handleProcessCommand(QString _command);

	Q_INVOKABLE void keepAliveClients();
	bool findWorkerClient(const QString & _worker, NetworkClientInfo * _nci);

	QProcess * mProcess;
	QFuture<void> mProcessReadFuture;
	NetworkServer * mNetServer;
	QTimer * mKeepAliveTimer;
	int mKeepAliveIntervalMs;
	QByteArray mWorkerProcessData;
	QMutex mProcessMutex;
	QMutex mNetworkMutex;

#pragma region Signals-Slots
signals:
	void workerInGrid(QString _worker, int _capacity);
	void workerOutGrid(QString _worker);
	void log(QString _message, ComputeGrid::LogType _logType = ComputeGrid::LT_INFO, ComputeGrid::LogSource _logSource = ComputeGrid::LS_GM);
	void statusMessage(QString _message);

public slots:
	void processStarted();
	void processFinished(int _exitCode, QProcess::ExitStatus _exitStatus);

	void networkClientConnected(NetworkClientInfo _clientInfo);
	void networkClientDisconnected(NetworkClientInfo _clientInfo);
	void networkClientError(NetworkClientInfo _clientInfo, QAbstractSocket::SocketError _socketError);
	void networkPacketReceived(NetworkClientInfo _clientInfo, NetworkPacket _packet);
	void networkError(QAbstractSocket::SocketError _socketError);

	void keepAliveTimerTimeout();
#pragma endregion

};
