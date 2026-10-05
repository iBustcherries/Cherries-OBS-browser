#include "CherriesTwitchLogin.hpp"
#include "CherriesTwitchProtocol.hpp"
#include "CherriesTwitchClient.hpp"

#include <widgets/OBSBasic.hpp>
#include <browser-panel.hpp>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>
#include <ctime>

using namespace CherriesTwitchProtocol;
extern QCef *cef;

static std::string Setting(const char *key)
{
	const char *value = config_get_string(OBSBasic::Get()->Config(), "Twitch", key);
	return value ? value : "";
}

static void SaveSetting(const char *key, const std::string &value)
{
	config_set_string(OBSBasic::Get()->Config(), "Twitch", key, value.c_str());
	config_save_safe(OBSBasic::Get()->Config(), "tmp", nullptr);
	if (std::string(key) == "ClientId") {
		config_set_string(App()->GetUserConfig(), "CherriesTwitch", "ClientId", value.c_str());
		config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
	}
}

std::string CherriesTwitchClient(QWidget *parent, bool setup, bool forceSetup)
{
	auto client = Setting("ClientId");
	if (client.empty()) {
		const char *saved = config_get_string(App()->GetUserConfig(), "CherriesTwitch", "ClientId");
		client = saved ? saved : "";
	}
	if (client.empty())
		client = CherriesBundledTwitchId;
	if ((!client.empty() || !setup) && !forceSetup)
		return client;
	bool ok = false;
	const QString id =
		QInputDialog::getText(
			parent, "Twitch application setup",
			"Enter your Twitch Public application Client ID.\n"
			"Register Cherries OBS at https://dev.twitch.tv/console/apps with Client Type: Public.\n"
			"This is a one-time setup for this OBS profile; no client secret is needed.",
			QLineEdit::Normal, {}, &ok)
			.trimmed();
	if (!ok || id.isEmpty())
		return {};
	if (!QRegularExpression("^[a-zA-Z0-9]{10,128}$").match(id).hasMatch()) {
		QMessageBox::warning(parent, "Twitch application setup", "That does not look like a Twitch Client ID.");
		return {};
	}
	if (forceSetup)
		SaveSetting("ClientId", id.toStdString());
	// Automatic setup commits only after authorization succeeds.
	return id.toStdString();
}

struct Response {
	int status = 0;
	QJsonObject json;
};

static Response Request(const QUrl &url, const QByteArray &body = {}, const std::string &token = {})
{
	QNetworkAccessManager manager;
	QNetworkRequest request(url);
	request.setTransferTimeout(15000);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
	if (!token.empty())
		request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(token));
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
	QNetworkReply *reply = body.isEmpty() ? manager.get(request) : manager.post(request, body);
	QEventLoop loop;
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();
	return {reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
		QJsonDocument::fromJson(reply->readAll()).object()};
}

bool CherriesTwitchEnsureToken(const std::string &client, std::string &token, std::string &refresh, uint64_t &expiry)
{
	if (client.empty())
		return false;
	if (!token.empty()) {
		const auto valid = Request(QUrl("https://id.twitch.tv/oauth2/validate"), {}, token);
		if (valid.status == 200 && valid.json.value("client_id").toString().toStdString() == client &&
		    valid.json.value("expires_in").toInt() > 60 &&
		    valid.json.value("scopes").toArray().contains(QStringLiteral("channel:read:stream_key"))) {
			expiry = uint64_t(time(nullptr)) + valid.json.value("expires_in").toInt();
			return true;
		}
		// A transport failure must not discard a still-usable token or force a new login.
		if (valid.status != 200 && valid.status != 401)
			return false;
	}
	if (refresh.empty())
		return false;
	const auto response = Request(QUrl("https://id.twitch.tv/oauth2/token"),
				      Form({{"client_id", QString::fromStdString(client)},
					    {"grant_type", "refresh_token"},
					    {"refresh_token", QString::fromStdString(refresh)}}));
	return response.status == 200 && ApplyToken(response.json, token, refresh, expiry, time(nullptr), true);
}

bool CherriesTwitchLogin(QWidget *parent, const std::string &client, std::string &token, std::string &refresh,
			 uint64_t &expiry, QCefCookieManager *cookies)
{
	if (client.empty())
		return false;
	if (!cef || !cookies) {
		QMessageBox::warning(parent, "Connect Twitch",
				     "The browser component is unavailable. Restart OBS and try again.");
		return false;
	}
	QDialog dialog(parent);
	dialog.setWindowTitle("Connect Twitch");
	dialog.resize(800, 750);
	auto layout = new QVBoxLayout(&dialog);
	auto label = new QLabel("Requesting Twitch authorization…", &dialog);
	label->setWordWrap(true);
	layout->addWidget(label);
	auto browser = cef->create_widget(&dialog, "about:blank", cookies);
	if (!browser)
		return false;
	browser->allowAllPopups(true);
	layout->addWidget(browser, 1);
	QObject::connect(&dialog, &QDialog::finished, &dialog, [browser, cookies]() {
		browser->closeBrowser();
		cookies->FlushStore();
	});
	auto open = new QPushButton("Open Twitch authorization", &dialog);
	open->setEnabled(false);
	layout->addWidget(open);
	auto buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
	layout->addWidget(buttons);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QNetworkAccessManager manager(&dialog);
	QTimer poll;
	poll.setSingleShot(true);
	QTimer deadline;
	deadline.setSingleShot(true);
	QString device;
	QUrl verification;
	int interval = 5;
	bool success = false;
	const QString scopes = "channel:read:stream_key channel:manage:broadcast";
	auto send = [&](bool initial) {
		QNetworkRequest request(
			QUrl(initial ? "https://id.twitch.tv/oauth2/device" : "https://id.twitch.tv/oauth2/token"));
		request.setTransferTimeout(15000);
		request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
		QList<QPair<QString, QString>> fields{{"client_id", QString::fromStdString(client)},
						      {"scopes", scopes}};
		if (!initial) {
			fields.append({"device_code", device});
			fields.append(
				QPair<QString, QString>{"grant_type", "urn:ietf:params:oauth:grant-type:device_code"});
		}
		auto reply = manager.post(request, Form(fields));
		QObject::connect(&dialog, &QDialog::finished, reply, [reply]() { reply->abort(); });
		QObject::connect(reply, &QNetworkReply::finished, &dialog, [&, reply, initial]() {
			const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			const auto json = QJsonDocument::fromJson(reply->readAll()).object();
			reply->deleteLater();
			if (!dialog.isVisible())
				return;
			if (initial && status == 200) {
				device = json.value("device_code").toString();
				verification = QUrl(json.value("verification_uri").toString());
				const QString code = json.value("user_code").toString();
				const int seconds = json.value("expires_in").toInt();
				if (device.isEmpty() || code.isEmpty() || seconds <= 0 || seconds > 3600 ||
				    verification.scheme() != "https" || verification.host() != "www.twitch.tv" ||
				    verification.path() != "/activate") {
					label->setText(
						"Twitch returned an invalid authorization response. Cancel and try again.");
					return;
				}
				interval = qBound(5, json.value("interval").toInt(5), 60);
				label->setText("Sign in to Twitch below and authorize Cherries OBS. Your code is: " +
					       code);
				open->setEnabled(true);
				deadline.start(seconds * 1000);
				poll.start(interval * 1000);
				browser->setURL(verification.toString(QUrl::FullyEncoded).toStdString());
			} else if (!initial && status == 200 &&
				   ApplyToken(json, token, refresh, expiry, time(nullptr), true)) {
				success = true;
				dialog.accept();
			} else {
				const auto message = json.value("message").toString(json.value("error").toString());
				if (!initial &&
				    (message == "authorization_pending" || message == "slow_down" || status == 429)) {
					if (message == "slow_down" || status == 429)
						interval = qMin(interval + 5, 60);
					poll.start(interval * 1000);
				} else {
					poll.stop();
					label->setText(
						initial ? "Could not start Twitch authorization. Check your connection and Public application Client ID. Cancel to retry."
							: "Twitch authorization failed or was declined. Cancel to retry.");
				}
			}
		});
	};
	QObject::connect(open, &QPushButton::clicked, &dialog,
			 [&]() { browser->setURL(verification.toString(QUrl::FullyEncoded).toStdString()); });
	QObject::connect(&poll, &QTimer::timeout, &dialog, [&]() { send(false); });
	QObject::connect(&deadline, &QTimer::timeout, &dialog, [&]() {
		poll.stop();
		dialog.reject();
	});
	QTimer::singleShot(0, &dialog, [&]() { send(true); });
	dialog.exec();
	if (success)
		SaveSetting("ClientId", client);
	return success;
}
