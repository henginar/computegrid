#include "uicomputegridmanager.h"
#include <QSettings>
#include <QScrollBar>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextBlock>
#include <QDateTime>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>

UIComputeGridManager::UIComputeGridManager(QWidget * _parent)
	: QMainWindow(_parent),
	mSentCommandsShowIndex(-1)
{
	ui.setupUi(this);

	qApp->installEventFilter(this);

	// init ui controls
	ui.groupBoxProcessor->setEnabled(true);
	ui.pushButtonProcessorStart->setEnabled(true);
	ui.spinBoxNetworkPort->setEnabled(true);
	ui.pushButtonProcessorStop->setEnabled(false);
	ui.frameProcessorStartStop->setEnabled(false);
	ui.pushButtonProcessorSetManager->setEnabled(true);
	ui.pushButtonProcessorSetWorker->setEnabled(false);
	
	ui.groupBoxCommandPrompt->setEnabled(false);

	QObject::connect(&mProcessHost, SIGNAL(workerInGrid(QString, int)), this, SLOT(workerInGrid(QString, int)));
	QObject::connect(&mProcessHost, SIGNAL(workerOutGrid(QString)), this, SLOT(workerOutGrid(QString)));
	QObject::connect(&mProcessHost, SIGNAL(log(QString, ComputeGrid::LogType, ComputeGrid::LogSource)), this, SLOT(log(QString, ComputeGrid::LogType, ComputeGrid::LogSource)));
	QObject::connect(&mProcessHost, SIGNAL(statusMessage(QString)), this, SLOT(statusMessage(QString)));

	QSettings settings(QCoreApplication::applicationName() + "_config.ini", QSettings::IniFormat);
	settings.beginGroup("/General");
	ui.spinBoxNetworkPort->setValue(settings.value("/ServerPort", NetworkingGlobals::DefaultServerPort).toUInt());
	ui.spinBoxWorkerLimit->setValue(settings.value("/WorkerLimit", 0).toUInt());
	settings.endGroup();

	refreshWorkersList();
}

UIComputeGridManager::~UIComputeGridManager()
{
}

bool UIComputeGridManager::eventFilter(QObject * _obj, QEvent * _ev)
{
	if (_obj == ui.lineEditCommandPrompt && _ev->type() == QEvent::KeyPress)
	{
		QKeyEvent * key = static_cast<QKeyEvent *>(_ev);
		if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
			on_pushButtonCommandPromptSend_clicked();
		else if (key->key() == Qt::Key::Key_Up && mSentCommands.count() > 0)
		{
			if (mSentCommandsShowIndex == -1)
				mSentCommandsShowIndex = mSentCommands.count() - 1;
			else if (--mSentCommandsShowIndex < 0)
				mSentCommandsShowIndex = mSentCommands.count() - 1;

			ui.lineEditCommandPrompt->setText(mSentCommands.at(mSentCommandsShowIndex));
		}
		else if (key->key() == Qt::Key::Key_Down && mSentCommands.count() > 0)
		{
			if (mSentCommandsShowIndex == -1)
				mSentCommandsShowIndex = 0;
			else if (++mSentCommandsShowIndex >= mSentCommands.count())
				mSentCommandsShowIndex = 0;

			ui.lineEditCommandPrompt->setText(mSentCommands.at(mSentCommandsShowIndex));
		}
	}

	return QObject::eventFilter(_obj, _ev);
}

void UIComputeGridManager::appendLog(QString _message, QColor _color)
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

void UIComputeGridManager::refreshWorkersList()
{
	ui.listWidgetWorkers->clear();

	int totalCap = 0;
	for (QMap<QString, int>::iterator it = mWorkerCapacityMap.begin(); it != mWorkerCapacityMap.end(); ++it)
	{
		totalCap += it.value();
		ui.listWidgetWorkers->addItem(QString("%1 (Cap.: %2)").arg(it.key()).arg(it.value()));
	}

	ui.labelGridWorkersStatus->setText(QString("%1 workers with %2 parallel compute capacity.").arg(mWorkerCapacityMap.count()).arg(totalCap));
}

#pragma region Slots
void UIComputeGridManager::on_pushButtonProcessorSetManager_clicked()
{
	ui.pushButtonProcessorSetManager->setEnabled(false);
	
	QFile fManagerExe(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/manager/manager.exe");
	if (fManagerExe.exists() && QMessageBox::Yes == QMessageBox(QMessageBox::Warning, "Warning", "A manager installation found and it will be overwritten if you install new one!\n\nDo you want to use existing instead of installing a new one?", QMessageBox::Yes | QMessageBox::No).exec())
	{
		ui.pushButtonProcessorSetWorker->setEnabled(true);
		return;
	}

	mProcManagerArchivePath = QFileDialog::getOpenFileName(this, "Manager Process Archive", QDir::currentPath(), "Zip Archives(*.zip)");
	if (!mProcManagerArchivePath.isEmpty() && mProcessHost.loadProcessArchive(mProcManagerArchivePath))
	{
		ui.pushButtonProcessorSetWorker->setEnabled(true);
		return;
	}
		
	ui.pushButtonProcessorSetManager->setEnabled(true);
}

void UIComputeGridManager::on_pushButtonProcessorSetWorker_clicked()
{
	ui.pushButtonProcessorSetWorker->setEnabled(false);

	QFile fWorkerZip(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/worker.zip");
	if (fWorkerZip.exists() 
		&& QMessageBox::Yes == QMessageBox(QMessageBox::Warning, "Warning", "A worker installation found and it will be overwritten if you install new one!\n\nDo you want to use existing instead of installing a new one?", QMessageBox::Yes | QMessageBox::No).exec()
		&& mProcessHost.attachWorkerArchive())
	{
		ui.frameProcessorStartStop->setEnabled(true);
		return;
	}

	mProcWorkerArchivePath = QFileDialog::getOpenFileName(this, "Worker Process Archive", QDir::currentPath(), "Zip Archives(*.zip)");
	if (!mProcWorkerArchivePath.isEmpty() && mProcessHost.loadProcessArchive(mProcWorkerArchivePath, false))
	{
		ui.frameProcessorStartStop->setEnabled(true);
		return;
	}

	ui.pushButtonProcessorSetWorker->setEnabled(true);
}

void UIComputeGridManager::on_pushButtonProcessorStart_clicked()
{
	ui.pushButtonProcessorStart->setEnabled(false);
	ui.spinBoxNetworkPort->setEnabled(false);
	if (mProcessHost.startProcess(ui.spinBoxNetworkPort->value(), ui.spinBoxWorkerLimit->value()))
	{
		ui.pushButtonProcessorStop->setEnabled(true);
		ui.groupBoxCommandPrompt->setEnabled(true);
	}
	else
	{
		ui.pushButtonProcessorStart->setEnabled(true);
		ui.spinBoxNetworkPort->setEnabled(true);
	}
}

void UIComputeGridManager::on_pushButtonProcessorStop_clicked()
{
	ui.pushButtonProcessorStop->setEnabled(false);
	if (mProcessHost.stopProcess())
	{
		mWorkerCapacityMap.clear();
		refreshWorkersList();
		ui.labelStatus->clear();

		ui.groupBoxCommandPrompt->setEnabled(false);
		ui.pushButtonProcessorStart->setEnabled(true);
		ui.spinBoxNetworkPort->setEnabled(true);
	}
	else
		ui.pushButtonProcessorStop->setEnabled(true);
}

void UIComputeGridManager::on_pushButtonCommandPromptSend_clicked()
{
	QString cmd = ui.lineEditCommandPrompt->text();
	if (!cmd.isEmpty())
	{
		mSentCommands.push_back(cmd);
		mSentCommandsShowIndex = -1;
		ui.lineEditCommandPrompt->clear();

		appendLog(cmd, Qt::darkGreen);
		mProcessHost.writeToProcess(ComputeGrid::ComputeGridGlobals::makeProcessCommand(ComputeGrid::PC_TERMINAL_COMMAND, cmd.split(' ')));
	}
}

void UIComputeGridManager::workerInGrid(QString _worker, int _capacity)
{
	if (!mWorkerCapacityMap.contains(_worker))
		mWorkerCapacityMap.insert(_worker, _capacity);
	else
		mWorkerCapacityMap[_worker] = _capacity;

	refreshWorkersList();
}

void UIComputeGridManager::workerOutGrid(QString _worker)
{
	if (mWorkerCapacityMap.contains(_worker))
		mWorkerCapacityMap.remove(_worker);

	refreshWorkersList();
}

void UIComputeGridManager::log(QString _message, ComputeGrid::LogType _logType, ComputeGrid::LogSource _logSource)
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

void UIComputeGridManager::statusMessage(QString _message)
{
	ui.labelStatus->setText(_message);
}
#pragma endregion