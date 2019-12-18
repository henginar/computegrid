#include "uicomputegridmanager.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
	QApplication a(argc, argv);
	UIComputeGridManager w;
	w.show();
	return a.exec();
}
