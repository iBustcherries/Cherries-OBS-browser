#include "CherriesMultistream.hpp"
#include "CherriesOAuth.hpp"
#include "CherriesSharedOutput.hpp"
#include "TwitchAuth.hpp"
#include "YoutubeAuth.hpp"
#include <dialogs/OBSYoutubeActions.hpp>
#include <utility/YoutubeApiWrappers.hpp>
#include <widgets/OBSBasic.hpp>
#include <settings/OBSBasicSettings.hpp>
#include <qt-wrappers.hpp>
#include <obs-frontend-api.h>
#include <browser-panel.hpp>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTabWidget>
#include <QInputDialog>
#include <QPointer>
#include <QFormLayout>
#include <QCryptographicHash>
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
		QString server;
		QString key;
		QString status = "Offline";
		bool connected = true;
		QString broadcast;
		std::unique_ptr<QCefCookieManager> cookies;
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
			account->status = account->key.isEmpty() || dynamic_cast<TwitchAuth *>(account->auth.get())
						  ? "Offline"
						  : "Broadcast ready";
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
				if (account->key.isEmpty()) {
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
	void Manage();
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
		table = new QTableWidget(0, 3, this);
		table->setHorizontalHeaderLabels({"Use", "Account", "Connection"});
		table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
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
		hide();
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
void CherriesManageBroadcast()
{
	if (manager)
		manager->Manage();
}

void CherriesMultistream::Manage()
{
	if (preparing)
		return;
	ImportNative();
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
	auto twitchPage = new QWidget;
	auto twitchLayout = new QVBoxLayout(twitchPage);
	QCefWidget *browser = nullptr;
	if (twitch && cef) {
		const QString hash = QString::fromLatin1(
			QCryptographicHash::hash(twitch->id.toUtf8(), QCryptographicHash::Sha256).toHex());
		if (!twitch->cookies)
			twitch->cookies.reset(
				cef->create_cookie_manager("cherries-twitch-" + hash.toStdString(), true));
		twitchLayout->addWidget(
			new QLabel("Twitch account: " + twitch->label +
					   " — sign in to this account in the embedded page if prompted.",
				   twitchPage));
		browser = cef->create_widget(twitchPage,
					     "https://dashboard.twitch.tv/popout/u/" +
						     dynamic_cast<TwitchAuth *>(twitch->auth.get())->name +
						     "/stream-manager/edit-stream-info",
					     twitch->cookies.get());
		if (browser)
			twitchLayout->addWidget(browser);
	} else
		twitchLayout->addWidget(new QLabel(
			"Connect a Twitch account in Settings → Stream to edit its stream info.", twitchPage));
	if (youtube) {
		OBSYoutubeActions editor(&window, youtube->auth.get(), false);
		editor.setWindowFlags(Qt::Widget);
		editor.SetCombinedPage(twitchPage);
		layout->addWidget(new QLabel("YouTube account: " + youtube->label, &window));
		layout->addWidget(&editor);
		connect(&editor, &OBSYoutubeActions::rejected, &window, &QDialog::reject);
		connect(&editor, &OBSYoutubeActions::ok, &window,
			[&, youtube](const std::string &broadcast, const std::string &, const std::string &key,
				     bool autostart, bool, bool) {
				if (!autostart) {
					QMessageBox::warning(
						&window, "YouTube broadcast",
						"Enable automatic start for this broadcast before using it with shared multistream.");
					return;
				}
				youtube->broadcast = QString::fromStdString(broadcast);
				youtube->server = "rtmps://a.rtmps.youtube.com/live2";
				youtube->key = QString::fromStdString(key);
				youtube->status = "Broadcast ready";
				message->setText(
					"YouTube broadcast ready. Use Start Streaming in Controls when you are ready to go live.");
				Update();
			});
		window.exec();
		if (browser)
			browser->closeBrowser();
		if (twitch && twitch->cookies)
			twitch->cookies->FlushStore();
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
		layout->addWidget(tabs);
		auto buttons = new QDialogButtonBox(QDialogButtonBox::Close, &window);
		connect(buttons, &QDialogButtonBox::rejected, &window, &QDialog::reject);
		layout->addWidget(buttons);
		window.exec();
		if (browser)
			browser->closeBrowser();
		if (twitch && twitch->cookies)
			twitch->cookies->FlushStore();
	}
	Save();
}
