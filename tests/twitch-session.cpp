#include "packaging/twitch/CherriesTwitchProtocol.hpp"
#include "panel/browser-panel-popup.hpp"
#include <QCoreApplication>
#include <QDebug>
#include <cstdlib>
using namespace CherriesTwitchProtocol;
static void Check(bool ok)
{
	if (!ok)
		std::abort();
}
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	Check(Form({{"refresh_token", "a+b&c"}, {"client_id", "id"}}) == "refresh_token=a%2Bb%26c&client_id=id");
	std::string token = "old", refresh = "original";
	uint64_t expiry = 10;
	Check(!ApplyToken({{"access_token", "new"}, {"expires_in", 300}}, token, refresh, expiry, 100, true));
	Check(token == "old" && refresh == "original" && expiry == 10);
	Check(ApplyToken({{"access_token", "new"}, {"refresh_token", "rotated"}, {"expires_in", 300}}, token, refresh,
			 expiry, 100, true));
	Check(token == "new" && refresh == "rotated" && expiry == 400);
	Check(!ApplyToken({{"access_token", "bad"}, {"refresh_token", "rotated"}, {"expires_in", 0}}, token, refresh,
			  expiry, 100, true));
	Check(token == "new" && expiry == 400);
	Check(ReuseTwitchPopup(true, QUrl("https://id.twitch.tv/oauth2/authorize")));
	Check(!ReuseTwitchPopup(false, QUrl("https://id.twitch.tv/oauth2/authorize")));
	Check(!ReuseTwitchPopup(true, QUrl("https://twitch.tv.attacker.example/")));
	Check(!ReuseTwitchPopup(true, QUrl("http://id.twitch.tv/")));
	qInfo("Twitch token rotation, form encoding and session popup checks passed");
}
