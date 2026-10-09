#include "packaging/gamecapture/CherriesSteamCapture.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>
#include <iostream>
using namespace CherriesCapture;
template<class F> void rejected(F operation)
{
	bool failed = false;
	try {
		operation();
	} catch (const std::exception &) {
		failed = true;
	}
	assert(failed);
}
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temporary;
	assert(temporary.isValid());
	QString base = temporary.path();
	SteamCaptureStore store(base + "/state");
	if (argc == 3 && QByteArray(argv[1]) == "--installed-vulkan") {
		store.setVulkan(true, QString::fromLocal8Bit(argv[2]));
		std::cout << "Installed automatic Vulkan manifests prepared for loader test\n";
		return 0;
	}
	QString root = base + "/Steam";
	QString external = base + "/Other Drive/Steam Library";
	saveFile(root + "/steamapps/libraryfolders.vdf",
		 "\"libraryfolders\" { \"0\" { \"path\" " + VdfDocument::quote(root.toUtf8()) + " } \"1\" { \"path\" " +
			 VdfDocument::quote(external.toUtf8()) + " } }");
	saveFile(external + "/steamapps/appmanifest_588650.acf",
		 R"("AppState" { "appid" "588650" "name" "Dead Cells" "installdir" "Dead Cells" })");
	saveFile(external + "/steamapps/common/Dead Cells/deadcells.sh", "#!/bin/sh\nLD_PRELOAD= ./deadcells\n");
	saveFile(root + "/steamapps/appmanifest_999.acf",
		 R"("AppState" { "appid" "999" "name" "New Unknown Game" "installdir" "New Unknown Game" })");
	QDir().mkpath(root + "/steamapps/common/New Unknown Game");
	const QByteArray original = R"(// Keep this comment and unrelated account data.
"UserLocalConfigStore"
{
 "Software" { "valve" { "Steam" { "apps" {
  "588650" { "LaunchOptions" "env PROTON_LOG=1 gamemoderun %command% --foo \"two words\"" "Other" "unchanged" }
  "111" { "LaunchOptions" "mangohud %command%" }
 } } } }
 "Friends" { "Keep" "\\path\tvalue" }
}
)";
	QString config = root + "/userdata/42/config/localconfig.vdf";
	saveFile(config, original);
	saveFile(root + "/userdata/43/config/localconfig.vdf", original);
	saveFile(root + "/config/loginusers.vdf",
		 R"("users" { "76561197960265770" { "PersonaName" "Moe" "MostRecent" "1" } })");
	auto accounts = steamAccounts(root);
	assert(accounts.size() == 2 && accounts[0].id == "42" && accounts[0].name == "Moe");
	auto games = installedGames(root);
	assert(games.size() == 2 && games[0].id == "588650" && games[0].nativeDeadCells);
	assert(games[0].directory == external + "/steamapps/common/Dead Cells");
	QDir().mkpath(base + "/proc");
	store.configure(accounts[0], games[0], true, base + "/proc");
	QByteArray enabled = readFile(config);
	VdfDocument enabledDocument(enabled);
	assert(enabledDocument.find(launchPath("588650"))->value ==
	       "env PROTON_LOG=1 gamemoderun /usr/bin/cherries-gamecapture %command% --foo \"two words\"");
	assert(enabled.contains("// Keep this comment") &&
	       enabled.contains("\"111\" { \"LaunchOptions\" \"mangohud %command%\" }"));
	assert(readFile(root + "/userdata/43/config/localconfig.vdf") == original);
	assert(readFile(external + "/steamapps/common/Dead Cells/deadcells.sh") ==
	       "#!/bin/sh\nLD_PRELOAD= ./deadcells\n");
	store.configure(accounts[0], games[0], true, base + "/proc"); // idempotent
	assert(readFile(config) == enabled);
	store.configure(accounts[0], games[0], false, base + "/proc");
	assert(readFile(config) == original);
	// Missing key and missing objects can be created, then the setting removed.
	saveFile(config, "\"UserLocalConfigStore\" { \"Friends\" { \"keep\" \"1\" } }");
	store.configure(accounts[0], games[1], true, base + "/proc");
	assert(VdfDocument(readFile(config)).find(launchPath("999"))->value ==
	       "/usr/bin/cherries-gamecapture %command%");
	store.configure(accounts[0], games[1], false, base + "/proc");
	assert(!VdfDocument(readFile(config)).find(launchPath("999")));
	assert(VdfDocument(readFile(config)).find({"UserLocalConfigStore", "Friends", "keep"})->value == "1");
	// Existing argument-only options, wrappers and environment prefixes are preserved.
	assert(configuredOptions("-novid --name 'two words'") ==
	       "/usr/bin/cherries-gamecapture %command% -novid --name 'two words'");
	assert(configuredOptions("env X=1 mangohud %command% > ~/game.log") ==
	       "env X=1 mangohud /usr/bin/cherries-gamecapture %command% > ~/game.log");
	rejected([] { configuredOptions("'%command%'"); });
	rejected([] { configuredOptions("%command% %command%"); });
	rejected([] { configuredOptions("cherries-gamecapture %command%"); });
	// Steam running, malformed/ambiguous files and later user edits must never be overwritten.
	saveFile(base + "/proc/123/comm", "steam\n");
	QByteArray prior = readFile(config);
	rejected([&] { store.configure(accounts[0], games[0], true, base + "/proc"); });
	assert(readFile(config) == prior);
	QFile::remove(base + "/proc/123/comm");
	saveFile(config, "\"UserLocalConfigStore\" {");
	rejected([&] { store.configure(accounts[0], games[0], true, base + "/proc"); });
	assert(readFile(config) == "\"UserLocalConfigStore\" {");
	rejected([] { VdfDocument("\"apps\" {} \"apps\" {}").find({"apps"}); });
	saveFile(config, original);
	store.configure(accounts[0], games[0], true, base + "/proc");
	saveFile(config, VdfDocument(readFile(config)).set(launchPath("588650"), "user changed options"));
	rejected([&] { store.configure(accounts[0], games[0], false, base + "/proc"); });
	assert(VdfDocument(readFile(config)).find(launchPath("588650"))->value == "user changed options");
	assert(!store.capturedBefore("588650"));
	store.markCaptured("588650");
	assert(store.capturedBefore("588650") && !store.capturedBefore("999"));
	// Automatic mode preserves the usual Steam identity and all game launch options.
	QString desktop = base + "/system/steam.desktop";
	QString applications = base + "/applications";
	QString autostart = base + "/config/autostart/steam.desktop";
	QByteArray shortcut =
		"[Desktop Entry]\nType=Application\nName=Steam\nIcon=steam\nExec=/usr/bin/steam %U\nActions=Library;\n[Desktop Action Library]\nName=Library\nExec=steam steam://open/games\n";
	saveFile(desktop, shortcut);
	QByteArray unchangedGameConfig = readFile(config);
	store.setSteamAutomatic(true, desktop, applications, autostart, base + "/proc");
	assert(store.steamAutomatic());
	QByteArray automaticShortcut = readFile(applications + "/steam.desktop");
	assert(automaticShortcut.contains("Name=Steam\nIcon=steam\n"));
	assert(automaticShortcut.contains("Exec=/usr/bin/cherries-gamecapture /usr/bin/steam %U"));
	assert(automaticShortcut.contains("Exec=/usr/bin/cherries-gamecapture steam steam://open/games"));
	assert(!QFileInfo::exists(autostart)); // Does not opt the user into starting Steam on login.
	assert(readFile(config) == unchangedGameConfig);
	store.setSteamAutomatic(false, desktop, applications, autostart, base + "/proc");
	assert(!store.steamAutomatic() && !QFileInfo::exists(applications + "/steam.desktop"));
	// Existing user shortcut and autostart are restored byte for byte.
	saveFile(applications + "/steam.desktop", shortcut);
	saveFile(autostart, shortcut);
	store.setSteamAutomatic(true, applications + "/steam.desktop", applications, autostart, base + "/proc");
	assert(readFile(autostart).contains("cherries-gamecapture"));
	store.setSteamAutomatic(false, desktop, applications, autostart, base + "/proc");
	assert(readFile(applications + "/steam.desktop") == shortcut && readFile(autostart) == shortcut);
	saveFile(base + "/proc/123/comm", "steam\n");
	rejected([&] { store.setSteamAutomatic(true, desktop, applications, autostart, base + "/proc"); });
	assert(readFile(applications + "/steam.desktop") == shortcut);
	QFile::remove(base + "/proc/123/comm");
	store.setSteamAutomatic(true, desktop, applications, autostart, base + "/proc");
	saveFile(applications + "/steam.desktop", "user changed shortcut");
	QByteArray savedAutostart = readFile(autostart);
	rejected([&] { store.setSteamAutomatic(false, desktop, applications, autostart, base + "/proc"); });
	assert(readFile(applications + "/steam.desktop") == "user changed shortcut" &&
	       readFile(autostart) == savedAutostart);
	saveFile(applications + "/steam.desktop", automaticShortcut);
	store.setSteamAutomatic(false, desktop, applications, autostart, base + "/proc");
	assert(readFile(config) == unchangedGameConfig);
	// Register both architectures without enable_environment, using real copies in the user's mounted home.
	QString source = base + "/installed";
	QString data = base + "/userdata";
	for (QString arch : {QString("64"), QString("32")}) {
		saveFile(source + (arch == "64" ? "/lib64/" : "/lib/") + "libVkLayer_obs_vkcapture.so",
			 "test binary " + arch.toUtf8());
		QJsonObject layer{{"name", "VK_LAYER_CHERRIES_vkcapture_" + arch},
				  {"enable_environment", QJsonObject{{"OBS_VKCAPTURE", "1"}}},
				  {"disable_environment", QJsonObject{{"DISABLE_OBS_VKCAPTURE", "1"}}}};
		saveFile(source + "/share/vulkan/implicit_layer.d/obs_vkcapture_" + arch + ".json",
			 QJsonDocument(QJsonObject{{"file_format_version", "1.1.2"}, {"layer", layer}}).toJson());
	}
	store.setVulkan(true, data, source);
	assert(store.vulkanEnabled());
	for (QString arch : {QString("64"), QString("32")}) {
		auto layer =
			QJsonDocument::fromJson(
				readFile(data + "/vulkan/implicit_layer.d/cherries_obs_vkcapture_" + arch + ".json"))
				.object()
				.value("layer")
				.toObject();
		assert(!layer.contains("enable_environment") && layer.contains("disable_environment"));
		assert(readFile(layer.value("library_path").toString()) == "test binary " + arch.toUtf8());
	}
	store.setVulkan(false, data, source);
	assert(!store.vulkanEnabled());
	assert(!QFileInfo::exists(data + "/vulkan/implicit_layer.d/cherries_obs_vkcapture_64.json"));
	assert(!QFileInfo::exists(data + "/vulkan/implicit_layer.d/cherries_obs_vkcapture_32.json"));
	QFile::remove(source + "/lib/libVkLayer_obs_vkcapture.so");
	rejected([&] { store.setVulkan(true, data, source); });
	assert(!store.vulkanEnabled() &&
	       !QFileInfo::exists(data + "/vulkan/implicit_layer.d/cherries_obs_vkcapture_64.json"));
	std::cout
		<< "Steam setup: custom libraries, accounts, lossless option updates/restores, running-client refusal, capture history and reversible automatic Vulkan manifests passed\n";
}
