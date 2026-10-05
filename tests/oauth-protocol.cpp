#include "packaging/oauth/CherriesOAuthProtocol.hpp"
#include <cassert>
#include "panel/browser-panel-popup.hpp"

int main()
{
	using namespace CherriesOAuthProtocol;
	assert(Form({{"code", "a+b&c=d /"}}) == "code=a%2Bb%26c%3Dd%20%2F");
	const QString profile = "cherries-twitch-0123456789abcdef0123456789abcdef";
	assert(TwitchBrowserProfile(profile, "alice") == profile);
	const auto legacy = TwitchBrowserProfile({}, "alice");
	assert(TwitchBrowserProfile(legacy, "alice") == legacy);
	assert(TwitchBrowserProfile("../../other", "alice") == legacy);
	assert(TwitchBrowserProfile({}, "bob") != legacy);
	assert(ReuseTwitchPopup(true, QUrl("https://id.twitch.tv/activate")));
	assert(ReuseTwitchPopup(true, QUrl("https://twitch.tv/login")));
	assert(!ReuseTwitchPopup(false, QUrl("https://id.twitch.tv/activate")));
	assert(!ReuseTwitchPopup(true, QUrl("http://id.twitch.tv/activate")));
	assert(!ReuseTwitchPopup(true, QUrl("https://twitch.tv.evil.example/")));
	assert(!ReuseTwitchPopup(true, QUrl("https://eviltwitch.tv/")));
	assert(!ReuseTwitchPopup(true, QUrl("https://twitch.tv@evil.example/")));
	assert(TwitchStreamKey("test-stream-key", false) == "test-stream-key");
	assert(TwitchStreamKey("test-stream-key", true) == "test-stream-key?bandwidthtest=true");
	QString code;
	auto callback = [&](QByteArray target) {
		return Callback("GET " + target + " HTTP/1.1\r\nHost: localhost\r\n\r\n", "expected", code);
	};
	assert(callback("/?state=expected&code=a%2Bb%26c%3Dd") == CallbackResult::Code);
	assert(code == "a+b&c=d");
	assert(callback("/?state=wrong&code=token") == CallbackResult::Ignore);
	assert(callback("/?state=expected&state=wrong&code=token") == CallbackResult::Ignore);
	assert(callback("/?state=expected&code=a&code=b") == CallbackResult::Ignore);
	assert(callback("/favicon.ico?state=expected&code=token") == CallbackResult::Ignore);
	assert(callback("https://evil.example/?state=expected&code=token") == CallbackResult::Ignore);
	assert(callback("/?state=expected&error=access_denied") == CallbackResult::Denied);
	std::string token = "old-access", refresh = "old-refresh";
	uint64_t expiry = 100;
	assert(ApplyToken({{"access_token", "new-access"}, {"refresh_token", "rotated-refresh"}, {"expires_in", 3600}},
			  token, refresh, expiry, 1000, true));
	assert(token == "new-access" && refresh == "rotated-refresh" && expiry == 4600);
	assert(!ApplyToken({{"access_token", "bad"}, {"expires_in", 3600}}, token, refresh, expiry, 1000, true));
	assert(token == "new-access" && refresh == "rotated-refresh");
	assert(ApplyToken({{"access_token", "google-refreshed"}, {"expires_in", 100}}, token, refresh, expiry, 2000,
			  false));
	assert(refresh == "rotated-refresh" && expiry == 2100);
}
