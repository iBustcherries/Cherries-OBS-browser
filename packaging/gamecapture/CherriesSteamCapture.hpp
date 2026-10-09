/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "CherriesSteamVdf.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QLockFile>
#include <QRegularExpression>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDateTime>
#include <algorithm>
#include <cstdint>
#include <unistd.h>

namespace CherriesCapture {
inline QByteArray readFile(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024)
		throw std::runtime_error("Cannot read Steam/capture settings. Check file permissions.");
	return file.readAll();
}
inline void saveFile(const QString &path, const QByteArray &bytes,
		     QFile::Permissions permissions = QFile::ReadOwner | QFile::WriteOwner)
{
	if (!QDir().mkpath(QFileInfo(path).absolutePath()))
		throw std::runtime_error("Cannot create the capture settings folder.");
	QSaveFile file(path);
	file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(permissions) ||
	    file.write(bytes) != bytes.size() || !file.commit())
		throw std::runtime_error("Cannot safely save capture settings. No direct overwrite was attempted.");
}
inline bool numericId(const QString &value)
{
	bool ok = false;
	qulonglong id = value.toULongLong(&ok);
	return ok && id && QString::number(id) == value;
}
inline QByteArray setting(const VdfNode *parent, const char *key)
{
	const auto *node = parent ? parent->child(key) : nullptr;
	return node && !node->object ? node->value : QByteArray();
}
inline QString nativeSteamRoot()
{
	for (const auto &candidate : {QDir::homePath() + "/.local/share/Steam", QDir::homePath() + "/.steam/root",
				      QDir::homePath() + "/.steam/steam"}) {
		QString root = QFileInfo(candidate).canonicalFilePath();
		if (!root.isEmpty() && !root.contains("/.var/app/com.valvesoftware.Steam/") &&
		    QDir(root + "/steamapps").exists())
			return root;
	}
	return {};
}
inline bool steamRunning(const QString &proc = "/proc")
{
	for (const auto &entry : QDir(proc).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		if (!numericId(entry.fileName()) || entry.ownerId() != getuid())
			continue;
		QFile file(entry.filePath() + "/comm");
		if (file.open(QIODevice::ReadOnly)) {
			QByteArray name = file.read(128).trimmed();
			if (name == "steam" || name == "steam.sh")
				return true;
		}
	}
	return false;
}
struct SteamAccount {
	QString id, name, config;
	bool recent = false;
};
inline std::vector<SteamAccount> steamAccounts(const QString &root)
{
	std::vector<SteamAccount> result;
	QMap<QString, QPair<QString, bool>> names;
	try {
		VdfDocument document(readFile(root + "/config/loginusers.vdf"));
		const auto *users = document.find({"users"});
		if (users) {
			for (const auto &user : users->children) {
				bool ok = false;
				qulonglong steamId = user.key.toULongLong(&ok);
				constexpr qulonglong base = 76561197960265728ULL;
				if (!ok || steamId <= base || steamId - base > UINT32_MAX)
					continue;
				QByteArray name = setting(&user, "PersonaName");
				if (name.isEmpty())
					name = setting(&user, "AccountName");
				names.insert(QString::number(steamId - base),
					     {QString::fromUtf8(name), setting(&user, "MostRecent") == "1"});
			}
		}
	} catch (const std::exception &) {
		// Accounts remain selectable by their local account ID if display names are unavailable.
	}
	for (const auto &folder : QDir(root + "/userdata").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		QString config = root + "/userdata/" + folder + "/config/localconfig.vdf";
		if (!numericId(folder) || !QFileInfo::exists(config))
			continue;
		auto details = names.value(folder);
		result.push_back({folder, details.first.isEmpty() ? "Account " + folder : details.first,
				  QFileInfo(config).canonicalFilePath(), details.second});
	}
	std::stable_sort(result.begin(), result.end(),
			 [](const auto &a, const auto &b) { return a.recent > b.recent; });
	return result;
}
struct SteamGame {
	QString id, name, directory;
	bool nativeDeadCells = false;
};
inline std::vector<SteamGame> installedGames(const QString &root)
{
	QStringList libraries{root};
	QString libraryFile = root + "/steamapps/libraryfolders.vdf";
	if (!QFileInfo::exists(libraryFile))
		libraryFile = root + "/config/libraryfolders.vdf";
	if (QFileInfo::exists(libraryFile)) {
		VdfDocument document(readFile(libraryFile));
		const auto *folders = document.find({"libraryfolders"});
		if (!folders || !folders->object)
			throw std::runtime_error("Steam's library folder list is not recognized.");
		for (const auto &folder : folders->children) {
			bool number = false;
			folder.key.toUInt(&number);
			if (!number)
				continue;
			QString path = QString::fromUtf8(folder.object ? setting(&folder, "path") : folder.value);
			if (!path.isEmpty() && !libraries.contains(path))
				libraries.append(path);
		}
	}
	std::vector<SteamGame> games;
	QSet<QString> seen;
	for (const auto &library : libraries) {
		QDir steamapps(library + "/steamapps");
		for (const auto &manifest : steamapps.entryList({"appmanifest_*.acf"}, QDir::Files)) {
			VdfDocument document(readFile(steamapps.filePath(manifest)));
			const auto *app = document.find({"AppState"});
			QString id = QString::fromUtf8(setting(app, "appid"));
			QString name = QString::fromUtf8(setting(app, "name"));
			QString directory =
				steamapps.filePath("common/" + QString::fromUtf8(setting(app, "installdir")));
			if (!numericId(id) || name.isEmpty() || seen.contains(id) || !QDir(directory).exists() ||
			    name.startsWith("Steam Linux Runtime", Qt::CaseInsensitive) ||
			    name.startsWith("Proton", Qt::CaseInsensitive) ||
			    name.startsWith("Steamworks", Qt::CaseInsensitive))
				continue;
			seen.insert(id);
			games.push_back({id, name, QFileInfo(directory).canonicalFilePath(),
					 id == "588650" && QFileInfo::exists(directory + "/deadcells.sh")});
		}
	}
	std::sort(games.begin(), games.end(),
		  [](const auto &a, const auto &b) { return QString::localeAwareCompare(a.name, b.name) < 0; });
	return games;
}
inline QStringList launchPath(const QString &id)
{
	return {"UserLocalConfigStore", "Software", "Valve", "Steam", "apps", id, "LaunchOptions"};
}
inline QByteArray configuredOptions(const QByteArray &original)
{
	if (original.contains("cherries-gamecapture"))
		throw std::runtime_error("Capture is already present in this game's launch options.");
	if (original.contains('\n') || original.contains('\r'))
		throw std::runtime_error(
			"Multiline launch options are not supported. Existing options were left untouched.");
	const QByteArray marker = "%command%";
	int matches = original.count(marker);
	if (!matches)
		return "/usr/bin/cherries-gamecapture %command%" + (original.isEmpty() ? QByteArray() : " " + original);
	if (matches != 1)
		throw std::runtime_error(
			"Multiple Steam command placeholders are not supported. Options were left untouched.");
	qsizetype target = original.indexOf(marker);
	char quote = 0;
	bool escaped = false;
	for (qsizetype i = 0; i < target; i++) {
		char c = original[i];
		if (escaped) {
			escaped = false;
			continue;
		}
		if (c == '\\' && quote != '\'') {
			escaped = true;
			continue;
		}
		if (quote) {
			if (c == quote)
				quote = 0;
		} else if (c == '\'' || c == '"')
			quote = c;
	}
	if (quote || escaped || (target && !QByteArray(" \t").contains(original[target - 1])) ||
	    (target + marker.size() < original.size() &&
	     !QByteArray(" \t;&|<>").contains(original[target + marker.size()])))
		throw std::runtime_error(
			"The Steam command placeholder must be an unquoted command. Options were left untouched.");
	QByteArray result = original;
	result.insert(target, "/usr/bin/cherries-gamecapture ");
	return result;
}

class SteamCaptureStore {
	QString folder;
	QString recordKey(const SteamAccount &account, const SteamGame &game) const
	{
		return QString::fromLatin1(
			       QCryptographicHash::hash(account.config.toUtf8(), QCryptographicHash::Sha256).toHex()) +
		       "/" + game.id;
	}
	QJsonObject load() const
	{
		if (!QFileInfo::exists(folder + "/settings.json"))
			return {};
		QJsonParseError error;
		auto document = QJsonDocument::fromJson(readFile(folder + "/settings.json"), &error);
		if (error.error != QJsonParseError::NoError || !document.isObject())
			throw std::runtime_error(
				"Capture settings are damaged; existing Steam settings were left untouched.");
		return document.object();
	}
	void save(const QJsonObject &state) const
	{
		saveFile(folder + "/settings.json", QJsonDocument(state).toJson());
	}

public:
	explicit SteamCaptureStore(QString stateFolder = {})
		: folder(stateFolder.isEmpty()
				 ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
					   "/cherries-capture"
				 : std::move(stateFolder))
	{
	}
	QJsonObject record(const SteamAccount &account, const SteamGame &game) const
	{
		return load().value("games").toObject().value(recordKey(account, game)).toObject();
	}
	bool managed(const SteamAccount &account, const SteamGame &game, const QByteArray &current) const
	{
		auto item = record(account, game);
		return item.contains("configured") &&
		       current == QByteArray::fromBase64(item.value("configured").toString().toLatin1());
	}
	void configure(const SteamAccount &account, const SteamGame &game, bool enable, const QString &proc = "/proc")
	{
		if (!numericId(account.id) || !numericId(game.id) || account.config.isEmpty())
			throw std::runtime_error("Select a valid Steam account and installed game.");
		if (steamRunning(proc))
			throw std::runtime_error(
				"Close Steam completely, then try again. Steam must be closed while changing launch options.");
		QDir().mkpath(folder);
		QLockFile lock(folder + "/settings.lock");
		if (!lock.tryLock(0))
			throw std::runtime_error("Another capture setup is saving changes. Try again shortly.");
		QByteArray before = readFile(account.config);
		VdfDocument document(before);
		if (!document.find({"UserLocalConfigStore"}))
			throw std::runtime_error("This is not a recognized Steam account configuration.");
		const auto *node = document.find(launchPath(game.id));
		if (node && node->object)
			throw std::runtime_error("The game's launch options are not a string.");
		QByteArray current = node ? node->value : QByteArray();
		auto state = load();
		auto games = state.value("games").toObject();
		QString key = recordKey(account, game);
		auto item = games.value(key).toObject();
		QByteArray after;
		if (enable) {
			if (managed(account, game, current))
				return;
			QByteArray configured = configuredOptions(current);
			item = {{"original", QString::fromLatin1(current.toBase64())},
				{"hadOriginal", node != nullptr},
				{"configured", QString::fromLatin1(configured.toBase64())}};
			after = document.set(launchPath(game.id), configured);
			games.insert(key, item);
		} else {
			if (!item.contains("configured") || !item.contains("original") ||
			    !item.value("hadOriginal").isBool())
				throw std::runtime_error(
					"This launch option was not configured by this setup, so it cannot be restored automatically.");
			QByteArray configured = QByteArray::fromBase64(item.value("configured").toString().toLatin1());
			if (current != configured)
				throw std::runtime_error(
					"Launch options changed outside this setup. Existing edits were left untouched.");
			after = document.set(launchPath(game.id),
					     QByteArray::fromBase64(item.value("original").toString().toLatin1()),
					     !item.value("hadOriginal").toBool());
		}
		VdfDocument checked(after);
		(void)checked;
		QString backup =
			folder + "/backups/" +
			QString::fromLatin1(QCryptographicHash::hash(before, QCryptographicHash::Sha256).toHex()) +
			".vdf";
		if (!QFileInfo::exists(backup))
			saveFile(backup, before);
		// Save recovery metadata first. A crash before the config commit leaves the original intact.
		if (enable) {
			state.insert("games", games);
			save(state);
		}
		if (steamRunning(proc) || readFile(account.config) != before)
			throw std::runtime_error(
				"Steam/settings changed during setup. No launch options were overwritten; try again with Steam closed.");
		saveFile(account.config, after, QFileInfo(account.config).permissions());
		if (!enable) {
			games.remove(key);
			state.insert("games", games);
			save(state);
		}
	}
	bool capturedBefore(const QString &id) const { return load().value("observed").toObject().contains(id); }
	void markCaptured(const QString &id)
	{
		if (!numericId(id))
			return;
		QDir().mkpath(folder);
		QLockFile lock(folder + "/settings.lock");
		if (!lock.tryLock(0))
			return;
		auto state = load();
		auto observed = state.value("observed").toObject();
		if (observed.contains(id))
			return;
		observed.insert(id, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
		state.insert("observed", observed);
		save(state);
	}
	bool vulkanEnabled() const { return load().value("vulkanEnabled").toBool(); }
	bool steamAutomatic() const { return !load().value("steamAutomatic").toObject().isEmpty(); }
	void setSteamAutomatic(bool enable, QString source = {}, QString applications = {}, QString autostart = {},
			       const QString &proc = "/proc")
	{
		if (steamRunning(proc))
			throw std::runtime_error(
				"Close Steam completely, enable automatic capture, then open Steam normally.");
		QDir().mkpath(folder);
		QLockFile lock(folder + "/settings.lock");
		if (!lock.tryLock(0))
			throw std::runtime_error("Another capture setup is saving changes.");
		auto state = load();
		if (applications.isEmpty())
			applications =
				QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/applications";
		auto existing = state.value("steamAutomatic").toObject();
		if (enable && !existing.isEmpty())
			return;
		if (!enable && existing.isEmpty())
			return;
		struct ShortcutChange {
			QString path;
			QByteArray before, after;
			bool existed, remove;
		};
		std::vector<ShortcutChange> changes;
		QJsonObject record;
		if (enable) {
			if (applications.isEmpty())
				applications = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
					       "/applications";
			if (source.isEmpty()) {
				const QStringList candidates = QStandardPaths::locateAll(
					QStandardPaths::ApplicationsLocation, "steam.desktop");
				for (const auto &candidate : candidates) {
					QByteArray content = readFile(candidate);
					if (!content.contains("flatpak") && content.contains("Exec=") &&
					    content.contains("steam")) {
						source = candidate;
						break;
					}
				}
			}
			if (source.isEmpty())
				throw std::runtime_error(
					"The native Steam desktop shortcut was not found. Install native Steam before enabling this mode.");
			if (autostart.isEmpty()) {
				QString config = qEnvironmentVariable("CHERRIES_HOST_CONFIG_HOME");
				if (config.isEmpty())
					config = QDir::homePath() + "/.config";
				autostart = config + "/autostart/steam.desktop";
			}
			auto prepare = [&](const QString &path, const QByteArray &original) {
				if (original.contains("flatpak") || original.contains("cherries-gamecapture"))
					throw std::runtime_error(
						"The Steam shortcut already has a capture wrapper or uses Flatpak. It was left untouched.");
				QByteArray modified;
				int commands = 0;
				for (const auto &line : original.split('\n')) {
					if (line.startsWith("Exec=")) {
						modified += "Exec=/usr/bin/cherries-gamecapture " + line.mid(5);
						commands++;
					} else {
						modified += line;
					}
					modified += '\n';
				}
				if (original.endsWith('\n'))
					modified.chop(1);
				if (!commands)
					throw std::runtime_error("The Steam shortcut contains no launch command.");
				bool exists = QFileInfo::exists(path);
				changes.push_back(
					{path, exists ? readFile(path) : QByteArray(), modified, exists, false});
				record.insert(
					path,
					QJsonObject{{"original", QString::fromLatin1(changes.back().before.toBase64())},
						    {"configured", QString::fromLatin1(modified.toBase64())},
						    {"hadOriginal", exists}});
			};
			prepare(applications + "/steam.desktop", readFile(source));
			// Preserve an existing automatic Steam startup; never create a new autostart entry.
			if (QFileInfo::exists(autostart))
				prepare(autostart, readFile(autostart));
		} else {
			for (auto it = existing.begin(); it != existing.end(); ++it) {
				auto item = it.value().toObject();
				QByteArray current = readFile(it.key());
				if (current != QByteArray::fromBase64(item.value("configured").toString().toLatin1()))
					throw std::runtime_error(
						"The Steam shortcut changed outside setup. Existing edits were left untouched.");
				changes.push_back({it.key(), current,
						   QByteArray::fromBase64(item.value("original").toString().toLatin1()),
						   true, !item.value("hadOriginal").toBool()});
			}
		}
		if (steamRunning(proc))
			throw std::runtime_error("Steam started during setup. Close it and try again.");
		if (enable)
			saveFile(folder + "/steam-shortcuts-backup.json", QJsonDocument(record).toJson());
		for (const auto &change : changes) {
			if (QFileInfo::exists(change.path) != change.existed ||
			    (change.existed && readFile(change.path) != change.before))
				throw std::runtime_error(
					"The Steam shortcut changed during setup. No changes were made.");
		}
		try {
			for (const auto &change : changes) {
				if (change.remove) {
					if (!QFile::remove(change.path))
						throw std::runtime_error("Cannot restore the Steam shortcut.");
				} else {
					saveFile(change.path, change.after,
						 QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup |
							 QFile::ReadOther);
				}
			}
			if (enable)
				state.insert("steamAutomatic", record);
			else
				state.remove("steamAutomatic");
			save(state);
		} catch (...) {
			for (const auto &change : changes) {
				if (change.existed)
					saveFile(change.path, change.before,
						 QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup |
							 QFile::ReadOther);
				else if (QFileInfo::exists(change.path))
					QFile::remove(change.path);
			}
			throw;
		}
		// KDE's pinned launcher continues using steam.desktop, with its original name/icon.
		if (applications ==
		    QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/applications") {
			QString builder = QStandardPaths::findExecutable("kbuildsycoca6");
			if (!builder.isEmpty()) {
				QProcess process;
				auto environment = QProcessEnvironment::systemEnvironment();
				QString hostConfig = qEnvironmentVariable("CHERRIES_HOST_CONFIG_HOME");
				if (!hostConfig.isEmpty())
					environment.insert("XDG_CONFIG_HOME", hostConfig);
				process.setProcessEnvironment(environment);
				process.setProgram(builder);
				process.setArguments({"--noincremental"});
				(void)process.startDetached();
			}
		}
	}
	void setVulkan(bool enable, QString dataFolder = {}, QString sourceFolder = "/opt/cherries-obs")
	{
		if (dataFolder.isEmpty())
			dataFolder = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
		QDir().mkpath(folder);
		QLockFile lock(folder + "/settings.lock");
		if (!lock.tryLock(0))
			throw std::runtime_error("Another capture setup is saving changes.");
		auto state = load();
		const QString manifests = dataFolder + "/vulkan/implicit_layer.d";
		const QString libraries = dataFolder + "/cherries-obs/capture";
		struct ManifestChange {
			QString path;
			QByteArray before, after;
			bool existed;
		};
		std::vector<ManifestChange> changes;
		for (const auto &arch : {QString("64"), QString("32")}) {
			QString target = manifests + "/cherries_obs_vkcapture_" + arch + ".json";
			ManifestChange change{target, {}, {}, QFileInfo::exists(target)};
			if (change.existed) {
				change.before = readFile(target);
				auto existing =
					QJsonDocument::fromJson(change.before).object().value("layer").toObject();
				if (existing.value("name").toString() != "VK_LAYER_CHERRIES_vkcapture_" + arch)
					throw std::runtime_error(
						"An unrelated file occupies the automatic capture manifest path. It was left untouched.");
			}
			if (!enable) {
				changes.push_back(change);
				continue;
			}
			QString library = libraries + "/" + arch + "/libVkLayer_obs_vkcapture.so";
			QByteArray binary = readFile(sourceFolder + (arch == "64" ? "/lib64/" : "/lib/") +
						     "libVkLayer_obs_vkcapture.so");
			if (!QFileInfo::exists(library) || readFile(library) != binary)
				saveFile(library, binary);
			QJsonParseError error;
			auto document = QJsonDocument::fromJson(
				readFile(sourceFolder + "/share/vulkan/implicit_layer.d/obs_vkcapture_" + arch +
					 ".json"),
				&error);
			if (error.error != QJsonParseError::NoError || !document.isObject())
				throw std::runtime_error("Installed Vulkan capture manifest is invalid.");
			auto object = document.object();
			auto layer = object.value("layer").toObject();
			if (layer.value("name").toString() != "VK_LAYER_CHERRIES_vkcapture_" + arch)
				throw std::runtime_error(
					"Installed Vulkan capture layer is not the expected Cherries layer.");
			layer.remove("enable_environment");
			layer.insert("library_path", library);
			object.insert("layer", layer);
			change.after = QJsonDocument(object).toJson();
			changes.push_back(change);
		}
		try {
			for (const auto &change : changes) {
				if (enable)
					saveFile(change.path, change.after);
				else if (change.existed && !QFile::remove(change.path))
					throw std::runtime_error("Cannot remove the automatic capture manifest.");
			}
			state.insert("vulkanEnabled", enable);
			save(state);
		} catch (...) {
			// A failed second architecture/settings commit must not leave a half-enabled setup.
			for (const auto &change : changes) {
				if (change.existed)
					saveFile(change.path, change.before);
				else if (QFileInfo::exists(change.path))
					QFile::remove(change.path);
			}
			throw;
		}
	}
};
} // namespace CherriesCapture
