#pragma once

#include <QApplication>
#include <QDockWidget>
#include <QTimer>
#include <QVariant>
#include <memory>

inline void InstallWaylandDockDragCleanup(QDockWidget *dock)
{
	if (!dock || dock->property("cherriesWaylandDragCleanup").toBool())
		return;
	dock->setProperty("cherriesWaylandDragCleanup", true);

	auto timer = new QTimer(dock);
	timer->setInterval(50);
	auto remaining = std::make_shared<int>(0);
	QObject::connect(timer, &QTimer::timeout, dock, [dock, timer, remaining]() {
		// Qt 6.11's mouseMoveEvent calls grabMouse() after startDrag(). On
		// Wayland startDrag() blocks until the drop and endDrag() have
		// already released the grab. That final grab has no matching release.
		// Wait for the native drag to unwind and the button to be released.
		if (QApplication::mouseButtons() == Qt::NoButton && QWidget::mouseGrabber() == dock) {
			dock->releaseMouse();
			qInfo("[cherries-obs-browser] Released completed Wayland dock drag mouse grab");
			timer->stop();
		} else if (--*remaining <= 0) {
			timer->stop();
		}
	});
	auto arm = [timer, remaining]() {
		*remaining = 20;
		timer->start();
	};
	QObject::connect(dock, &QDockWidget::topLevelChanged, dock, arm);
	QObject::connect(dock, &QDockWidget::dockLocationChanged, dock, arm);
}
