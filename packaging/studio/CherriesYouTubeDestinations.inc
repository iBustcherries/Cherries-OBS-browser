// SPDX-License-Identifier: GPL-2.0-or-later
// Native YouTube account/broadcast bridge. Destination tokens never cross into
// the multistream plugin; only the chosen ingest binding does.
#include <QComboBox>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariantMap>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QDateTimeEdit>

QVariantMap OBSBasic::CherriesYouTubeDestination(QVariantMap request)
{
    auto fail = [this](const QString &message) -> QVariantMap {
        QMessageBox::warning(this, "YouTube destination", message);
        return {};
    };
#ifndef YOUTUBE_ENABLED
    Q_UNUSED(request);
    return fail("YouTube account integration is unavailable in this build.");
#else
    const QString operation = request.value("operation", "configure").toString();
    if (operation != "configure" && operation != "edit" && operation != "live" && operation != "complete")
        return fail("Unknown broadcast action.");
    auto config = Config();
    const char *stored = config_get_string(config, "CherriesYouTube", "Accounts");
    QJsonParseError parseError;
    const QByteArray storedAccounts = stored ? QByteArray(stored) : QByteArray();
    const auto document = QJsonDocument::fromJson(storedAccounts, &parseError);
    if (!storedAccounts.isEmpty() && (parseError.error != QJsonParseError::NoError || !document.isObject()))
        return fail("Saved YouTube account data is unreadable. Restore the OBS profile backup before reconnecting accounts.");
    QJsonObject accounts = document.object();
    auto api = std::make_shared<YoutubeApiWrappers>(youtubeServices[1]);
    QString accountId = request.value("account_id").toString();
    auto primary = dynamic_cast<YoutubeApiWrappers *>(GetAuth());

    if (operation == "configure") {
        QDialog choose(this);
        choose.setObjectName("cherriesYouTubeAccounts");
        choose.setWindowTitle("YouTube account — " + request.value("name").toString());
        auto layout = new QVBoxLayout(&choose);
        auto label = new QLabel("Choose a connected channel, or connect another. Your main OBS streaming account stays unchanged.");
        label->setWordWrap(true); layout->addWidget(label);
        auto channels = new QComboBox(&choose);
        if (primary) channels->addItem("Use main OBS YouTube account", "@primary");
        for (auto it = accounts.begin(); it != accounts.end(); ++it)
            channels->addItem(it.value().toObject().value("title").toString() + " — " + it.key(), it.key());
        const int selected = channels->findData(accountId);
        if (selected >= 0) channels->setCurrentIndex(selected);
        layout->addWidget(channels);
        auto connectAccount = new QPushButton("Connect another YouTube account…", &choose);
        layout->addWidget(connectAccount);
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &choose);
        buttons->button(QDialogButtonBox::Ok)->setText("Use account");
        buttons->button(QDialogButtonBox::Ok)->setEnabled(channels->count() > 0);
        layout->addWidget(buttons);
        bool login = false;
        connect(connectAccount, &QPushButton::clicked, &choose, [&]{login = true; choose.accept();});
        connect(buttons, &QDialogButtonBox::accepted, &choose, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &choose, &QDialog::reject);
        if (choose.exec() != QDialog::Accepted) return {};
        if (login) {
            // Upstream Login writes its channel label to the primary profile.
            // Preserve that label while using its normal browser/PKCE flow.
            const bool hadName = config_has_user_value(config, "YouTube", "ChannelName");
            const char *old = config_get_string(config, "YouTube", "ChannelName");
            const std::string oldName = old ? old : "";
            auto connected = std::dynamic_pointer_cast<YoutubeApiWrappers>(YoutubeAuth::Login(this, "YouTube - RTMPS"));
            if (hadName) config_set_string(config, "YouTube", "ChannelName", oldName.c_str());
            else config_remove_value(config, "YouTube", "ChannelName");
            config_save_safe(config, "tmp", nullptr);
            if (!connected) return {};
            api->CherriesLoadCredentials(connected->CherriesCredentials());
            accountId.clear();
        } else if (channels->currentData().toString() == "@primary") {
            api->CherriesLoadCredentials(primary->CherriesCredentials());
            accountId.clear();
        } else {
            accountId = channels->currentData().toString();
            api->CherriesLoadCredentials(accounts.value(accountId).toObject().value("credentials").toObject().toVariantMap());
        }
    } else {
        if (accountId.isEmpty() || !accounts.contains(accountId))
            return fail("Reconnect this destination's YouTube account using Create / select broadcast.");
        api->CherriesLoadCredentials(accounts.value(accountId).toObject().value("credentials").toObject().toVariantMap());
    }

    ChannelDescription channel;
    if (!api->GetChannelDescription(channel))
        return fail("YouTube authorization failed. Reconnect the channel and try again.\n\n" + api->GetLastError());
    if (!accountId.isEmpty() && accountId != channel.id)
        return fail("This authorization belongs to a different YouTube channel. Reconnect the intended channel.");
    accountId = channel.id;
    auto saveAccount = [&] {
        accounts.insert(accountId, QJsonObject{{"title", channel.title},
            {"credentials", QJsonObject::fromVariantMap(api->CherriesCredentials())}});
        const QByteArray json = QJsonDocument(accounts).toJson(QJsonDocument::Compact);
        config_set_string(config, "CherriesYouTube", "Accounts", json.constData());
        return config_save_safe(config, "tmp", nullptr) == CONFIG_SUCCESS;
    };
    if (!saveAccount()) return fail("The YouTube account could not be saved in this OBS profile.");

    if (operation != "configure") {
        const QString id = request.value("broadcast_id").toString();
        json11::Json remote;
        if (id.isEmpty() || !api->FindBroadcast(id, remote) || remote["items"].array_items().empty())
            return fail("The assigned broadcast could not be found.\n\n" + api->GetLastError());
        const auto &broadcast = remote["items"][0];
        if (QString::fromStdString(broadcast["contentDetails"]["boundStreamId"].string_value()) != request.value("stream_id").toString())
            return fail("This broadcast's stream binding changed. Select it again before going live.");
        if (operation == "edit") {
            QDialog edit(this); edit.setWindowTitle("Edit YouTube broadcast — " + request.value("name").toString());
            auto layout = new QFormLayout(&edit);
            auto title = new QLineEdit(QString::fromStdString(broadcast["snippet"]["title"].string_value()), &edit);
            title->setMaxLength(100); layout->addRow("Title", title);
            auto description = new QPlainTextEdit(QString::fromStdString(broadcast["snippet"]["description"].string_value()), &edit);
            layout->addRow("Description", description);
            auto privacy = new QComboBox(&edit); privacy->addItems({"private", "unlisted", "public"});
            privacy->setCurrentText(QString::fromStdString(broadcast["status"]["privacyStatus"].string_value()));
            layout->addRow("Privacy", privacy);
            const QString scheduled = QString::fromStdString(broadcast["snippet"]["scheduledStartTime"].string_value());
            auto time = new QDateTimeEdit(QDateTime::fromString(scheduled, Qt::ISODate).toLocalTime(), &edit);
            time->setCalendarPopup(true); time->setDisplayFormat("yyyy-MM-dd HH:mm");
            const auto state = broadcast["status"]["lifeCycleStatus"].string_value();
            time->setEnabled(state == "created" || state == "ready");
            layout->addRow("Scheduled start (local)", time);
            auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &edit);
            layout->addRow(buttons);
            connect(buttons, &QDialogButtonBox::rejected, &edit, &QDialog::reject);
            connect(buttons, &QDialogButtonBox::accepted, &edit, [&]{
                if (title->text().trimmed().isEmpty() || description->toPlainText().size() > 5000) {
                    QMessageBox::information(&edit, "Broadcast details", "Enter a title and a description of no more than 5,000 characters."); return;
                }
                edit.accept();
            });
            if (edit.exec() != QDialog::Accepted) return {};
            QVariantMap values = {{"title", title->text()}, {"description", description->toPlainText()},
                {"privacy", privacy->currentText()}, {"scheduled_start", time->isEnabled() ? time->dateTime().toUTC().toString(Qt::ISODate) : scheduled}};
            const bool updated = api->CherriesUpdateBroadcast(broadcast, values);
            saveAccount();
            if (!updated) return fail("YouTube could not update the broadcast.\n\n" + api->GetLastError());
            return {{"ok", true}, {"operation", operation}, {"title", title->text()}};
        }
        if (operation == "complete" && QMessageBox::question(this, "End YouTube broadcast",
            "End this destination's YouTube broadcast? Other destinations will continue.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return {};
        const bool ok = operation == "live" ? api->StartBroadcast(id) : api->StopBroadcast(id);
        saveAccount();
        if (!ok) return fail("YouTube could not change the broadcast state. To go live, start this destination's encoder first.\n\n" + api->GetLastError());
        return {{"ok", true}, {"operation", operation}};
    }

    QVariantMap result;
    QString selectedBroadcast = request.value("broadcast_id").toString();
    // A private API instance prevents native chat/broadcast IDs from being
    // overwritten when an additional destination is configured.
    api->SetBroadcastId(selectedBroadcast);
    {
    OBSYoutubeActions dialog(this, api.get(), !selectedBroadcast.isEmpty(), true,
                             request.value("used_broadcasts").toStringList());
    dialog.setWindowTitle("YouTube — " + channel.title + " — " + request.value("name").toString());
    connect(&dialog, &OBSYoutubeActions::ok, &dialog,
        [&](const std::string &broadcast, const std::string &stream, const std::string &key,
            bool autoStart, bool autoStop, bool) {
            result = {{"broadcast_id", QString::fromStdString(broadcast)},
                      {"stream_id", QString::fromStdString(stream)},
                      {"stream_key", QString::fromStdString(key)},
                      {"auto_start", autoStart}, {"auto_stop", autoStop}};
        });
    if (dialog.Valid()) dialog.exec();
    } // Join the native list worker before persisting credentials or more API calls.
    if (!saveAccount()) return fail("The refreshed YouTube authorization could not be saved.");
    if (result.isEmpty()) return {};
    if (request.value("used_broadcasts").toStringList().contains(result.value("broadcast_id").toString()) ||
        request.value("used_streams").toStringList().contains(result.value("stream_id").toString()))
        return fail("That broadcast or ingest stream is already assigned to another destination. Choose or create a separate broadcast for this canvas.");
    json11::Json stream, broadcast;
    if (!api->FindStream(result.value("stream_id").toString(), stream) || stream["items"].array_items().empty() ||
        !api->FindBroadcast(result.value("broadcast_id").toString(), broadcast) || broadcast["items"].array_items().empty())
        return fail("YouTube created or selected the broadcast, but its ingest details could not be retrieved. Select the existing broadcast again; the destination has not been changed.");
    const auto &ingest = stream["items"][0]["cdn"]["ingestionInfo"];
    QString server = QString::fromStdString(ingest["rtmpsIngestionAddress"].string_value());
    if (server.isEmpty()) server = QString::fromStdString(ingest["ingestionAddress"].string_value());
    const QString key = QString::fromStdString(ingest["streamName"].string_value());
    if (server.isEmpty() || key.isEmpty()) return fail("YouTube returned incomplete ingest details. The destination has not been changed.");
    if (request.value("used_keys").toStringList().contains(key))
        return fail("This ingest key is already used by another output. Choose or create a separate broadcast for this destination.");
    result.insert("server", server); result.insert("stream_key", key);
    result.insert("account_id", accountId); result.insert("channel_title", channel.title);
    result.insert("title", QString::fromStdString(broadcast["items"][0]["snippet"]["title"].string_value()));
    result.insert("ok", true);
    saveAccount();
    return result;
#endif
}
