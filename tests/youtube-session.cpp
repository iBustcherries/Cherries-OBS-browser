#include "packaging/youtube/CherriesYouTubeProtocol.hpp"
#include <cassert>
int main()
{
	assert(!CherriesParseYouTubeClient("{}").valid());
	assert(!CherriesParseYouTubeClient(
			R"({"web":{"client_id":"a.apps.googleusercontent.com","client_secret":"s"}})")
			.valid());
	assert(CherriesParseYouTubeClient(
		       R"({"installed":{"client_id":"a.apps.googleusercontent.com","client_secret":"s"}})")
		       .valid());
	QString code;
	const auto reply = [&](QByteArray target) {
		return CherriesParseYouTubeRedirect("GET " + target + " HTTP/1.1\r\nHost: localhost\r\n\r\n",
						    "expected", code);
	};
	assert(reply("/?code=4%2Fabc%2Bdef&state=expected") == CherriesYouTubeReply::Authorized);
	assert(code == "4/abc+def");
	assert(reply("/?state=expected&code=token") == CherriesYouTubeReply::Authorized);
	assert(reply("/?state=wrong&code=token") == CherriesYouTubeReply::Ignore);
	assert(reply("/favicon.ico") == CherriesYouTubeReply::Ignore);
	assert(reply("/?state=expected&state=expected&code=token") == CherriesYouTubeReply::Ignore);
	assert(reply("/?state=expected&code=a&code=b") == CherriesYouTubeReply::Ignore);
	assert(reply("/?state=expected&error=access_denied") == CherriesYouTubeReply::Denied);
	assert(CherriesParseYouTubeRedirect("GET /?state=expected&code=a HTTP/1.1\r\n", "expected", code) ==
	       CherriesYouTubeReply::Ignore);
}
