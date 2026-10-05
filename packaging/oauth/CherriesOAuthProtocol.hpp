#pragma once

#include <QJsonObject>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QString>
#include <cstdint>
#include <string>

namespace CherriesOAuthProtocol {
inline std::string TwitchStreamKey(const std::string &key, bool bandwidthTest)
{
	return bandwidthTest ? key + "?bandwidthtest=true" : key;
}

inline QString TwitchBrowserProfile(const QString &saved, const QString &login)
{
	// Profiles are account-local directory names, never arbitrary filesystem paths.
	if (QRegularExpression("^cherries-twitch-([a-f0-9]{32}|[a-f0-9]{64})$").match(saved).hasMatch())
		return saved;
	// Reuse the stream-info cookies created by releases 6/7 for existing accounts.
	return "cherries-twitch-" +
	       QString::fromLatin1(
		       QCryptographicHash::hash(("twitch:" + login).toUtf8(), QCryptographicHash::Sha256).toHex());
}

inline QByteArray Form(const QList<QPair<QString, QString>> &fields)
{
	QByteArray result;
	for (const auto &field : fields) {
		if (!result.isEmpty())
			result += '&';
		result += QUrl::toPercentEncoding(field.first) + '=' + QUrl::toPercentEncoding(field.second);
	}
	return result;
}

inline bool ApplyToken(const QJsonObject &json, std::string &token, std::string &refresh, uint64_t &expiry,
		       uint64_t now, bool requireRefresh)
{
	const auto access = json.value("access_token").toString();
	const auto rotated = json.value("refresh_token").toString();
	const int seconds = json.value("expires_in").toInt();
	if (access.isEmpty() || seconds <= 0 || (requireRefresh && rotated.isEmpty()))
		return false;
	token = access.toStdString();
	if (!rotated.isEmpty())
		refresh = rotated.toStdString();
	expiry = now + seconds;
	return true;
}

enum class CallbackResult { Ignore, Code, Denied };
inline CallbackResult Callback(const QByteArray &request, const QString &state, QString &code)
{
	const auto line = request.left(request.indexOf("\r\n")).split(' ');
	if (line.size() != 3 || line[0] != "GET" || !line[2].startsWith("HTTP/1."))
		return CallbackResult::Ignore;
	const QUrl url = QUrl::fromEncoded(line[1], QUrl::StrictMode);
	if (!url.isRelative() || url.path() != "/" || url.hasFragment())
		return CallbackResult::Ignore;
	const QUrlQuery query(url);
	const auto states = query.allQueryItemValues("state", QUrl::FullyDecoded);
	if (state.isEmpty() || states.size() != 1 || states[0] != state)
		return CallbackResult::Ignore;
	if (query.hasQueryItem("error"))
		return CallbackResult::Denied;
	const auto codes = query.allQueryItemValues("code", QUrl::FullyDecoded);
	if (codes.size() != 1 || codes[0].isEmpty())
		return CallbackResult::Ignore;
	code = codes[0];
	return CallbackResult::Code;
}
} // namespace CherriesOAuthProtocol
