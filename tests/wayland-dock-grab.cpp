#include "panel/browser-panel-dock.hpp"

#include <QEventLoop>
#include <QMainWindow>
#include <cassert>

static void waitForCleanup()
{
	QEventLoop loop;
	QTimer::singleShot(150, &loop, &QEventLoop::quit);
	loop.exec();
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	QMainWindow main;
	QDockWidget dock("Browser dock", &main);
	main.addDockWidget(Qt::RightDockWidgetArea, &dock);
	main.show();
	InstallWaylandDockDragCleanup(&dock);
	InstallWaylandDockDragCleanup(&dock);
	assert(dock.findChildren<QTimer *>().size() == 1);

	// Reproduce Qt's ordering: complete the transition, then acquire a
	// mouse grab after the platform drag has already released its button.
	dock.setFloating(true);
	dock.setFloating(false);
	dock.grabMouse();
	assert(QWidget::mouseGrabber() == &dock);
	waitForCleanup();
	assert(QWidget::mouseGrabber() == nullptr);

	// Another widget's legitimate grab must never be released by this helper.
	QWidget other;
	other.show();
	dock.setFloating(true);
	dock.setFloating(false);
	other.grabMouse();
	waitForCleanup();
	assert(QWidget::mouseGrabber() == &other);
	other.releaseMouse();

	// Pending cleanup belongs to the dock and must not outlive it.
	auto temporary = new QDockWidget("Temporary", &main);
	main.addDockWidget(Qt::LeftDockWidgetArea, temporary);
	InstallWaylandDockDragCleanup(temporary);
	temporary->setFloating(true);
	delete temporary;
	waitForCleanup();
	qInfo("Wayland dock mouse-grab regression checks passed");
}
