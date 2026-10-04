#pragma once

#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>
#include <QString>
#include <cstdint>
#include <string>

namespace CherriesOAuthProtocol {
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
