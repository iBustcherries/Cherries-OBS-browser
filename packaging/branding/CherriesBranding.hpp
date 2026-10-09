#pragma once
#include <QIcon>
#include <QPainter>
#include <QPixmap>

// Use the supplied logo at rest; retain distinct active/paused tray indicators.
inline QIcon CherriesTrayIcon(int state = 0)
{
    QPixmap icon(64, 64);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.drawPixmap(icon.rect(), QPixmap(":/res/images/obs.png"));
    if (state) {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Qt::black, 3));
        painter.setBrush(state == 2 ? QColor("#ffc44d") : QColor("#ff304f"));
        painter.drawEllipse(QRectF(40, 40, 21, 21));
        if (state == 2) {
            painter.fillRect(QRect(46, 46, 3, 10), Qt::black);
            painter.fillRect(QRect(52, 46, 3, 10), Qt::black);
        }
    }
    painter.end();
    return QIcon(icon);
}
