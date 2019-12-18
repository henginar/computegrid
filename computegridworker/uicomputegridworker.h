#pragma once

#include <QtWidgets/QMainWindow>
#include <QSystemTrayIcon>
#include <QAction>
#include <QMenu>
#include "ui_uicomputegridworker.h"
#include "workerprocesshost.h"

#define UI_LOG_BLOCK_LIMIT 10000

class UIComputeGridWorker : public QMainWindow
{
	Q_OBJECT

public:
	UIComputeGridWorker(QWidget * _parent = Q_NULLPTR);
	~UIComputeGridWorker();

protected:
	void closeEvent(QCloseEvent * _e) override;

private:
	Q_INVOKABLE void init();
	void appendLog(QString _message, QColor _color = Qt::black);

	Ui::UIComputeGridWorkerClass ui;
	QSystemTrayIcon * mSystemTrayIcon;
	QMenu * mSystemTrayMenu;
	QAction * mQuitAction;
	QAction * mShowHideAction;
	WorkerProcessHost mProcessHost;
	QString mNetServerIP;
	uint 
		mNetServerPort,
		mConnectTimeOut,
		mReconnectTimeOut;
	bool mExitFlag;

#pragma region Signals-Slots
public slots:
	void systemTrayIconActivated(QSystemTrayIcon::ActivationReason _activationReason);
	void exitAction();
	void showHideAction();
	
	void workerInGrid();
	void workerOutGrid();
	void log(QString _message, ComputeGrid::LogType _logType, ComputeGrid::LogSource _logSource);
	void statusMessage(QString _message);
#pragma endregion
};
