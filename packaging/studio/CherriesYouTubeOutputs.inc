// SPDX-License-Identifier: GPL-2.0-or-later
#include <QDialog>
#include <QDialogButtonBox>
#include <QUrl>
#include <QVariantMap>

static bool CherriesIsYouTube(obs_data_t *settings)
{
    const QUrl url(QString::fromUtf8(obs_data_get_string(settings, "stream_server")));
    const QString host = url.host().toLower();
    return host == "youtube.com" || host.endsWith(".youtube.com");
}

obs_output_t *MultistreamDock::CherriesGetOutput(obs_data_t *settings, bool portrait)
{
    const char *name = obs_data_get_string(settings, "name");
    if (!portrait) {
        for (const auto &entry : outputs)
            if (std::get<std::string>(entry) == name) return obs_output_get_ref(std::get<obs_output_t *>(entry));
        return nullptr;
    }
    calldata_t cd; calldata_init(&cd); calldata_set_string(&cd, "name", name);
    proc_handler_call(obs_get_proc_handler(), "aitum_vertical_get_stream_output", &cd);
    auto output = static_cast<obs_output_t *>(calldata_ptr(&cd, "output")); calldata_free(&cd);
    return output;
}

QVariantMap MultistreamDock::CherriesBindingRequest(obs_data_t *settings)
{
    QVariantMap request;
    request.insert("name", QString::fromUtf8(obs_data_get_string(settings, "name")));
    for (const char *key : {"account_id", "broadcast_id", "stream_id"})
        request.insert(key, QString::fromUtf8(obs_data_get_string(settings, (std::string("cherries_yt_") + key).c_str())));
    QStringList broadcasts, streams, keys;
    auto main = obs_frontend_get_streaming_service();
    auto data = main ? obs_service_get_settings(main) : nullptr;
    if (data) {
        broadcasts.append(QString::fromUtf8(obs_data_get_string(data, "broadcast_id")));
        streams.append(QString::fromUtf8(obs_data_get_string(data, "stream_id")));
        keys.append(QString::fromUtf8(obs_data_get_string(data, "key")));
    }
    obs_data_release(data);
    auto landscape = obs_data_get_array(current_config, "outputs");
    for (auto array : {landscape, vertical_outputs}) {
        for (size_t i = 0; i < obs_data_array_count(array); ++i) {
            auto entry = obs_data_array_item(array, i);
            if (entry != settings) {
                broadcasts.append(QString::fromUtf8(obs_data_get_string(entry, "cherries_yt_broadcast_id")));
                streams.append(QString::fromUtf8(obs_data_get_string(entry, "cherries_yt_stream_id")));
                keys.append(QString::fromUtf8(obs_data_get_string(entry, "stream_key")));
            }
            obs_data_release(entry);
        }
    }
    obs_data_array_release(landscape);
    broadcasts.removeAll(""); streams.removeAll(""); keys.removeAll("");
    request.insert("used_broadcasts", broadcasts); request.insert("used_streams", streams); request.insert("used_keys", keys);
    return request;
}

bool MultistreamDock::CherriesApplyYouTubeBinding(QString name, bool portrait, QVariantMap binding)
{
    auto array = portrait ? vertical_outputs : obs_data_get_array(current_config, "outputs");
    obs_data_t *target = nullptr;
    for (size_t i = 0; i < obs_data_array_count(array); ++i) {
        auto entry = obs_data_array_item(array, i);
        if (QString::fromUtf8(obs_data_get_string(entry, "name")) == name) { target = entry; break; }
        obs_data_release(entry);
    }
    if (!portrait) obs_data_array_release(array);
    if (!target) return false;
    auto output = CherriesGetOutput(target, portrait);
    const bool active = output && obs_output_active(output);
    obs_output_release(output);
    const QUrl endpoint(binding.value("server").toString());
    auto used = CherriesBindingRequest(target);
    bool valid = !active && !cherriesWaiting && binding.value("ok").toBool() &&
        endpoint.isValid() && !endpoint.host().isEmpty() &&
        (endpoint.scheme() == "rtmp" || endpoint.scheme() == "rtmps");
    for (const char *key : {"stream_key", "stream_id", "broadcast_id", "account_id"})
        valid = valid && !binding.value(key).toString().isEmpty();
    valid = valid && !used.value("used_broadcasts").toStringList().contains(binding.value("broadcast_id").toString()) &&
        !used.value("used_streams").toStringList().contains(binding.value("stream_id").toString()) &&
        !used.value("used_keys").toStringList().contains(binding.value("stream_key").toString());
    if (!valid) { obs_data_release(target); return false; }
    obs_data_set_string(target, "stream_server", binding.value("server").toString().toUtf8().constData());
    obs_data_set_string(target, "stream_key", binding.value("stream_key").toString().toUtf8().constData());
    for (const char *key : {"account_id", "broadcast_id", "stream_id", "channel_title", "title"})
        obs_data_set_string(target, (std::string("cherries_yt_") + key).c_str(), binding.value(key).toString().toUtf8().constData());
    obs_data_set_bool(target, "cherries_yt_auto_start", binding.value("auto_start").toBool());
    obs_data_set_bool(target, "cherries_yt_auto_stop", binding.value("auto_stop").toBool());
    if (portrait) {
        calldata_t cd; calldata_init(&cd); calldata_set_ptr(&cd, "outputs", vertical_outputs);
        proc_handler_call(obs_get_proc_handler(), "aitum_vertical_set_stream_settings", &cd);
        calldata_free(&cd);
    } else SaveSettings();
    obs_data_release(target);
    obs_frontend_save();
    return true;
}

bool MultistreamDock::CherriesCanStart(obs_data_t *settings)
{
    if (CherriesIsYouTube(settings) && !*obs_data_get_string(settings, "stream_key")) {
        QMessageBox::information(this, "YouTube broadcast required", "Use Manage Broadcast to create or select a broadcast for " +
            QString::fromUtf8(obs_data_get_string(settings, "name")) + " before starting it.");
        return false;
    }
    return true;
}

void MultistreamDock::CherriesManageYouTube(obs_data_t *settings, bool portrait, const QString &operation)
{
    auto output = CherriesGetOutput(settings, portrait);
    const bool active = output && obs_output_active(output);
    obs_output_release(output);
    if (operation == "configure" && (active || cherriesWaiting)) {
        QMessageBox::information(this, "YouTube destination", "Stop this destination before changing its broadcast.");
        return;
    }
    auto request = CherriesBindingRequest(settings); request.insert("operation", operation);
    if (operation != "configure" && request.value("broadcast_id").toString().isEmpty()) {
        QMessageBox::information(this, "YouTube destination", "Create or select a broadcast for this destination first."); return;
    }
    obs_data_addref(settings);
    QVariantMap result;
    const bool invoked = QMetaObject::invokeMethod(static_cast<QMainWindow *>(obs_frontend_get_main_window()),
        "CherriesYouTubeDestination", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, result), Q_ARG(QVariantMap, request));
    if (!invoked) QMessageBox::warning(this, "YouTube destination", "Update the complete Cherries OBS package to use destination broadcast management.");
    else if (result.value("ok").toBool()) {
        if (operation == "configure") {
            if (!CherriesApplyYouTubeBinding(request.value("name").toString(), portrait, result))
                QMessageBox::warning(this, "YouTube destination", "The destination became busy or its binding conflicts with another output. No new binding was applied.");
        } else if (operation == "edit") {
            obs_data_set_string(settings, "cherries_yt_title", result.value("title").toString().toUtf8().constData());
            if (portrait) {
                calldata_t cd; calldata_init(&cd); calldata_set_ptr(&cd, "outputs", vertical_outputs);
                proc_handler_call(obs_get_proc_handler(), "aitum_vertical_set_stream_settings", &cd); calldata_free(&cd);
            } else SaveSettings();
        } else if (operation == "complete") {
            auto current = CherriesGetOutput(settings, portrait);
            if (current && obs_output_active(current)) obs_output_stop(current);
            obs_output_release(current);
        }
    }
    obs_data_release(settings);
}

void MultistreamDock::CherriesBroadcastMenu()
{
    bool hasYouTube = false;
    auto landscape = obs_data_get_array(current_config, "outputs");
    for (auto array : {landscape, vertical_outputs}) {
        for (size_t i = 0; i < obs_data_array_count(array); ++i) {
            auto entry = obs_data_array_item(array, i);
            hasYouTube = hasYouTube || CherriesIsYouTube(entry);
            obs_data_release(entry);
        }
    }
    auto native = [] {
        auto main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
        if (!QMetaObject::invokeMethod(main, "CherriesManageBroadcast", Qt::QueuedConnection))
            QMessageBox::warning(main, "Manage Broadcast", "The broadcast action is unavailable. Update the complete package.");
    };
    if (!hasYouTube) { obs_data_array_release(landscape); native(); return; }
    QDialog menu(this); menu.setWindowTitle("Manage Broadcasts"); menu.setObjectName("cherriesBroadcastDestinations");
    auto layout = new QVBoxLayout(&menu);
    auto main = new QPushButton("Manage main OBS account", &menu); layout->addWidget(main);
    connect(main, &QPushButton::clicked, &menu, [&]{menu.accept(); native();});
    for (auto array : {landscape, vertical_outputs}) {
        const bool portrait = array == vertical_outputs;
        for (size_t i = 0; i < obs_data_array_count(array); ++i) {
            auto entry = obs_data_array_item(array, i);
            if (!CherriesIsYouTube(entry)) { obs_data_release(entry); continue; }
            auto group = new QGroupBox(QString::fromUtf8(obs_data_get_string(entry, "name")) + (portrait ? " · Portrait" : " · Landscape"), &menu);
            auto controls = new QVBoxLayout(group);
            const QString title = QString::fromUtf8(obs_data_get_string(entry, "cherries_yt_title"));
            controls->addWidget(new QLabel(title.isEmpty() ? "No broadcast assigned through OBS" : title, group));
            for (auto action : {std::make_pair("Create / select broadcast", "configure"), std::make_pair("Edit broadcast details", "edit"), std::make_pair("Go live", "live"), std::make_pair("End broadcast", "complete")}) {
                auto button = new QPushButton(action.first, group); controls->addWidget(button);
                connect(button, &QPushButton::clicked, &menu, [this, entry, portrait, action]{CherriesManageYouTube(entry, portrait, action.second);});
            }
            connect(group, &QObject::destroyed, [entry]{obs_data_release(entry);});
            layout->addWidget(group);
        }
    }
    obs_data_array_release(landscape);
    auto close = new QDialogButtonBox(QDialogButtonBox::Close, &menu); layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, &menu, &QDialog::reject);
    menu.exec();
}
