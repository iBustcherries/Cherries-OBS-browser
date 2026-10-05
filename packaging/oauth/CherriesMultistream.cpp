#include "CherriesMultistream.hpp"
#include "CherriesOAuth.hpp"
#include "CherriesOAuthProtocol.hpp"
#include "CherriesSharedOutput.hpp"
#include "TwitchAuth.hpp"
#include "YoutubeAuth.hpp"
#include <dialogs/OBSYoutubeActions.hpp>
#include <utility/YoutubeApiWrappers.hpp>
#include <utility/BasicOutputHandler.hpp>
#include <docks/YouTubeAppDock.hpp>
#include <QThread>
#include <widgets/OBSBasic.hpp>
#include <widgets/OBSBasicControls.hpp>
#include <browser-panel.hpp>
#include <settings/OBSBasicSettings.hpp>
#include <qt-wrappers.hpp>
#include <obs-frontend-api.h>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTabWidget>
#include <QInputDialog>
#include <QPointer>
#include <QFormLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QVBoxLayout>

extern QCef *cef;

class CherriesMultistream : public QWidget {
	struct Account {
		QString id;
		QString label;
		std::shared_ptr<OAuth> auth;
		bool selected = true;
		int audioTrack = 0; // 0 retains the normal OBS streaming track.
		int vodTrack = 0;   // 0 uses normal live audio for the Twitch archive.
		bool enhanced = false;
		QString streamId;
		bool autoStart = true;
		bool autoStop = true;
		bool ingestReported = false;
		QString server;
		QString key;
		QString status = "Offline";
		bool connected = true;
		QString broadcast;
		OBSOutputAutoRelease output;
		OBSServiceAutoRelease service;
	};
	OBSBasic *main;
	std::vector<std::unique_ptr<Account>> accounts;
	QTableWidget *table;
	QLabel *connections;
	QComboBox *addonChoice;
	QWidget *addons;
	bool internalStart = false;
	QPushButton *addTwitch;
	QPushButton *addYouTube;
	QPushButton *remove;
	QPushButton *setup;
	QLabel *message;
	QTimer monitor;
	QTimer validate;
	QTimer startup;
	bool running = false;
	bool shuttingDown = false;
	bool preparing = false;
	Account *primary = nullptr;
	int defaultAudioTrack = 1;
	CherriesAudioEncoders audioEncoders;
	OBSEncoderAutoRelease audioTemplate;
	OBSServiceAutoRelease previousService;
	std::shared_ptr<Auth> previousAuth;
	bool previousAutoStart = false;
	bool previousAutoStop = false;
	bool previousBroadcastReady = false;
	bool previousBroadcastActive = false;

	QString StorePath() const
	{
		return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		       "/cherries-obs/accounts.json";
	}

	void Save()
	{
		if (shuttingDown)
			return;
		QJsonArray list;
		for (const auto &account : accounts) {
			const auto &auth = account->auth;
			QJsonObject record{{"id", account->id},
					   {"label", account->label},
					   {"service", auth->service()},
					   {"selected", account->selected},
					   {"audio_track", account->audioTrack},
					   {"vod_track", account->vodTrack},
					   {"enhanced", account->enhanced},
					   {"broadcast", account->broadcast},
					   {"stream_id", account->streamId},
					   {"auto_start", account->autoStart},
					   {"auto_stop", account->autoStop},
					   {"token", QString::fromStdString(auth->token)},
					   {"refresh", QString::fromStdString(auth->refresh_token)},
					   {"expiry", QString::number(auth->expire_time)},
					   {"scope", auth->currentScopeVer}};
			if (auto twitch = dynamic_cast<TwitchAuth *>(auth.get())) {
				record["client"] = QString::fromStdString(twitch->clientId);
				record["name"] = QString::fromStdString(twitch->name);
				record["browser_profile"] = QString::fromStdString(twitch->browserProfile);
			} else if (auto youtube = dynamic_cast<YoutubeAuth *>(auth.get())) {
				record["client"] = QString::fromStdString(youtube->googleClient.id);
				record["secret"] = QString::fromStdString(youtube->googleClient.secret);
			}
			list.append(record);
		}
		const QString path = StorePath();
		QDir dir;
		if (!dir.mkpath(QFileInfo(path).absolutePath())) {
			message->setText("Could not create the account storage folder.");
			return;
		}
		QFile::setPermissions(QFileInfo(path).absolutePath(),
				      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
		QSaveFile file(path);
		if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
		    file.write(QJsonDocument(list).toJson()) < 0 || !file.commit())
			message->setText("Could not save accounts. Your connections will not survive a restart.");
	}

	void Load()
	{
		QFile file(StorePath());
		if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
			return;
		for (const auto &value : QJsonDocument::fromJson(file.readAll()).array()) {
			const auto record = value.toObject();
			const auto service = record.value("service").toString();
			if (service != "Twitch" && !IsYouTubeService(service.toStdString()))
				continue;
			auto auth = std::dynamic_pointer_cast<OAuth>(Auth::Create(service.toStdString()));
			if (!auth || record.value("refresh").toString().isEmpty())
				continue;
			auth->token = record.value("token").toString().toStdString();
			auth->refresh_token = record.value("refresh").toString().toStdString();
			auth->expire_time = record.value("expiry").toString().toULongLong();
			auth->currentScopeVer = record.value("scope").toInt();
			if (auto twitch = dynamic_cast<TwitchAuth *>(auth.get())) {
				twitch->clientId = record.value("client").toString().toStdString();
				twitch->name = record.value("name").toString().toStdString();
				twitch->browserProfile = record.value("browser_profile").toString().toStdString();
			} else if (auto youtube = dynamic_cast<YoutubeAuth *>(auth.get())) {
				youtube->googleClient = {record.value("client").toString().toStdString(),
							 record.value("secret").toString().toStdString()};
			}
			auto account = std::make_unique<Account>();
			account->id = record.value("id").toString();
			account->label = record.value("label").toString();
			account->auth = auth;
			account->selected = record.value("selected").toBool();
			account->broadcast = record.value("broadcast").toString();
			account->streamId = record.value("stream_id").toString();
			account->autoStart = record.value("auto_start").toBool(true);
			account->autoStop = record.value("auto_stop").toBool(true);
			auth->firstLoad = false;
			const int track = record.value("audio_track").toInt();
			account->audioTrack = track >= 0 && track <= MAX_AUDIO_MIXES ? track : 0;
			if (dynamic_cast<TwitchAuth *>(auth.get())) {
				const int vod = record.value("vod_track").toInt(-1);
				account->vodTrack = vod >= -1 && vod <= MAX_AUDIO_MIXES ? vod : -1;
				account->enhanced = record.value("enhanced").toBool();
			}
			accounts.push_back(std::move(account));
		}
	}

	void EnsureDocks()
	{
		for (const auto &account : accounts) {
			if (!account->connected)
				continue;
			if (auto twitch = dynamic_cast<TwitchAuth *>(account->auth.get())) {
				if (account->auth == main->auth)
					twitch->LoadUI();
				else
					twitch->LoadAccountUI(account->id, account->label);
			} else if (auto youtube = dynamic_cast<YoutubeAuth *>(account->auth.get())) {
				const bool hadUI = youtube->uiLoaded;
				if (account->auth == main->auth)
					youtube->LoadUI();
				else
					youtube->LoadAccountUI(account->id, account->label);
				if (auto dock = youtube->GetControlDock())
					dock->BindAccount(dynamic_cast<YoutubeApiWrappers *>(youtube),
							  account->id.mid(QString("youtube:").size()));
				if (!hadUI && !account->broadcast.isEmpty())
					youtube->SetChatId(account->broadcast);
			}
		}
	}

	void Update()
	{
		const bool busy = running || preparing || obs_frontend_streaming_active();
		table->setRowCount(int(accounts.size()));
		for (int i = 0; i < int(accounts.size()); ++i) {
			auto &account = *accounts[i];
			if (!table->cellWidget(i, 0)) {
				auto check = new QCheckBox(table);
				check->setChecked(account.selected);
				table->setCellWidget(i, 0, check);
				connect(check, &QCheckBox::toggled, this, [this, ptr = &account](bool selected) {
					ptr->selected = selected;
					Save();
				});
			}
			table->cellWidget(i, 0)->setEnabled(!busy);
			table->setItem(i, 1, new QTableWidgetItem(account.label));
			table->setItem(i, 2, new QTableWidgetItem(account.status));
			if (!table->cellWidget(i, 3)) {
				auto tracks = new QComboBox(table);
				tracks->addItem("OBS default", 0);
				for (int track = 1; track <= MAX_AUDIO_MIXES; ++track)
					tracks->addItem(QString("Track %1").arg(track), track);
				tracks->setCurrentIndex(account.audioTrack);
				tracks->setToolTip(
					"Choose the OBS audio track sent to this account. Assign sources to tracks in Advanced Audio Properties.");
				table->setCellWidget(i, 3, tracks);
				connect(tracks, qOverload<int>(&QComboBox::currentIndexChanged), this,
					[this, ptr = &account](int track) {
						ptr->audioTrack = track;
						Save();
					});
			}
			table->cellWidget(i, 3)->setEnabled(!busy);
			if (dynamic_cast<TwitchAuth *>(account.auth.get())) {
				if (!table->cellWidget(i, 4)) {
					auto vod = new QComboBox(table);
					vod->addItem("OBS default", -1);
					vod->addItem("Disabled", 0);
					for (int track = 1; track <= MAX_AUDIO_MIXES; ++track)
						vod->addItem(QString("Track %1").arg(track), track);
					vod->setCurrentIndex(account.vodTrack + 1);
					vod->setToolTip(
						"Separate audio for Twitch VODs. Assign each source to tracks in Advanced Audio Properties. Disabled uses live audio for the VOD.");
					table->setCellWidget(i, 4, vod);
					connect(vod, qOverload<int>(&QComboBox::currentIndexChanged), this,
						[this, ptr = &account](int track) {
							ptr->vodTrack = track - 1;
							Save();
						});
					auto enhanced = new QCheckBox(table);
					enhanced->setChecked(account.enhanced);
					enhanced->setToolTip(
						"Use Twitch Enhanced Broadcasting for this account. Twitch may request several video encodes. Other destinations reuse a compatible H.264 rendition when available.");
					table->setCellWidget(i, 5, enhanced);
					connect(enhanced, &QCheckBox::toggled, this,
						[this, ptr = &account](bool enabled) {
							ptr->enhanced = enabled;
							Save();
						});
				}
				table->cellWidget(i, 4)->setEnabled(!busy);
				table->cellWidget(i, 5)->setEnabled(!busy);
			} else {
				table->setItem(i, 4, new QTableWidgetItem("—"));
				table->setItem(i, 5, new QTableWidgetItem("—"));
			}
		}
		QStringList states;
		bool twitchConnected = false, youtubeConnected = false;
		for (const auto &a : accounts) {
			if (!a->connected)
				continue;
			if (dynamic_cast<TwitchAuth *>(a->auth.get()))
				twitchConnected = true;
			else
				youtubeConnected = true;
		}
		if (twitchConnected)
			states << "Twitch: <span style='color:#63d471'>Connected</span>";
		if (youtubeConnected)
			states << "YouTube: <span style='color:#63d471'>Connected</span>";
		connections->setText(states.join("<br>"));
		connections->setVisible(!states.isEmpty());
		if (auto controls = main->findChild<OBSBasicControls *>())
			controls->SetPlatformConnections(twitchConnected, youtubeConnected);
		addons->setVisible(twitchConnected);
		addons->setEnabled(!busy);
		addTwitch->setEnabled(!busy);
		addYouTube->setEnabled(!busy);
		remove->setEnabled(!busy);
		setup->setEnabled(!busy);
	}

	void Add(bool twitch)
	{
		preparing = true;
		Update();
		auto auth = twitch ? TwitchAuth::Login(this, "Twitch") : YoutubeAuth::Login(this, "YouTube - RTMPS");
		preparing = false;
		if (!auth) {
			Update();
			return;
		}
		auto account = std::make_unique<Account>();
		account->auth = std::dynamic_pointer_cast<OAuth>(auth);
		if (auto t = dynamic_cast<TwitchAuth *>(auth.get())) {
			account->id = "twitch:" + QString::fromStdString(t->name);
			account->label = "Twitch · " + QString::fromStdString(t->name);
		} else if (auto y = dynamic_cast<YoutubeApiWrappers *>(auth.get())) {
			ChannelDescription channel;
			if (!y->GetChannelDescription(channel)) {
				message->setText("Could not read the YouTube channel. Check API access and quota.");
				Update();
				return;
			}
			account->id = "youtube:" + channel.id;
			account->label = "YouTube · " + channel.title;
		}
		for (auto &existing : accounts) {
			if (existing->id == account->id) {
				// Keep the native OBS connection in sync so ImportNative cannot restore an older session.
				auto connected = std::dynamic_pointer_cast<TwitchAuth>(account->auth);
				auto native = std::dynamic_pointer_cast<TwitchAuth>(main->auth);
				if (connected && native && connected->name == native->name) {
					native->clientId = connected->clientId;
					native->token = connected->token;
					native->refresh_token = connected->refresh_token;
					native->expire_time = connected->expire_time;
					native->browserProfile = connected->browserProfile;
					native->browserCookies = connected->browserCookies;
					native->ReloadBrowserDocks();
					account->auth = native;
					Auth::Save();
				}
				if (existing->auth == main->auth) {
					auto saved = std::dynamic_pointer_cast<YoutubeAuth>(existing->auth);
					auto refreshed = std::dynamic_pointer_cast<YoutubeAuth>(account->auth);
					if (saved && refreshed) {
						saved->googleClient = refreshed->googleClient;
						saved->token = refreshed->token;
						saved->refresh_token = refreshed->refresh_token;
						saved->expire_time = refreshed->expire_time;
						account->auth = saved;
						Auth::Save();
					}
				}
				existing->auth = account->auth;
				existing->connected = true;
				existing->key.clear();
				existing->label = account->label;
				Save();
				Update();
				return;
			}
		}
		accounts.push_back(std::move(account));
		Save();
		Update();
	}

	void Restore()
	{
		startup.stop();
		if (previousService) {
			const bool canceled = preparing && !obs_frontend_streaming_active();
			main->SetService(previousService);
			previousService = nullptr;
			main->auth = previousAuth;
			previousAuth.reset();
			main->autoStartBroadcast = previousAutoStart;
			main->autoStopBroadcast = previousAutoStop;
			main->broadcastReady = previousBroadcastReady;
			main->broadcastActive = previousBroadcastActive;
			main->SetBroadcastFlowEnabled(main->auth && main->auth->broadcastFlow());
			if (canceled) {
				main->StreamingStopped();
				if (main->sysTrayStream) {
					main->sysTrayStream->setText(QTStr("Basic.Main.StartStreaming"));
					main->sysTrayStream->setEnabled(true);
				}
			}
		}
		primary = nullptr;
		audioEncoders.Clear();
		audioTemplate = nullptr;
		running = false;
		preparing = false;
		Save();
		Update();
	}

	void ReportIngestion(Account &account, bool sending)
	{
		if (account.ingestReported == sending)
			return;
		account.ingestReported = sending;
		if (auto youtube = dynamic_cast<YoutubeAuth *>(account.auth.get())) {
			if (auto dock = youtube->GetControlDock()) {
				if (sending)
					dock->IngestionStarted(account.broadcast.toUtf8().constData(),
							       YouTubeAppDock::YTSM_ACCOUNT);
				else
					dock->IngestionStopped(account.broadcast.toUtf8().constData(),
							       YouTubeAppDock::YTSM_ACCOUNT);
			}
		}
	}

	void StopExtra()
	{
		for (auto &account : accounts) {
			if (account->output)
				obs_output_force_stop(account->output);
			ReportIngestion(*account, false);
			account->status = account->key.isEmpty() || dynamic_cast<TwitchAuth *>(account->auth.get())
						  ? "Offline"
						  : "Broadcast ready";
		}
		Update();
	}

	int LiveTrack(const Account &account) const
	{
		return account.audioTrack ? account.audioTrack : defaultAudioTrack;
	}
	int VodTrack(const Account &account) const
	{
		if (!dynamic_cast<TwitchAuth *>(account.auth.get()))
			return 0;
		if (account.vodTrack >= 0)
			return account.vodTrack;
		const char *mode = config_get_string(main->Config(), "Output", "Mode");
		const bool advanced = mode && strcmp(mode, "Advanced") == 0;
		if (!config_get_bool(main->Config(), advanced ? "AdvOut" : "SimpleOutput", "VodTrackEnabled"))
			return 0;
		const int track = advanced ? int(config_get_int(main->Config(), "AdvOut", "VodTrackIndex")) : 2;
		return track >= 1 && track <= MAX_AUDIO_MIXES ? track : 0;
	}

	obs_encoder_t *VodEncoder(const Account &account)
	{
		const int track = VodTrack(account);
		return track > 0 ? audioEncoders.Get(audioTemplate, track) : nullptr;
	}
	bool HasExtraSelected() const
	{
		for (const auto &account : accounts)
			if (account->selected && account.get() != primary)
				return true;
		return false;
	}
	OBSServiceAutoRelease CreateService(const Account &account, bool isPrimary)
	{
		OBSDataAutoRelease settings = obs_data_create();
		const bool twitch = dynamic_cast<TwitchAuth *>(account.auth.get()) != nullptr;
		OBSDataAutoRelease native =
			obs_service_get_settings(previousService ? previousService.Get() : main->GetService());
		const std::string nativeName = obs_data_get_string(native, "service");
		const std::string serviceName = twitch                         ? "Twitch"
						: IsYouTubeService(nativeName) ? nativeName
									       : account.auth->service();
		obs_data_set_string(settings, "service", serviceName.c_str());
		const char *server =
			twitch ? "auto"
			: serviceName == "YouTube - HLS"
				? "https://a.upload.youtube.com/http_upload_hls?cid={stream_key}&copy=0&file=out.m3u8"
			: serviceName == "YouTube - RTMP" ? "rtmp://a.rtmp.youtube.com/live2"
							  : "rtmps://a.rtmps.youtube.com/live2";
		if (nativeName == serviceName && *obs_data_get_string(native, "server")) {
			server = obs_data_get_string(native, "server");
			obs_data_set_bool(settings, "using_custom_server",
					  obs_data_get_bool(native, "using_custom_server"));
			if (twitch)
				obs_data_set_bool(settings, "bwtest", obs_data_get_bool(native, "bwtest"));
		}
		obs_data_set_string(settings, "server", server);
		const auto streamKey =
			twitch ? CherriesOAuthProtocol::TwitchStreamKey(account.key.toStdString(),
									obs_data_get_bool(settings, "bwtest"))
			       : account.key.toStdString();
		obs_data_set_string(settings, "key", streamKey.c_str());
		if (isPrimary) {
			obs_data_set_bool(settings, "cherries_multistream", true);
			obs_data_set_bool(settings, "cherries_enhanced", twitch && account.enhanced);
			obs_data_set_bool(settings, "cherries_require_shared_h264", HasExtraSelected());
			obs_data_set_int(settings, "cherries_audio_track", LiveTrack(account));
			obs_data_set_int(settings, "cherries_vod_track", VodTrack(account));
		}
		return obs_service_create("rtmp_common", account.id.toUtf8().constData(), settings, nullptr);
	}

	void Begin()
	{
		if (running || preparing || obs_frontend_streaming_active())
			return;
		preparing = true;
		Update();
		primary = nullptr;
		const char *mode = config_get_string(main->Config(), "Output", "Mode");
		defaultAudioTrack = mode && strcmp(mode, "Advanced") == 0
					    ? int(config_get_int(main->Config(), "AdvOut", "TrackIndex"))
					    : 1;
		if (defaultAudioTrack < 1 || defaultAudioTrack > MAX_AUDIO_MIXES)
			defaultAudioTrack = 1;
		int enhancedCount = 0;
		for (const auto &account : accounts)
			if (account->selected && account->enhanced)
				++enhancedCount;
		if (enhancedCount > 1) {
			preparing = false;
			Update();
			QMessageBox::information(
				main, "Enhanced Broadcasting",
				"Enable Enhanced Broadcasting on one Twitch account at a time. Other selected accounts can share its H.264 video rendition.");
			return;
		}
		for (auto &account : accounts) {
			account->output = nullptr;
			account->service = nullptr;

			if (!account->selected)
				continue;
			if (auto twitch = dynamic_cast<TwitchAuth *>(account->auth.get())) {
				if (!twitch->GetChannelInfo()) {
					Save();
					message->setText(
						"Reconnect the Twitch account in Settings → Stream before starting.");
					account->connected = false;
					QMessageBox::warning(main, "Twitch connection", message->text());
					preparing = false;
					Update();
					return;
				}
				account->server = "rtmp://live.twitch.tv/app";
				account->key = QString::fromStdString(twitch->key());
			} else {
				if (account->broadcast.isEmpty()) {
					preparing = false;
					Update();
					QMessageBox::information(
						main, "Manage Broadcast",
						"Create or select a YouTube broadcast in Manage Broadcast before starting.");
					Manage();
					return;
				}
				auto youtube = dynamic_cast<YoutubeApiWrappers *>(account->auth.get());
				json11::Json latest;
				if (!youtube || !youtube->FindBroadcast(account->broadcast, latest) ||
				    latest["items"].array_items().empty() ||
				    latest["items"].array_items()[0]["status"]["lifeCycleStatus"].string_value() ==
					    "complete") {
					account->key.clear();
					message->setText(
						"The selected YouTube broadcast is no longer available. Choose it again in Manage Broadcast.");
					preparing = false;
					Update();
					QMessageBox::warning(main, "YouTube broadcast", message->text());
					return;
				}
				const auto item = latest["items"][0];
				account->streamId =
					QString::fromStdString(item["contentDetails"]["boundStreamId"].string_value());
				account->autoStart = item["contentDetails"]["enableAutoStart"].bool_value();
				account->autoStop = item["contentDetails"]["enableAutoStop"].bool_value();
				json11::Json stream;
				if (account->streamId.isEmpty() || !youtube->FindStream(account->streamId, stream)) {
					preparing = false;
					Update();
					QMessageBox::warning(
						main, "YouTube broadcast",
						"Could not load this broadcast's stream. Select it again in Manage Broadcast.");
					return;
				}
				account->key = QString::fromStdString(
					stream["items"][0]["cdn"]["ingestionInfo"]["streamName"].string_value());
				account->server = "rtmps://a.rtmps.youtube.com/live2";
				if (account->key.isEmpty()) {
					preparing = false;
					Update();
					QMessageBox::warning(
						main, "YouTube broadcast",
						"YouTube did not return a stream key. Select the broadcast again.");
					return;
				}
			}

			if (!primary || account->enhanced ||
			    (!primary->enhanced && !dynamic_cast<TwitchAuth *>(primary->auth.get()) &&
			     dynamic_cast<TwitchAuth *>(account->auth.get())))
				primary = account.get();
		}
		Save();
		if (!primary) {
			message->setText("Select at least one account.");
			preparing = false;
			Update();
			return;
		}
		OBSServiceAutoRelease service = CreateService(*primary, true);
		if (!service) {
			preparing = false;
			Update();
			return;
		}
		Auth::Save();
		previousService = obs_service_get_ref(main->GetService());
		previousAuth = main->auth;
		previousAutoStart = main->autoStartBroadcast;
		previousAutoStop = main->autoStopBroadcast;
		previousBroadcastReady = main->broadcastReady;
		previousBroadcastActive = main->broadcastActive;
		main->auth.reset();
		main->autoStartBroadcast = false;
		main->autoStopBroadcast = false;
		main->broadcastReady = false;
		main->broadcastActive = false;
		main->SetBroadcastFlowEnabled(false);
		main->SetService(service);
		primary->status = "Connecting";
		startup.start(primary->enhanced ? 120000 : 45000);
		internalStart = true;
		main->StartStreaming();
		internalStart = false;
	}

	void StartExtra()
	{
		if (!preparing || !primary)
			return;
		startup.stop();
		OBSOutputAutoRelease source = obs_frontend_get_streaming_output();
		obs_encoder_t *video = source ? obs_output_get_video_encoder(source) : nullptr;
		obs_encoder_t *audio = source ? obs_output_get_audio_encoder(source, 0) : nullptr;
		if (HasExtraSelected() && (!video || !audio || !CherriesCanShareOutput(source))) {
			message->setText(
				"Use H.264 video and AAC audio in Settings → Output for shared multistream encoding.");
			main->ForceStopStreaming();
			return;
		}
		running = true;
		preparing = false;
		for (auto &account : accounts) {
			if (!account->selected || account.get() == primary)
				continue;
			account->service = CreateService(*account, false);
			const char *outputType = account->service ? GetStreamOutputType(account->service) : nullptr;
			account->output = outputType ? obs_output_create(outputType, account->id.toUtf8().constData(),
									 nullptr, nullptr)
						     : nullptr;
			if (!account->service || !account->output) {
				account->status = "Could not create output";
				continue;
			}
			// Video always shares the primary encoder. Audio is shared per selected OBS track.
			auto trackEncoder = audioEncoders.Get(audioTemplate, LiveTrack(*account));
			if (!trackEncoder ||
			    !CherriesShareEncoders(account->output, source, trackEncoder, VodEncoder(*account))) {
				account->status = "Incompatible shared encoder";
				continue;
			}
			obs_output_set_service(account->output, account->service);
			obs_output_set_reconnect_settings(account->output, 20, 2);
			account->status = obs_output_start(account->output) ? "Connecting" : "Connection failed";
		}
		message->setText(
			primary->enhanced && main->outputHandler->multitrackVideoActive
				? "Twitch Enhanced Broadcasting is active. Other destinations share a compatible H.264 rendition."
				: "All destinations share OBS's video encoder; audio encoders are shared by track. Sending does not confirm platform live status.");
		Update();
	}

	static void Event(enum obs_frontend_event event, void *data)
	{
		auto self = static_cast<CherriesMultistream *>(data);
		if (event == OBS_FRONTEND_EVENT_STREAMING_STARTED)
			self->StartExtra();
		else if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPING)
			self->StopExtra();
		else if (event == OBS_FRONTEND_EVENT_EXIT) {
			self->StopExtra();
			self->Restore();
			self->shuttingDown = true;
			self->monitor.stop();
			self->validate.stop();
			self->startup.stop();
			// Release account docks and OBS outputs while the main window and libobs still exist.
			self->main->auth.reset();
			self->accounts.clear();
		} else if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPED && (self->running || self->preparing)) {
			self->StopExtra();
			self->Restore();
		} else if (event == OBS_FRONTEND_EVENT_STREAMING_STARTING ||
			   event == OBS_FRONTEND_EVENT_STREAMING_STOPPED)
			self->Update();
		else if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGING && self->preparing) {
			self->StopExtra();
			self->Restore();
		}
	}

public:
	bool ConfigureAudio(obs_output_t *output)
	{
		if (!preparing || !primary)
			return true;
		audioEncoders.Clear();
		audioTemplate = obs_encoder_get_ref(obs_output_get_audio_encoder(output, 0));
		const bool enhancedActive = main->outputHandler->multitrackVideoActive;
		if (enhancedActive) {
			for (size_t i = 0; i < MAX_OUTPUT_AUDIO_ENCODERS; ++i)
				audioEncoders.Seed(obs_output_get_audio_encoder(output, i));
		}
		for (const auto &account : accounts) {
			if (enhancedActive && account.get() == primary)
				continue;
			if (account->selected && (!audioEncoders.Get(audioTemplate, LiveTrack(*account)) ||
						  (VodTrack(*account) > 0 && !VodEncoder(*account)))) {
				message->setText(
					"Could not create the selected AAC audio track. Check Settings → Output.");
				return false;
			}
		}
		if (enhancedActive)
			return true; // Preserve all audio renditions assigned by Twitch's configuration.
		auto encoder = audioEncoders.Get(audioTemplate, LiveTrack(*primary));
		return CherriesSetAudioTracks(output, encoder, VodEncoder(*primary));
	}
	bool HasAccounts() const { return !accounts.empty(); }
	bool HasSelected() const
	{
		for (const auto &a : accounts)
			if (a->selected)
				return true;
		return false;
	}
	bool StartSelected()
	{
		if (internalStart || accounts.empty())
			return false;
		if (!HasSelected()) {
			QMessageBox::information(main, "Stream destinations",
						 "Select at least one destination in Settings → Stream.");
			return true;
		}
		Begin();
		return true;
	}
	void Detach()
	{
		ImportNative();
		EnsureDocks();
		addons->setParent(this);
		qobject_cast<QVBoxLayout *>(layout())->addWidget(addons);
		hide();
		setParent(main);
	}
	void Attach(QWidget *page)
	{
		ImportNative();
		setParent(page);
		auto advanced = page->findChild<QFormLayout *>("serviceAdvancedOptionsLayout");
		if (advanced)
			advanced->addRow(addons);
		if (auto legacy = page->findChild<QComboBox *>("twitchAddonDropdown")) {
			addonChoice->setCurrentIndex(legacy->currentIndex());
			connect(addonChoice, qOverload<int>(&QComboBox::currentIndexChanged), legacy,
				&QComboBox::setCurrentIndex);
		}

		auto layout = qobject_cast<QVBoxLayout *>(page->layout());
		if (layout)
			layout->insertWidget(1, this);
		show();
		Update();
	}
	void ImportNative()
	{
		if (running || preparing)
			return;
		auto auth = std::dynamic_pointer_cast<OAuth>(main->auth);
		if (!auth || auth->refresh_token.empty())
			return;
		auto account = std::make_unique<Account>();
		account->auth = auth;
		if (auto twitch = dynamic_cast<TwitchAuth *>(auth.get())) {
			account->id = "twitch:" + QString::fromStdString(twitch->name);
			account->label = "Twitch · " + QString::fromStdString(twitch->name);
			account->enhanced = config_get_bool(main->Config(), "Stream1", "EnableMultitrackVideo");
		} else if (auto youtube = dynamic_cast<YoutubeApiWrappers *>(auth.get())) {
			for (const auto &a : accounts)
				if (a->auth == auth)
					return;
			ChannelDescription channel;
			if (!youtube->GetChannelDescription(channel))
				return;
			account->id = "youtube:" + channel.id;
			account->label = "YouTube · " + channel.title;
		} else
			return;
		for (auto &a : accounts)
			if (a->id == account->id) {
				if (auto native = std::dynamic_pointer_cast<TwitchAuth>(auth)) {
					auto saved = std::dynamic_pointer_cast<TwitchAuth>(a->auth);
					if (saved && native->browserProfile.empty()) {
						native->browserProfile = saved->browserProfile;
						native->browserCookies = saved->browserCookies;
					}
				}
				a->auth = auth;
				a->connected = true;
				Save();
				Update();
				return;
			}
		accounts.push_back(std::move(account));
		Save();
		Update();
	}
	void Manage(int tab = -1);
	explicit CherriesMultistream(OBSBasic *obsMain) : QWidget(obsMain), main(obsMain)
	{
		auto layout = new QVBoxLayout(this);
		connections = new QLabel(this);
		connections->setTextFormat(Qt::RichText);
		layout->addWidget(connections);
		message = new QLabel(
			"Select destinations. Start Streaming in Controls starts all selected accounts. Account changes are saved immediately.",
			this);
		message->setWordWrap(true);
		layout->addWidget(message);
		table = new QTableWidget(0, 6, this);
		table->setHorizontalHeaderLabels(
			{"Use", "Account", "Connection", "Live audio", "Twitch VOD", "Enhanced"});
		table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		for (int column : {0, 2, 3, 4, 5})
			table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
		table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		table->setSelectionBehavior(QAbstractItemView::SelectRows);
		table->setMaximumHeight(150);
		layout->addWidget(table);
		auto row = new QHBoxLayout;
		addTwitch = new QPushButton("Add Twitch", this);
		addYouTube = new QPushButton("Add YouTube", this);
		remove = new QPushButton("Remove account", this);
		row->addWidget(addTwitch);
		row->addWidget(addYouTube);
		row->addWidget(remove);
		layout->addLayout(row);
		setup = new QPushButton("Application setup…", this);
		layout->addWidget(setup);
		connect(setup, &QPushButton::clicked, this, [this]() {
			QMessageBox chooser(this);
			chooser.setWindowTitle("Application setup");
			chooser.setText(
				"Choose the application credentials for future account connections. Saved accounts keep their existing credentials.");
			auto twitch = chooser.addButton("Twitch Public Client ID", QMessageBox::ActionRole);
			auto youtube = chooser.addButton("YouTube Desktop OAuth JSON", QMessageBox::ActionRole);
			chooser.addButton(QMessageBox::Cancel);
			chooser.exec();
			if (chooser.clickedButton() == twitch)
				CherriesTwitchClient(this, true, true);
			else if (chooser.clickedButton() == youtube)
				CherriesYouTubeClient(this, true, true);
		});
		connect(addTwitch, &QPushButton::clicked, this, [this]() { Add(true); });
		connect(addYouTube, &QPushButton::clicked, this, [this]() { Add(false); });
		connect(remove, &QPushButton::clicked, this, [this]() {
			const int index = table->currentRow();
			if (index >= 0 && index < int(accounts.size()) && !running && !preparing) {
				table->setRowCount(0);
				if (auto twitch = std::dynamic_pointer_cast<TwitchAuth>(accounts[index]->auth)) {
					if (auto cookies = twitch->GetBrowserCookies()) {
						cookies->DeleteCookies("", "");
						cookies->FlushStore();
					}
				} else if (auto youtube =
						   std::dynamic_pointer_cast<YoutubeAuth>(accounts[index]->auth)) {
					if (youtube->browserCookies) {
						youtube->browserCookies->DeleteCookies("", "");
						youtube->browserCookies->FlushStore();
					}
				}
				if (auto settings = qobject_cast<OBSBasicSettings *>(window())) {
					auto native = std::dynamic_pointer_cast<OAuth>(settings->auth);
					bool same = native == accounts[index]->auth;
					if (auto t = dynamic_cast<TwitchAuth *>(native.get()))
						same = accounts[index]->id ==
						       "twitch:" + QString::fromStdString(t->name);
					if (same) {
						settings->auth.reset();
						main->auth.reset();
						main->SetBroadcastFlowEnabled(false);
						Auth::Save();
						settings->ui->disconnectAccount->hide();
						settings->ui->connectedAccountLabel->hide();
						settings->ui->connectedAccountText->hide();
						settings->ui->key->clear();
					}
				}
				accounts.erase(accounts.begin() + index);
				Save();
				Update();
			}
		});
		addons = new QWidget(this);
		auto addonLayout = new QHBoxLayout(addons);
		addonLayout->setContentsMargins(0, 0, 0, 0);
		addonLayout->addWidget(new QLabel("Twitch Chat Add-Ons", addons));
		addonChoice = new QComboBox(addons);
		addonChoice->addItems({"None", "BetterTTV", "FrankerFaceZ", "BetterTTV and FrankerFaceZ"});
		addonChoice->setCurrentIndex(config_get_int(main->Config(), "Twitch", "AddonChoice"));
		addonLayout->addWidget(addonChoice, 1);
		layout->addWidget(addons);
		connect(addonChoice, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int choice) {
			config_set_int(main->Config(), "Twitch", "AddonChoice", choice);
			config_save_safe(main->Config(), "tmp", nullptr);
			CherriesRefreshTwitchAddons();
		});
		connect(main, &OBSBasic::StreamingStopped, this, [this]() {
			if (preparing && previousService && !obs_frontend_streaming_active()) {
				preparing = false;
				Restore();
			}
		});
		startup.setSingleShot(true);
		connect(&startup, &QTimer::timeout, this, [this]() {
			if (!obs_frontend_streaming_active()) {
				message->setText(
					"OBS could not start the primary stream. Check the OBS log and output settings.");
				Restore();
			}
		});
		monitor.setInterval(1000);
		connect(&monitor, &QTimer::timeout, this, [this]() {
			if (!running)
				return;
			OBSOutputAutoRelease source = obs_frontend_get_streaming_output();
			for (auto &account : accounts) {
				obs_output_t *output = account.get() == primary ? source.Get() : account->output.Get();
				if (!account->selected || !output)
					continue;
				account->status = obs_output_reconnecting(output)          ? "Reconnecting"
						  : !obs_output_active(output)             ? "Disconnected"
						  : obs_output_get_total_bytes(output) > 0 ? "Sending"
											   : "Connecting";
				ReportIngestion(*account, account->status == "Sending");
			}
			Update();
		});
		monitor.start();
		validate.setInterval(60 * 60 * 1000);
		connect(&validate, &QTimer::timeout, this, [this]() {
			if (preparing)
				return;
			for (auto &account : accounts) {
				if (auto twitch = dynamic_cast<TwitchAuth *>(account->auth.get())) {
					if (!CherriesTwitchEnsureToken(twitch->clientId, twitch->token,
								       twitch->refresh_token, twitch->expire_time))
						message->setText(
							"A Twitch session could not be validated. Reconnect it before the next stream.");
				}
			}
			Save();
		});
		validate.start();
		Load();
		ImportNative();
		EnsureDocks();
		Update();
		obs_frontend_add_event_callback(Event, this);
		hide();
	}

	~CherriesMultistream() override
	{
		if (!shuttingDown)
			obs_frontend_remove_event_callback(Event, this);
		for (auto &account : accounts)
			if (account->output)
				obs_output_force_stop(account->output);
		Save();
	}
};

static QPointer<CherriesMultistream> manager;

void CherriesInstallMultistream(OBSBasic *main)
{
	manager = new CherriesMultistream(main);
}
void CherriesAttachStreamSettings(QWidget *page)
{
	if (manager)
		manager->Attach(page);
}
void CherriesDetachStreamSettings()
{
	if (manager)
		manager->Detach();
}
bool CherriesStartSelectedStreams()
{
	return manager && manager->StartSelected();
}
bool CherriesHasSelectedStreams()
{
	return manager && manager->HasAccounts();
}
void CherriesManageBroadcast(int tab)
{
	if (manager)
		manager->Manage(tab);
}
bool CherriesConfigureAudio(obs_output_t *output)
{
	return !manager || manager->ConfigureAudio(output);
}

void CherriesMultistream::Manage(int tab)
{
	if (preparing)
		return;
	ImportNative();
	EnsureDocks();
	Account *twitch = nullptr, *youtube = nullptr;
	auto choose = [&](bool wantTwitch) -> Account * {
		QStringList names;
		std::vector<Account *> matches;
		for (auto &a : accounts) {
			if (!a->connected || (dynamic_cast<TwitchAuth *>(a->auth.get()) != nullptr) != wantTwitch)
				continue;
			names << a->label;
			matches.push_back(a.get());
		}
		if (matches.empty())
			return nullptr;
		if (matches.size() == 1)
			return matches[0];
		if (tab >= 0 && wantTwitch != (tab == 0))
			return matches[0];
		bool ok = false;
		QString choice = QInputDialog::getItem(main, "Manage Broadcast",
						       wantTwitch ? "Twitch account" : "YouTube account", names, 0,
						       false, &ok);
		return ok ? matches[names.indexOf(choice)] : nullptr;
	};
	twitch = choose(true);
	youtube = choose(false);
	QDialog window(main);
	window.setWindowTitle("Manage Broadcast");
	window.resize(950, 800);
	auto layout = new QVBoxLayout(&window);
	auto twitchPage = new QWidget(&window);
	auto twitchLayout = new QVBoxLayout(twitchPage);
	twitchLayout->setContentsMargins(0, 0, 0, 0);
	if (twitch && cef) {
		auto auth = std::dynamic_pointer_cast<TwitchAuth>(twitch->auth);
		auto cookies = auth->GetBrowserCookies();
		auto browser = cookies ? cef->create_widget(twitchPage,
							    "https://dashboard.twitch.tv/popout/u/" + auth->name +
								    "/stream-manager/edit-stream-info",
							    cookies)
				       : nullptr;
		if (browser) {
			browser->allowAllPopups(true);
			twitchLayout->addWidget(browser, 1);
			connect(&window, &QDialog::finished, &window, [browser, auth, cookies]() {
				browser->closeBrowser();
				cookies->FlushStore();
			});
		} else {
			twitchLayout->addWidget(new QLabel(
				"Could not open the Twitch browser session. Restart OBS and try again.", twitchPage));
		}
	} else {
		twitchLayout->addWidget(new QLabel(
			"Connect a Twitch account in Settings → Stream to edit its stream info.", twitchPage));
	}
	if (youtube) {
		OBSYoutubeActions editor(&window, youtube->auth.get(), false);
		editor.setWindowFlags(Qt::Widget);
		editor.SetCombinedPage(twitchPage);
		editor.findChild<QTabWidget *>("tabWidget")->setCurrentIndex(tab > 0 ? tab : 0);
		layout->addWidget(&editor, 1);
		connect(&editor, &OBSYoutubeActions::rejected, &window, &QDialog::reject);
		auto actions = new QWidget(&window);
		auto actionLayout = new QHBoxLayout(actions);
		actionLayout->setContentsMargins(0, 0, 0, 0);
		auto status = new QLabel(actions);
		status->setTextFormat(Qt::PlainText);
		status->setWordWrap(true);
		auto refresh = new QPushButton("Refresh status", actions);
		auto start = new QPushButton("Start YouTube broadcast", actions);
		auto end = new QPushButton("End YouTube broadcast", actions);
		actionLayout->addWidget(status, 1);
		actionLayout->addWidget(refresh);
		actionLayout->addWidget(start);
		actionLayout->addWidget(end);
		layout->addWidget(actions);
		auto api = std::dynamic_pointer_cast<YoutubeApiWrappers>(youtube->auth);
		bool requestRunning = false;
		auto updateActions = [&]() {
			editor.SetBroadcastLocked(running && youtube->selected);
			actions->setVisible(editor.findChild<QTabWidget *>("tabWidget")->currentIndex() > 0 &&
					    !youtube->broadcast.isEmpty());
			bool available = !requestRunning && !editor.IsLoading();
			refresh->setEnabled(available);
			start->setEnabled(available && running && youtube->selected);
			end->setEnabled(available && !youtube->broadcast.isEmpty());
		};
		auto request = [&](int action) {
			if (requestRunning || editor.IsLoading() || youtube->broadcast.isEmpty())
				return;
			if (action == 2) {
				const QString text =
					youtube == primary && HasExtraSelected()
						? "End this YouTube broadcast? Because it owns the primary output, OBS will also stop the other selected streams."
						: "End this YouTube broadcast? An ended broadcast cannot be resumed.";
				if (QMessageBox::question(&window, "End YouTube broadcast", text,
							  QMessageBox::Yes | QMessageBox::No,
							  QMessageBox::No) != QMessageBox::Yes)
					return;
			}
			requestRunning = true;
			updateActions();
			QMessageBox waiting(&window);
			waiting.setWindowTitle("YouTube broadcast");
			waiting.setText("Contacting YouTube…");
			waiting.setStandardButtons(QMessageBox::NoButton);
			waiting.setWindowFlags(waiting.windowFlags() & ~Qt::WindowCloseButtonHint);
			bool success = false;
			QString result;
			QScopedPointer<QThread> worker(CreateQThread([&]() {
				json11::Json response;
				if (action == 1) {
					if (api->FindStream(youtube->streamId, response) &&
					    response["items"][0]["status"]["streamStatus"].string_value() == "active")
						success = api->StartBroadcast(youtube->broadcast);
					else
						result =
							"YouTube is not receiving the stream yet. Start Streaming in Controls, then try again.";
				} else if (action == 2) {
					success = api->StopBroadcast(youtube->broadcast);
				} else {
					success = api->FindBroadcast(youtube->broadcast, response);
					if (success)
						result = "YouTube: " +
							 QString::fromStdString(
								 response["items"][0]["status"]["lifeCycleStatus"]
									 .string_value());
				}
				if (!success && result.isEmpty()) {
					result = api->GetLastError();
					if (result.isEmpty())
						result = "YouTube could not complete the request. Try again.";
				}
				QMetaObject::invokeMethod(&waiting, &QMessageBox::accept, Qt::QueuedConnection);
			}));
			worker->start();
			waiting.exec();
			worker->wait();
			if (success && action == 1)
				result = "YouTube broadcast start requested. Refresh status to confirm it is live.";
			if (success && action == 2) {
				result = "YouTube broadcast ended.";
				if (youtube == primary && running)
					main->StopStreaming();
				else if (youtube->output)
					obs_output_force_stop(youtube->output);
				ReportIngestion(*youtube, false);
				youtube->key.clear();
				youtube->broadcast.clear();
				youtube->status = "Broadcast ended";
			}
			status->setText(result);
			requestRunning = false;
			Save();
			updateActions();
		};
		connect(refresh, &QPushButton::clicked, &window, [&]() { request(0); });
		connect(start, &QPushButton::clicked, &window, [&]() { request(1); });
		connect(end, &QPushButton::clicked, &window, [&]() { request(2); });
		QTimer stateTimer;
		stateTimer.setInterval(500);
		connect(&stateTimer, &QTimer::timeout, &window, updateActions);
		stateTimer.start();
		connect(&editor, &OBSYoutubeActions::ok, &window,
			[&, youtube](const std::string &broadcast, const std::string &stream, const std::string &key,
				     bool autostart, bool autostop, bool) {
				youtube->broadcast = QString::fromStdString(broadcast);
				youtube->streamId = QString::fromStdString(stream);
				youtube->autoStart = autostart;
				youtube->autoStop = autostop;
				youtube->server = "rtmps://a.rtmps.youtube.com/live2";
				youtube->key = QString::fromStdString(key);
				youtube->status = "Broadcast ready";
				status->setText(
					autostart
						? "Starts automatically when YouTube receives your stream."
						: "Start Streaming, then use Start YouTube broadcast when ready to go live.");
				message->setText(
					"YouTube broadcast ready. Use Start Streaming in Controls when you are ready.");
				Save();
				Update();
				updateActions();
			});
		status->setText(youtube->autoStart ? "Automatic broadcast start is enabled."
						   : "Manual broadcast start is enabled.");
		updateActions();
		window.exec();
	} else {
		auto tabs = new QTabWidget(&window);
		tabs->addTab(twitchPage, "Twitch Stream Info");
		for (const QString &name :
		     {QString("Create New YouTube Stream"), QString("Select Existing YouTube Stream")}) {
			auto page = new QWidget(tabs);
			auto body = new QVBoxLayout(page);
			body->addWidget(new QLabel(
				"Connect a YouTube account in Settings → Stream to manage broadcasts.", page));
			tabs->addTab(page, name);
		}
		tabs->setCurrentIndex(tab > 0 ? tab : 0);
		layout->addWidget(tabs);
		auto buttons = new QDialogButtonBox(QDialogButtonBox::Close, &window);
		connect(buttons, &QDialogButtonBox::rejected, &window, &QDialog::reject);
		layout->addWidget(buttons);
		window.exec();
	}
	Save();
}
