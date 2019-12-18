#include "uicomputegridworker.h"
#include <QtWidgets/QApplication>
#include <QSharedMemory>

QSharedMemory sharedMemory;

int main(int argc, char *argv[])
{
	sharedMemory.setKey("computegridworker");

	if (sharedMemory.create(1))
	{
		QApplication a(argc, argv);
		UIComputeGridWorker w;
		//w.show();
		return a.exec();
	}

	return -1;
}
