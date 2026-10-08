// SPDX-License-Identifier: GPL-2.0-or-later
// Compiled in OBSBasic, where the current profile's authenticated service lives.
#include <QDockWidget>
#include <QMessageBox>

void OBSBasic::CherriesManageBroadcast()
{
    auto notice = [this](const QString &message) {
        QMessageBox dialog(QMessageBox::Information, "Manage Broadcast", message,
                           QMessageBox::Ok, this);
        dialog.setObjectName("cherriesBroadcastNotice");
        dialog.exec();
    };
    Auth *current = GetAuth();
    if (!current) {
        notice("Connect your main Twitch or YouTube account in Settings > Stream to manage its broadcast here. Additional YouTube accounts can be connected through that destination's Create / select broadcast action.");
        return;
    }
    const QString service = QString::fromUtf8(current->service());
    if (service.compare("Twitch", Qt::CaseInsensitive) == 0) {
        // The normal Twitch loader waits for browser cookies. An explicit click
        // must also open the page when its web session still needs signing in.
        if (!findChild<QDockWidget *>("twitchInfo"))
            QMetaObject::invokeMethod(current, "CherriesShowStreamInfo", Qt::DirectConnection);
        if (auto info = findChild<QDockWidget *>("twitchInfo")) {
            info->show();
            info->raise();
            info->activateWindow();
        } else {
            notice("Twitch Stream Information could not be loaded. Check your Twitch connection in Settings > Stream and try again.");
        }
        return;
    }
#ifdef YOUTUBE_ENABLED
    if (IsYouTubeService(current->service())) {
        // Open setup only; BroadcastButtonClicked can start/stop a live broadcast
        // and is not a Qt slot that plugins can invoke by name.
        SetupBroadcast();
        return;
    }
#endif
    notice("Broadcast management is available for a connected Twitch or YouTube account. Configure your main account in Settings > Stream.");
}
