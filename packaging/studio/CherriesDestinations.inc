// Cherries destination controls. Upstream Aitum output engines remain authoritative.
void MultistreamDock::CherriesCard(QGroupBox *card, obs_data_t *settings, bool portrait, QPushButton *start)
{
    auto layout = static_cast<QVBoxLayout *>(card->layout());
    auto selected = new QCheckBox(card);
    selected->setObjectName("cherriesSelected");
    const QString key = QString(portrait ? "Portrait/" : "Landscape/") +
        (settings ? QString::fromUtf8(obs_data_get_string(settings, "name")) : "OBS Account");
    auto config = obs_frontend_get_profile_config();
    config_set_default_bool(config, "CherriesDestinations", key.toUtf8().constData(), true);
    selected->setChecked(config_get_bool(config, "CherriesDestinations", key.toUtf8().constData()));
    if (!settings) cherriesMainSelected = selected;
    connect(selected, &QCheckBox::toggled, this, [key](bool checked) {
        auto config = obs_frontend_get_profile_config();
        config_set_bool(config, "CherriesDestinations", key.toUtf8().constData(), checked);
        config_save_safe(config, "tmp", nullptr);
    });
    selected->setToolTip("Include in Start Selected");
    selected->setAccessibleName("Include destination in Start Selected");
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(3);
    auto titleRow = static_cast<QHBoxLayout *>(layout->itemAt(0)->layout());
    titleRow->insertWidget(0, selected);
    start->setMinimumWidth(78);
    start->setText("Start");
    auto detail = new QLabel(card);
    detail->setWordWrap(true);
    layout->addWidget(detail);
    auto status = new QLabel("Idle", card);
    status->setObjectName("cherriesOutputStatus");
    titleRow->insertWidget(titleRow->count() - 1, status);
    auto expand = new QPushButton("Details", card);
    expand->setObjectName("cherriesExpandDestination");
    expand->setCheckable(true);
    titleRow->addWidget(expand);
    auto expanded = new QWidget(card);
    expanded->setObjectName("cherriesDestinationDetails");
    auto expandedLayout = new QVBoxLayout(expanded);
    expandedLayout->setContentsMargins(0, 4, 0, 0);
    auto encoder = new QLabel(expanded);
    encoder->setWordWrap(true);
    expandedLayout->addWidget(encoder);
    if (settings) {
        auto broadcast = new QPushButton("Edit broadcast", expanded);
        expandedLayout->addWidget(broadcast);
        broadcast->setVisible(QString::fromUtf8(obs_data_get_string(settings, "stream_server")).contains("youtube.com"));
        connect(broadcast, &QPushButton::clicked, this, [this, settings, portrait] {
            CherriesManageYouTube(settings, portrait, "configure");
        });
    }
    auto options = new QPushButton("Output settings", expanded);
    expandedLayout->addWidget(options);
    connect(options, &QPushButton::clicked, this, [this] {
        auto button = findChild<QPushButton *>("cherriesOutputSettings");
        if (button) button->click();
    });
    layout->addWidget(expanded);
    expanded->hide();
    connect(expand, &QPushButton::toggled, expanded, &QWidget::setVisible);
    card->setProperty("cherriesPortrait", portrait);
    card->setProperty("cherriesShared", settings && !portrait &&
        (!obs_data_get_bool(settings, "advanced") || !*obs_data_get_string(settings, "video_encoder") ||
         !*obs_data_get_string(settings, "audio_encoder")));
    if (settings) {
        obs_data_addref(settings);
        connect(card, &QObject::destroyed, this, [settings] { obs_data_release(settings); });
    }
    auto timer = new QTimer(card);
    connect(timer, &QTimer::timeout, card, [this, card, settings, portrait, detail, encoder, status, start, selected, key] {
        if (exiting) return;
        auto config = obs_frontend_get_profile_config();
        config_set_default_bool(config,"CherriesDestinations",key.toUtf8().constData(),true);
        selected->blockSignals(true);
        selected->setChecked(config_get_bool(config,"CherriesDestinations",key.toUtf8().constData()));
        selected->blockSignals(false);
        obs_output_t *output = nullptr;
        bool release = false;
        if (!settings) {
            output = obs_frontend_get_streaming_output(); release = true;
            auto service = obs_frontend_get_streaming_service();
            obs_data_t *s = service ? obs_service_get_settings(service) : nullptr;
            detail->setText(QString("Landscape · %1")
                .arg(s ? QString::fromUtf8(obs_data_get_string(s, "service")) : "Configure an account"));
            encoder->setText("Uses OBS stream settings");
            obs_data_release(s);
        } else {
            const char *name = obs_data_get_string(settings, "name");
            if (portrait) {
                calldata_t cd; calldata_init(&cd); calldata_set_string(&cd, "name", name);
                proc_handler_call(obs_get_proc_handler(), "aitum_vertical_get_stream_output", &cd);
                output = static_cast<obs_output_t *>(calldata_ptr(&cd, "output")); release = true;
                calldata_free(&cd);
            } else {
                for (const auto &entry : outputs)
                    if (std::get<std::string>(entry) == name) output = std::get<obs_output_t *>(entry);
            }
            const bool customAudio = obs_data_get_bool(settings, "advanced") && *obs_data_get_string(settings, "audio_encoder");
            const QString audio = customAudio ? QString("Audio track %1").arg(obs_data_get_int(settings, "audio_track") + 1) : "Canvas audio settings";
            detail->setText(QString("%1 · %2").arg(portrait ? "Portrait" : "Landscape", audio));
            encoder->setText(QString("%1\n%2").arg(
                portrait ? "Portrait encoder" : card->property("cherriesShared").toBool() ? "Shares OBS encoder settings" : "Dedicated encoder",
                QString::fromUtf8(obs_data_get_string(settings, "cherries_yt_title"))));
        }
        const bool active = output && obs_output_active(output);
        card->setProperty("cherriesActive", active);
        status->setText(active ? (obs_output_reconnecting(output) ? "Reconnecting…" : "Live") : "Idle");
        const char *error = output ? obs_output_get_last_error(output) : nullptr;
        if (!active && error && *error) { status->setText("Output error"); status->setToolTip(QString::fromUtf8(error)); }
        start->setText(active ? "Stop" : "Start");
        if (release) obs_output_release(output);
    });
    timer->start(500);
}

void MultistreamDock::CherriesStartExtras()
{
    for (auto layout : {mainCanvasOutputLayout, verticalCanvasOutputLayout}) {
        for (int i = layout == mainCanvasOutputLayout ? 1 : 0; i < layout->count(); ++i) {
            auto card = layout->itemAt(i)->widget();
            if (!card) continue;
            auto selected = card->findChild<QCheckBox *>("cherriesSelected");
            auto button = card->findChild<QPushButton *>("canvasStream");
            if (selected && selected->isChecked() && button && !button->isChecked()) button->click();
        }
    }
}

void MultistreamDock::CherriesStartSelected()
{
    if (cherriesWaiting) return;
    auto landscape = obs_data_get_array(current_config, "outputs");
    bool ready = true;
    for (auto pair : {std::make_pair(mainCanvasOutputLayout, landscape), std::make_pair(verticalCanvasOutputLayout, vertical_outputs)}) {
        for (size_t i = 0; ready && i < obs_data_array_count(pair.second); ++i) {
            auto entry = obs_data_array_item(pair.second, i);
            for (int j = 0; j < pair.first->count(); ++j) {
                auto card = pair.first->itemAt(j)->widget();
                if (!card || card->objectName() != QString::fromUtf8(obs_data_get_string(entry, "name"))) continue;
                auto selected = card->findChild<QCheckBox *>("cherriesSelected");
                if (selected && selected->isChecked()) ready = CherriesCanStart(entry);
            }
            obs_data_release(entry);
        }
    }
    obs_data_array_release(landscape);
    if (!ready) return;
    bool needsMain = false;
    for (int i = 1; i < mainCanvasOutputLayout->count(); ++i) {
        auto card = mainCanvasOutputLayout->itemAt(i)->widget();
        auto selected = card ? card->findChild<QCheckBox *>("cherriesSelected") : nullptr;
        if (selected && selected->isChecked() && card->property("cherriesShared").toBool()) needsMain = true;
    }
    if (needsMain && !obs_frontend_streaming_active() && (!cherriesMainSelected || !cherriesMainSelected->isChecked())) {
        QMessageBox::information(this, "Shared encoder", "Selected destinations use the main OBS encoder. Include OBS Account, or choose dedicated video and audio encoders in Destination Settings.");
        return;
    }
    if (cherriesMainSelected && cherriesMainSelected->isChecked() && !obs_frontend_streaming_active()) {
        cherriesWaiting = true;
        mainStreamButton->click();
        QTimer::singleShot(30000, this, [this] {
            if (cherriesWaiting) {
                cherriesWaiting = false;
                QMessageBox::information(this, "Stream not started", "The main OBS stream has not started. Check its broadcast setup and connection, then try again.");
            }
        });
    } else CherriesStartExtras();
}

void MultistreamDock::CherriesHeader()
{
    auto header = new QWidget(this);
    auto layout = new QHBoxLayout(header);
    layout->setContentsMargins(4, 4, 4, 8);
    auto manage = new QPushButton("Manage Broadcast", header);
    manage->setObjectName("cherriesManageBroadcast");
    connect(manage, &QPushButton::clicked, this, &MultistreamDock::CherriesBroadcastMenu);
    layout->addWidget(manage);
    auto start = new QPushButton("Start Selected", header);
    start->setObjectName("cherriesStartSelected");
    start->setStyleSheet("QPushButton { background: #bd2340; color: white; padding: 9px; font-weight: bold; }");
    connect(start, &QPushButton::clicked, this, &MultistreamDock::CherriesStartSelected);
    layout->addWidget(start);
    auto stop = new QPushButton("Stop All Destinations", header);
    connect(stop, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, "Stop all destinations", "Stop every active stream?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        cherriesWaiting = false;
        for (auto layout : {mainCanvasOutputLayout, verticalCanvasOutputLayout}) {
            for (int i = layout == mainCanvasOutputLayout ? 1 : 0; i < layout->count(); ++i) {
                auto card = layout->itemAt(i)->widget();
                auto button = card ? card->findChild<QPushButton *>("canvasStream") : nullptr;
                if (button && button->isChecked()) button->click();
            }
        }
        if (obs_frontend_streaming_active()) obs_frontend_streaming_stop();
    });
    stop->setText("Stop All");
    stop->setObjectName("cherriesStopAll");
    auto footer = qobject_cast<QHBoxLayout *>(mainLayout->itemAt(mainLayout->count() - 1)->layout());
    if (footer) footer->addWidget(stop); else layout->addWidget(stop);
    mainLayout->insertWidget(0, header);
    setMinimumWidth(360);
}
