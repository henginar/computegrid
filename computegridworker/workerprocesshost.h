#pragma once

#include <QObject>
#include <QProcess>
#include <QMutex>
#include <QFuture>
#include <QStringList>
#include <QTimer>
#include "computegridcommons.hpp"
#include "networkclient.h"

using namespace Networking;

class WorkerProcessHost : public QObject
{
	Q_OBJECT

public:
	WorkerProcessHost(int _keepAliveIntervalMs = 300000, QObject * _parent = nullptr);
	~WorkerProcessHost();

	bool connectToNetworkServer(QString _ip, quint16 _port, uint _timeOut = NetworkingGlobals::DefaultTimeOut);
	Q_INVOKABLE bool disconnectFromNetworkServer();

	bool startProcess();
	bool stopProcess();
	void writeToProcess(QString _cmd);
	bool loadProcessArchive();

private:
	void readProcessAsync();
	Q_INVOKABLE void handleProcessCommand(QString _command);

	QProcess * mProcess;
	QMutex mProcessMutex;
	QFuture<void> mProcessReadFuture;
	NetworkClient * mNetClient;
	QTimer * mKeepAliveTimer;
	int mKeepAliveIntervalMs;
	bool mIsAlive;

#pragma region Signals-Slots
signals:
	void workerInGrid();
	void workerOutGrid();
	void log(QString _message, ComputeGrid::LogType _logType = ComputeGrid::LT_INFO, ComputeGrid::LogSource _logSource = ComputeGrid::LS_GW);
	void statusMessage(QString _message);

public slots:
	void processStarted();
	void processFinished(int _exitCode, QProcess::ExitStatus _exitStatus);

	void networkConnected();
	void networkDisconnected();
	void networkPacketReceived(NetworkPacket _packet);
	void networkError(QAbstractSocket::SocketError _socketError);

	void keepAliveTimerTimeout();
#pragma endregion

};
