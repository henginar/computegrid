#include "uicomputegridworker.h"
#include <QSettings>
#include <QTimer>
#include <QScrollBar>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextBlock>
#include <QDateTime>

UIComputeGridWorker::UIComputeGridWorker(QWidget * _parent)
	: QMainWindow(_parent),
	mNetServerIP(NetworkingGlobals::DefaultServerIP),
	mNetServerPort(NetworkingGlobals::DefaultServerPort),
	mConnectTimeOut(NetworkingGlobals::DefaultTimeOut),
	mReconnectTimeOut(NetworkingGlobals::DefaultTimeOut),
	mExitFlag(false)
{
	ui.setupUi(this);

#pragma region System Tray Icon
	mSystemTrayIcon = new QSystemTrayIcon(QIcon(":/UIComputeGridWorker/computegridworker.ico"), this);
	QObject::connect(mSystemTrayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(systemTrayIconActivated(QSystemTrayIcon::ActivationReason)));

	mSystemTrayMenu = new QMenu(this);

	mQuitAction = new QAction(QIcon(":/UIComputeGridWorker/Resources/close.png"), "Exit", mSystemTrayMenu);
	QObject::connect(mQuitAction, SIGNAL(triggered()), this, SLOT(exitAction()));
	mSystemTrayMenu->addAction(mQuitAction);

	mShowHideAction = new QAction(QIcon(":/UIComputeGridWorker/Resources/show.png"), "Show/Hide", mSystemTrayMenu);
	QObject::connect(mShowHideAction, SIGNAL(triggered()), this, SLOT(showHideAction()));
	mSystemTrayMenu->addAction(mShowHideAction);

	mSystemTrayIcon->setContextMenu(mSystemTrayMenu);
	mSystemTrayIcon->show();
#pragma endregion

	QObject::connect(&mProcessHost, SIGNAL(workerInGrid()), this, SLOT(workerInGrid()));
	QObject::connect(&mProcessHost, SIGNAL(workerOutGrid()), this, SLOT(workerOutGrid()));
	QObject::connect(&mProcessHost, SIGNAL(log(QString, ComputeGrid::LogType, ComputeGrid::LogSource)), this, SLOT(log(QString, ComputeGrid::LogType, ComputeGrid::LogSource)));
	QObject::connect(&mProcessHost, SIGNAL(statusMessage(QString)), this, SLOT(statusMessage(QString)));

#pragma region Read Settings
	QSettings settings(QCoreApplication::applicationName() + "_config.ini", QSettings::IniFormat);
	settings.beginGroup("General");
	mNetServerIP = settings.value("ServerIP", NetworkingGlobals::DefaultServerIP).toString();
	mNetServerPort = settings.value("ServerPort", NetworkingGlobals::DefaultServerPort).toUInt();
	mConnectTimeOut = settings.value("ConnectTimeOut", NetworkingGlobals::DefaultTimeOut).toUInt();
	mReconnectTimeOut = settings.value("ReconnectTimeOut", NetworkingGlobals::DefaultTimeOut).toUInt();
	settings.endGroup();
#pragma endregion

	QTimer::singleShot(500, this, SLOT(init()));
}

UIComputeGridWorker::~UIComputeGridWorker()
{
	if (mShowHideAction)
		delete mShowHideAction;

	if (mQuitAction)
		delete mQuitAction;

	if (mSystemTrayMenu)
		delete mSystemTrayMenu;

	if (mSystemTrayIcon)
		delete mSystemTrayIcon;
}

void UIComputeGridWorker::closeEvent(QCloseEvent * _e)
{
	this->hide();
	_e->ignore();
}

void UIComputeGridWorker::init()
{
	appendLog(QString("Connecting to grid manager at %1:%2").arg(mNetServerIP).arg(mNetServerPort));

	if (mProcessHost.connectToNetworkServer(mNetServerIP, mNetServerPort, mConnectTimeOut))
	{
		appendLog("Connection established.");
	}
	else
	{
		appendLog(QString("Connection failed. Retrying in %1 ms.").arg(mReconnectTimeOut), Qt::red);
		QTimer::singleShot(mReconnectTimeOut, this, SLOT(init()));
	}
}

void UIComputeGridWorker::appendLog(QString _message, QColor _color)
{
	QScrollBar * sBar = ui.textEditLog->verticalScrollBar();
	const bool atBottom = ui.textEditLog->verticalScrollBar()->value() == sBar->maximum();
	QTextDocument * doc = ui.textEditLog->document();

	while (doc->blockCount() >= UI_LOG_BLOCK_LIMIT)
	{
		QTextCursor blockCur = QTextCursor(doc->firstBlock());
		blockCur.select(QTextCursor::BlockUnderCursor);
		blockCur.removeSelectedText();
		blockCur.deleteChar();
	}

	QTextCursor cursor(doc);
	cursor.movePosition(QTextCursor::End);
	cursor.beginEditBlock();
	cursor.insertBlock();
	cursor.insertHtml(QString("<font color=\"%3\">%1: %2</font>\n").arg(QDateTime::currentDateTime().toString(Qt::DateFormat::ISODate).replace('T', ' ')).arg(_message).arg(_color.name()));
	cursor.endEditBlock();

	if (atBottom)
		sBar->setValue(sBar->maximum());
}

#pragma region Slots
void UIComputeGridWorker::systemTrayIconActivated(QSystemTrayIcon::ActivationReason _activationReason)
{
	switch (_activationReason)
	{
	case QSystemTrayIcon::Unknown:
		break;

	case QSystemTrayIcon::Context:
		break;

	case QSystemTrayIcon::DoubleClick:
		showHideAction();
		break;

	case QSystemTrayIcon::Trigger:
		break;

	case QSystemTrayIcon::MiddleClick:
		break;

	default:
		break;
	}
}

void UIComputeGridWorker::exitAction()
{
	mExitFlag = true;
	qApp->exit();
}

void UIComputeGridWorker::showHideAction()
{
	if (this->isHidden())
		this->showNormal();
	else
		this->hide();
}

void UIComputeGridWorker::workerInGrid()
{
	if (mSystemTrayIcon && !mExitFlag)
		mSystemTrayIcon->showMessage(this->windowTitle(), "Your PC has joined to the compute-grid.");
}

void UIComputeGridWorker::workerOutGrid()
{
	if (!mExitFlag)
	{
		if (mSystemTrayIcon)
			mSystemTrayIcon->showMessage(this->windowTitle(), "Your PC has left from the compute-grid.", QSystemTrayIcon::Warning);

		QTimer::singleShot(100, this, SLOT(init()));
	}

	ui.labelStatus->clear();
}

void UIComputeGridWorker::log(QString _message, ComputeGrid::LogType _logType, ComputeGrid::LogSource _logSource)
{
	QColor c = Qt::black;
	switch (_logType)
	{
	case ComputeGrid::LT_INFO:
		c = Qt::blue;
		break;

	case ComputeGrid::LT_WARNING:
		c = Qt::magenta;
		break;

	case ComputeGrid::LT_ERROR:
		c = Qt::red;
		break;
	}

	appendLog(QString("[%1:%2]%3").arg(ComputeGrid::LiteralLogSource[_logSource]).arg(ComputeGrid::LiteralLogType[_logType]).arg(_message), c);
}

void UIComputeGridWorker::statusMessage(QString _message)
{
	ui.labelStatus->setText(_message);
}
#pragma endregion
