#include "CherriesMultistream.hpp"
#include "CherriesOAuth.hpp"
#include "CherriesSharedOutput.hpp"
#include "TwitchAuth.hpp"
#include "YoutubeAuth.hpp"
#include <dialogs/OBSYoutubeActions.hpp>
#include <utility/YoutubeApiWrappers.hpp>
#include <widgets/OBSBasic.hpp>
#include <qt-wrappers.hpp>
#include <obs-frontend-api.h>
#include <browser-panel-dock.hpp>
#include <QCheckBox>
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

class CherriesMultistream : public QWidget {
	struct Account {
		QString id;
		QString label;
		std::shared_ptr<OAuth> auth;
		bool selected = true;
		QString server;
		QString key;
		QString status = "Offline";
		OBSOutputAutoRelease output;
		OBSServiceAutoRelease service;
	};
	OBSBasic *main;
	std::vector<std::unique_ptr<Account>> accounts;
	QTableWidget *table;
	QPushButton *start;
	QPushButton *stop;
	QPushButton *addTwitch;
	QPushButton *addYouTube;
	QPushButton *remove;
	QPushButton *setup;
	QLabel *message;
	QTimer monitor;
	QTimer validate;
	QTimer startup;
	bool running = false;
	bool preparing = false;
	Account *primary = nullptr;
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
		QJsonArray list;
		for (const auto &account : accounts) {
			const auto &auth = account->auth;
			QJsonObject record{{"id", account->id},
					   {"label", account->label},
					   {"service", auth->service()},
					   {"selected", account->selected},
					   {"token", QString::fromStdString(auth->token)},
					   {"refresh", QString::fromStdString(auth->refresh_token)},
					   {"expiry", QString::number(auth->expire_time)},
					   {"scope", auth->currentScopeVer}};
			if (auto twitch = dynamic_cast<TwitchAuth *>(auth.get())) {
				record["client"] = QString::fromStdString(twitch->clientId);
				record["name"] = QString::fromStdString(twitch->name);
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
			if (service != "Twitch" && service != "YouTube - RTMPS")
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
			} else if (auto youtube = dynamic_cast<YoutubeAuth *>(auth.get())) {
				youtube->googleClient = {record.value("client").toString().toStdString(),
							 record.value("secret").toString().toStdString()};
			}
			auto account = std::make_unique<Account>();
			account->id = record.value("id").toString();
			account->label = record.value("label").toString();
			account->auth = auth;
			account->selected = record.value("selected").toBool();
			accounts.push_back(std::move(account));
		}
	}

	void Update()
	{
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
			table->cellWidget(i, 0)->setEnabled(!running && !preparing);
			table->setItem(i, 1, new QTableWidgetItem(account.label));
			table->setItem(i, 2, new QTableWidgetItem(account.status));
		}
		const bool busy = running || preparing || obs_frontend_streaming_active();
		start->setEnabled(!busy && !accounts.empty());
		stop->setEnabled(running || bool(previousService));
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
				existing->auth = account->auth;
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
		running = false;
		preparing = false;
		Save();
		Update();
	}

	void StopExtra()
	{
		for (auto &account : accounts) {
			if (account->output)
				obs_output_force_stop(account->output);
			account->status = "Offline";
		}
		Update();
	}

	void Begin()
	{
		if (running || preparing || obs_frontend_streaming_active())
			return;
		preparing = true;
		Update();
		primary = nullptr;
		for (auto &account : accounts) {
			account->output = nullptr;
			account->service = nullptr;
			account->server.clear();
			account->key.clear();
			if (!account->selected)
				continue;
			if (auto twitch = dynamic_cast<TwitchAuth *>(account->auth.get())) {
				if (!twitch->GetChannelInfo()) {
					Save();
					message->setText("Reconnect the Twitch account before starting.");
					preparing = false;
					Update();
					return;
				}
				account->server = "rtmp://live.twitch.tv/app";
				account->key = QString::fromStdString(twitch->key());
			} else {
				OBSYoutubeActions dialog(this, account->auth.get(), false);
				bool ready = false;
				connect(&dialog, &OBSYoutubeActions::ok, &dialog,
					[&](const std::string &, const std::string &, const std::string &key,
					    bool autostart, bool, bool) {
						if (!autostart) {
							QMessageBox::warning(
								this, "YouTube multistream",
								"Choose Stream now or a broadcast with automatic start enabled.");
							return;
						}
						account->server = "rtmps://a.rtmps.youtube.com/live2";
						account->key = QString::fromStdString(key);
						ready = !key.empty();
					});
				dialog.exec();
				Save();
				if (!ready) {
					preparing = false;
					Update();
					return;
				}
			}
			if (!primary)
				primary = account.get();
		}
		Save();
		if (!primary) {
			message->setText("Select at least one account.");
			preparing = false;
			Update();
			return;
		}
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, "server", primary->server.toUtf8().constData());
		obs_data_set_string(settings, "key", primary->key.toUtf8().constData());
		obs_data_set_bool(settings, "cherries_multistream", true);
		OBSServiceAutoRelease service =
			obs_service_create("rtmp_custom", "Cherries primary", settings, nullptr);
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
		startup.start(45000);
		main->StartStreaming();
	}

	void StartExtra()
	{
		if (!preparing || !primary)
			return;
		startup.stop();
		OBSOutputAutoRelease source = obs_frontend_get_streaming_output();
		obs_encoder_t *video = source ? obs_output_get_video_encoder(source) : nullptr;
		obs_encoder_t *audio = source ? obs_output_get_audio_encoder(source, 0) : nullptr;
		if (!video || !audio || strcmp(obs_encoder_get_codec(video), "h264") != 0 ||
		    strcmp(obs_encoder_get_codec(audio), "aac") != 0) {
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
			OBSDataAutoRelease settings = obs_data_create();
			obs_data_set_string(settings, "server", account->server.toUtf8().constData());
			obs_data_set_string(settings, "key", account->key.toUtf8().constData());
			account->service =
				obs_service_create("rtmp_custom", account->id.toUtf8().constData(), settings, nullptr);
			account->output =
				obs_output_create("rtmp_output", account->id.toUtf8().constData(), nullptr, nullptr);
			if (!account->service || !account->output) {
				account->status = "Could not create output";
				continue;
			}
			// These are the exact encoders used by OBS's primary output. No new encoders or render paths.
			if (!CherriesShareEncoders(account->output, source)) {
				account->status = "Incompatible shared encoder";
				continue;
			}
			obs_output_set_service(account->output, account->service);
			obs_output_set_reconnect_settings(account->output, 20, 2);
			account->status = obs_output_start(account->output) ? "Connecting" : "Connection failed";
		}
		message->setText(
			"All destinations share OBS's video and audio encoders. Sending does not confirm platform live status.");
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
	explicit CherriesMultistream(OBSBasic *obsMain) : QWidget(obsMain), main(obsMain)
	{
		auto layout = new QVBoxLayout(this);
		message = new QLabel("Select accounts and start one shared stream to Twitch and YouTube.", this);
		message->setWordWrap(true);
		layout->addWidget(message);
		table = new QTableWidget(0, 3, this);
		table->setHorizontalHeaderLabels({"Use", "Account", "Connection"});
		table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		table->setSelectionBehavior(QAbstractItemView::SelectRows);
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
		row = new QHBoxLayout;
		start = new QPushButton("Start selected streams", this);
		stop = new QPushButton("Stop all streams", this);
		row->addWidget(start);
		row->addWidget(stop);
		layout->addLayout(row);
		connect(addTwitch, &QPushButton::clicked, this, [this]() { Add(true); });
		connect(addYouTube, &QPushButton::clicked, this, [this]() { Add(false); });
		connect(remove, &QPushButton::clicked, this, [this]() {
			const int index = table->currentRow();
			if (index >= 0 && index < int(accounts.size()) && !running && !preparing) {
				table->setRowCount(0);
				accounts.erase(accounts.begin() + index);
				Save();
				Update();
			}
		});
		connect(start, &QPushButton::clicked, this, [this]() { Begin(); });
		connect(stop, &QPushButton::clicked, this, [this]() {
			StopExtra();
			if (obs_frontend_streaming_active() || running)
				main->ForceStopStreaming();
			else
				Restore();
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
		Update();
		obs_frontend_add_event_callback(Event, this);
	}

	~CherriesMultistream() override
	{
		obs_frontend_remove_event_callback(Event, this);
		for (auto &account : accounts)
			if (account->output)
				obs_output_force_stop(account->output);
		Save();
	}
};

void CherriesInstallMultistream(OBSBasic *main)
{
	auto panel = new CherriesMultistream(main);
	if (!obs_frontend_add_dock_by_id("cherriesMultistream", "Accounts & Multistream", panel))
		delete panel;
	else if (QApplication::platformName().contains("wayland"))
		InstallWaylandDockDragCleanup(qobject_cast<QDockWidget *>(panel->parentWidget()));
}
