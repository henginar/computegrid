#pragma once

#include <QtWidgets/QMainWindow>
#include <QVector>
#include <QMap>
#include "ui_uicomputegridmanager.h"
#include "managerprocesshost.h"

#define UI_LOG_BLOCK_LIMIT 10000

class UIComputeGridManager : public QMainWindow
{
	Q_OBJECT

public:
	UIComputeGridManager(QWidget * _parent = Q_NULLPTR);
	~UIComputeGridManager();

protected:
	bool eventFilter(QObject * _obj, QEvent * _ev);

private:
	void appendLog(QString _message, QColor _color = Qt::black);
	void refreshWorkersList();

	Ui::UIComputeGridManagerClass ui;
	ManagerProcessHost mProcessHost;
	QString mProcManagerArchivePath;
	QString mProcWorkerArchivePath;
	QVector<QString> mSentCommands;
	int mSentCommandsShowIndex;
	QMap<QString, int> mWorkerCapacityMap;

#pragma region Signals-Slots
public slots:
	void on_pushButtonProcessorSetManager_clicked();
	void on_pushButtonProcessorSetWorker_clicked();
	void on_pushButtonProcessorStart_clicked();
	void on_pushButtonProcessorStop_clicked();
	void on_pushButtonCommandPromptSend_clicked();

	void workerInGrid(QString _worker, int _capacity);
	void workerOutGrid(QString _worker);
	void log(QString _message, ComputeGrid::LogType _logType, ComputeGrid::LogSource _logSource);
	void statusMessage(QString _message);
#pragma endregion
};
