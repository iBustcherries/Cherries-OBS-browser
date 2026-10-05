#pragma once
#include <QUrl>

// Keep Twitch authorization in the account's existing CEF request context.
inline bool ReuseTwitchPopup(bool allowed, const QUrl &url)
{
	const auto host = url.host().toLower();
	return allowed && url.isValid() && url.scheme() == "https" &&
	       (host == "twitch.tv" || host.endsWith(".twitch.tv"));
}
