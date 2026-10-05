#pragma once
#include "CherriesYouTubeProtocol.hpp"
#include "CherriesYouTubeBundled.hpp"
#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>

inline CherriesYouTubeCredentials CherriesYouTubeClient(QWidget *owner = nullptr, bool ask = false)
{
	for (config_t *config : {OBSBasic::Get()->Config(), App()->GetUserConfig()}) {
		const char *id = config_get_string(config, "CherriesYouTube", "ClientId");
		const char *secret = config_get_string(config, "CherriesYouTube", "ClientSecret");
		CherriesYouTubeCredentials client{QString::fromUtf8(id ? id : ""),
						  QString::fromUtf8(secret ? secret : "")};
		if (client.valid())
			return client;
	}
	const auto bundled = CherriesParseYouTubeClient(QByteArray(CherriesBundledYouTubeJSON));
	if (bundled.valid() || !ask)
		return bundled;
	QMessageBox::information(
		owner, "Connect YouTube",
		"This build needs a Google Desktop application OAuth client. Enable the YouTube Data API v3 in your "
		"Google Cloud project, create a Desktop app OAuth client, and download its JSON file. "
		"Select that file next. OBS will remember it after you connect. "
		"If your project is in testing, add your Google account as a test user.");
	const QString path =
		QFileDialog::getOpenFileName(owner, "Import Google Desktop OAuth client", {}, "JSON (*.json)");
	if (path.isEmpty())
		return {};
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
		QMessageBox::warning(owner, "Connect YouTube", "Could not read the OAuth client JSON file.");
		return {};
	}
	const auto imported = CherriesParseYouTubeClient(file.readAll());
	if (!imported.valid())
		QMessageBox::warning(owner, "Connect YouTube", "Select the JSON file for a Desktop app OAuth client.");
	return imported;
}

inline void CherriesSaveYouTubeClient(const CherriesYouTubeCredentials &client)
{
	for (config_t *config : {OBSBasic::Get()->Config(), App()->GetUserConfig()}) {
		config_set_string(config, "CherriesYouTube", "ClientId", client.id.toUtf8().constData());
		config_set_string(config, "CherriesYouTube", "ClientSecret", client.secret.toUtf8().constData());
		config_save_safe(config, "tmp", nullptr);
	}
}
