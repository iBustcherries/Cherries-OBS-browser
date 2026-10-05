#pragma once

#include <QJsonObject>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QString>
#include <cstdint>
#include <string>

namespace CherriesTwitchProtocol {
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

} // namespace CherriesTwitchProtocol
