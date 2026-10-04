#include "CherriesOAuth.hpp"
#include "CherriesOAuthProtocol.hpp"
#include "CherriesBundledClients.hpp"

#include <widgets/OBSBasic.hpp>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonDocument>
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

using namespace CherriesOAuthProtocol;

static std::string Setting(const char *key)
{
	const char *value = config_get_string(OBSBasic::Get()->Config(), "CherriesAccounts", key);
	return value ? value : "";
}

static void SaveSetting(const char *key, const std::string &value)
{
	config_set_string(OBSBasic::Get()->Config(), "CherriesAccounts", key, value.c_str());
	config_save_safe(OBSBasic::Get()->Config(), "tmp", nullptr);
}

std::string CherriesTwitchClient(QWidget *parent, bool setup, bool forceSetup)
{
	auto client = Setting("TwitchClientId");
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
		SaveSetting("TwitchClientId", id.toStdString());
	// Automatic setup commits only after authorization succeeds.
	return id.toStdString();
}

CherriesGoogleClient CherriesYouTubeClient(QWidget *parent, bool setup, bool forceSetup)
{
	CherriesGoogleClient client{Setting("YouTubeClientId"), Setting("YouTubeClientSecret")};
	if (client.id.empty() && client.secret.empty())
		client = {CherriesBundledGoogleId, CherriesBundledGoogleSecret};
	if (((!client.id.empty() && !client.secret.empty()) || !setup) && !forceSetup)
		return client;
	QMessageBox::information(
		parent, "YouTube application setup",
		"Select your Google Desktop app OAuth JSON file. You can reuse youtube_client_secret.json "
		"from your OBS chat setup. Enable the YouTube Data API and add your Google account as a test user "
		"if the application is in Testing. This file is imported once for this OBS profile.");
	const QString path = QFileDialog::getOpenFileName(
		parent, "Import YouTube Desktop OAuth client",
		QDir::homePath() + "/obs-multichat/youtube_client_secret.json", "JSON files (*.json)");
	if (path.isEmpty())
		return {};
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
		QMessageBox::warning(parent, "YouTube application setup", "Could not read this OAuth client file.");
		return {};
	}
	const auto installed = QJsonDocument::fromJson(file.readAll()).object().value("installed").toObject();
	client = {installed.value("client_id").toString().toStdString(),
		  installed.value("client_secret").toString().toStdString()};
	if (!QString::fromStdString(client.id).endsWith(".apps.googleusercontent.com") || client.secret.empty()) {
		QMessageBox::warning(parent, "YouTube application setup",
				     "Select credentials for a Desktop app, not a Web application or service account.");
		return {};
	}
	SaveSetting("YouTubeClientId", client.id);
	SaveSetting("YouTubeClientSecret", client.secret);
	return client;
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
		    valid.json.value("expires_in").toInt() > 60) {
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
			 uint64_t &expiry)
{
	if (client.empty())
		return false;
	QDialog dialog(parent);
	dialog.setWindowTitle("Connect Twitch");
	auto layout = new QVBoxLayout(&dialog);
	auto label = new QLabel("Requesting Twitch authorization…", &dialog);
	label->setWordWrap(true);
	layout->addWidget(label);
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
				label->setText("Authorize Cherries OBS in your browser. Your code is: " + code);
				open->setEnabled(true);
				deadline.start(seconds * 1000);
				poll.start(interval * 1000);
				QDesktopServices::openUrl(verification);
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
	QObject::connect(open, &QPushButton::clicked, &dialog, [&]() { QDesktopServices::openUrl(verification); });
	QObject::connect(&poll, &QTimer::timeout, &dialog, [&]() { send(false); });
	QObject::connect(&deadline, &QTimer::timeout, &dialog, [&]() {
		poll.stop();
		dialog.reject();
	});
	QTimer::singleShot(0, &dialog, [&]() { send(true); });
	dialog.exec();
	if (success)
		SaveSetting("TwitchClientId", client);
	return success;
}
