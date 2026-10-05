#include "CherriesTwitchBroadcast.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWidget>

namespace {
class TwitchBroadcast : public QWidget {
	QNetworkAccessManager network;
	std::string client;
	std::function<std::string()> token;
	QString broadcaster;
	QLineEdit *title;
	QLineEdit *query;
	QComboBox *category;
	QLineEdit *tags;
	QLineEdit *language;
	QLabel *status;
	QWidget *fields;
	QPushButton *refresh;
	QPushButton *save;
	bool loaded = false;
	bool busy = false;

	void SetBusy(bool value)
	{
		busy = value;
		fields->setEnabled(loaded && !busy);
		refresh->setEnabled(!busy);
		save->setEnabled(loaded && !busy);
	}
	void Request(const QString &path, const QUrlQuery &parameters, const QByteArray &method,
		     const QJsonObject &body, std::function<void(const QJsonObject &)> done)
	{
		SetBusy(true);
		QPointer<TwitchBroadcast> guard(this);
		const auto access = token();
		if (!guard)
			return;
		if (access.empty()) {
			status->setText("The Twitch connection expired. Reconnect this account in Settings → Stream.");
			SetBusy(false);
			return;
		}
		QUrl url("https://api.twitch.tv/helix/" + path);
		url.setQuery(parameters);
		QNetworkRequest request(url);
		request.setTransferTimeout(15000);
		request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
		request.setRawHeader("Client-Id", QByteArray::fromStdString(client));
		request.setRawHeader("Authorization", "Bearer " + QByteArray::fromStdString(access));
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		auto reply = method == "GET"
				     ? network.get(request)
				     : network.sendCustomRequest(request, method,
								 QJsonDocument(body).toJson(QJsonDocument::Compact));
		connect(reply, &QNetworkReply::finished, this, [this, reply, done]() {
			const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			QJsonParseError parse;
			const auto response = QJsonDocument::fromJson(reply->readAll(), &parse).object();
			const bool success = reply->error() == QNetworkReply::NoError && code >= 200 && code < 300 &&
					     (code == 204 || parse.error == QJsonParseError::NoError);
			QString error = response.value("message").toString();
			if (error.isEmpty())
				error = reply->errorString();
			reply->deleteLater();
			SetBusy(false);
			if (!success) {
				status->setText(
					code == 401 || code == 403
						? "Twitch could not authorize this change. Reconnect the account in Settings → Stream."
						: "Twitch request failed: " + error);
				return;
			}
			done(response);
		});
	}
	void Load()
	{
		if (busy)
			return;
		status->setText("Loading Twitch stream info…");
		Request("users", {}, "GET", {}, [this](const QJsonObject &users) {
			const auto data = users.value("data").toArray();
			const auto user = data.isEmpty() ? QJsonObject{} : data.at(0).toObject();
			broadcaster = user.value("id").toString();
			if (broadcaster.isEmpty()) {
				status->setText("Twitch did not return the connected account.");
				return;
			}
			QUrlQuery parameters;
			parameters.addQueryItem("broadcaster_id", broadcaster);
			Request("channels", parameters, "GET", {}, [this](const QJsonObject &response) {
				const auto data = response.value("data").toArray();
				if (data.isEmpty()) {
					status->setText("Twitch did not return stream information.");
					return;
				}
				const auto channel = data.at(0).toObject();
				title->setText(channel.value("title").toString());
				category->clear();
				category->addItem(channel.value("game_name").toString("No category"),
						  channel.value("game_id").toString());
				language->setText(channel.value("broadcaster_language").toString("en"));
				QStringList values;
				for (const auto &tag : channel.value("tags").toArray())
					values << tag.toString();
				tags->setText(values.join(", "));
				loaded = true;
				SetBusy(false);
				status->setText(
					"Connected. Changes are saved to Twitch when you click Save Stream Info.");
			});
		});
	}
	void Search()
	{
		if (busy || query->text().trimmed().isEmpty())
			return;
		QUrlQuery parameters;
		parameters.addQueryItem("query", query->text().trimmed());
		parameters.addQueryItem("first", "100");
		status->setText("Searching categories…");
		Request("search/categories", parameters, "GET", {}, [this](const QJsonObject &response) {
			const auto results = response.value("data").toArray();
			if (results.isEmpty()) {
				status->setText("No matching categories. Try another search.");
				return;
			}
			category->clear();
			for (const auto &result : results) {
				const auto game = result.toObject();
				category->addItem(game.value("name").toString(), game.value("id").toString());
			}
			status->setText("Choose a category, then save your stream info.");
		});
	}
	void Save()
	{
		if (busy || !loaded)
			return;
		if (title->text().trimmed().isEmpty()) {
			status->setText("Enter a stream title.");
			return;
		}
		const QString code = language->text().trimmed().toLower();
		if (!QRegularExpression("^([a-z]{2}|other)$").match(code).hasMatch()) {
			status->setText("Enter a language code such as en, es, or other.");
			return;
		}
		QJsonArray values;
		for (const auto &value : tags->text().split(',', Qt::SkipEmptyParts)) {
			const auto tag = value.trimmed();
			if (tag.isEmpty() || tag.size() > 25 ||
			    !QRegularExpression("^[\\p{L}\\p{N}]+$").match(tag).hasMatch()) {
				status->setText("Tags must be 1–25 letters or numbers, separated by commas.");
				return;
			}
			values.append(tag);
		}
		if (values.size() > 10) {
			status->setText("Use at most 10 tags.");
			return;
		}
		QJsonObject body{{"title", title->text().trimmed()},
				 {"game_id", category->currentData().toString()},
				 {"broadcaster_language", code},
				 {"tags", values}};
		QUrlQuery parameters;
		parameters.addQueryItem("broadcaster_id", broadcaster);
		status->setText("Saving Twitch stream info…");
		Request("channels", parameters, "PATCH", body, [this](const QJsonObject &) {
			status->setText(
				"Twitch stream info saved. Use Start Streaming in Controls when you are ready.");
		});
	}

public:
	TwitchBroadcast(QWidget *parent, const QString &account, const std::string &clientId,
			std::function<std::string()> accessToken)
		: QWidget(parent),
		  client(clientId),
		  token(std::move(accessToken))
	{
		auto layout = new QVBoxLayout(this);
		auto accountLabel = new QLabel("Twitch account: " + account, this);
		accountLabel->setTextFormat(Qt::PlainText);
		layout->addWidget(accountLabel);
		fields = new QWidget(this);
		auto form = new QFormLayout(fields);
		title = new QLineEdit(fields);
		title->setMaxLength(140);
		form->addRow("Title", title);
		auto searchRow = new QHBoxLayout;
		query = new QLineEdit(fields);
		query->setPlaceholderText("Search for a game or category");
		auto search = new QPushButton("Search", fields);
		searchRow->addWidget(query);
		searchRow->addWidget(search);
		form->addRow("Find category", searchRow);
		category = new QComboBox(fields);
		form->addRow("Category", category);
		tags = new QLineEdit(fields);
		tags->setPlaceholderText("Gaming, English, Linux");
		form->addRow("Tags", tags);
		language = new QLineEdit(fields);
		language->setPlaceholderText("en");
		form->addRow("Stream language", language);
		layout->addWidget(fields);
		status = new QLabel(this);
		status->setTextFormat(Qt::PlainText);
		status->setWordWrap(true);
		layout->addWidget(status);
		auto buttons = new QHBoxLayout;
		refresh = new QPushButton("Refresh", this);
		save = new QPushButton("Save Stream Info", this);
		buttons->addWidget(refresh);
		buttons->addStretch();
		buttons->addWidget(save);
		layout->addLayout(buttons);
		layout->addStretch(1);
		connect(refresh, &QPushButton::clicked, this, [this]() { Load(); });
		connect(save, &QPushButton::clicked, this, [this]() { Save(); });
		connect(search, &QPushButton::clicked, this, [this]() { Search(); });
		connect(query, &QLineEdit::returnPressed, this, [this]() { Search(); });
		SetBusy(false);
		QTimer::singleShot(0, this, [this]() { Load(); });
	}
};
} // namespace

QWidget *CherriesCreateTwitchBroadcast(QWidget *parent, const QString &account, const std::string &client,
				       std::function<std::string()> token)
{
	return new TwitchBroadcast(parent, account, client, std::move(token));
}
