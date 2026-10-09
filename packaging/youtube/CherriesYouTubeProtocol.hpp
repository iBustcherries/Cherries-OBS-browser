#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

struct CherriesYouTubeCredentials {
	QString id;
	QString secret;
	bool valid() const { return id.endsWith(".apps.googleusercontent.com") && !secret.isEmpty(); }
};

inline CherriesYouTubeCredentials CherriesParseYouTubeClient(const QByteArray &data)
{
	const auto installed = QJsonDocument::fromJson(data).object().value("installed").toObject();
	CherriesYouTubeCredentials result{installed.value("client_id").toString(),
					  installed.value("client_secret").toString()};
	return result.valid() ? result : CherriesYouTubeCredentials{};
}

enum class CherriesYouTubeReply { Ignore, Authorized, Denied };
inline CherriesYouTubeReply CherriesParseYouTubeRedirect(const QByteArray &request, const QString &state, QString &code)
{
	code.clear();
	if (!request.contains("\r\n\r\n") || state.isEmpty())
		return CherriesYouTubeReply::Ignore;
	const auto words = request.left(request.indexOf("\r\n")).split(' ');
	if (words.size() != 3 || words[0] != "GET" || !words[2].startsWith("HTTP/1."))
		return CherriesYouTubeReply::Ignore;
	const QUrl url = QUrl::fromEncoded(words[1], QUrl::StrictMode);
	if (!url.isValid() || !url.isRelative() || url.path() != "/")
		return CherriesYouTubeReply::Ignore;
	const QUrlQuery query(url);
	int states = 0, codes = 0, errors = 0;
	for (const auto &item : query.queryItems(QUrl::FullyDecoded)) {
		if (item.first == "state") {
			++states;
			if (item.second != state)
				return CherriesYouTubeReply::Ignore;
		} else if (item.first == "code") {
			++codes;
			code = item.second;
		} else if (item.first == "error") {
			++errors;
		}
	}
	if (states != 1 || codes > 1 || errors > 1 || (codes && errors)) {
		code.clear();
		return CherriesYouTubeReply::Ignore;
	}
	if (errors == 1)
		return CherriesYouTubeReply::Denied;
	return codes == 1 && !code.isEmpty() ? CherriesYouTubeReply::Authorized : CherriesYouTubeReply::Ignore;
}
