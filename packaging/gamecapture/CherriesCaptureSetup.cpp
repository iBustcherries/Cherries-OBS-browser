/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "CherriesCaptureSetup.hpp"
#include "CherriesSteamCapture.hpp"
#include <obs.h>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cstring>

namespace {
using namespace CherriesCapture;
QSet<QString> currentCaptures;

QString steamIdForPid(qint64 pid)
{
	if (pid <= 0)
		return {};
	QFile environment(QString("/proc/%1/environ").arg(pid));
	if (!environment.open(QIODevice::ReadOnly))
		return {};
	const auto entries = environment.read(128 * 1024).split('\0');
	for (const auto &key : {QByteArray("SteamAppId="), QByteArray("SteamGameId=")}) {
		for (const auto &entry : entries) {
			if (!entry.startsWith(key))
				continue;
			QString value = QString::fromLatin1(entry.mid(key.size()));
			if (numericId(value))
				return value;
		}
	}
	return {};
}

void observeCaptures()
{
	currentCaptures.clear();
	obs_enum_sources(
		[](void *, obs_source_t *source) {
			if (strcmp(obs_source_get_id(source), "vkcapture-source") != 0)
				return true;
			calldata_t data;
			calldata_init(&data);
			if (proc_handler_call(obs_source_get_proc_handler(source), "get_capture_state", &data) &&
			    calldata_bool(&data, "capturing")) {
				QString id = steamIdForPid(calldata_int(&data, "pid"));
				if (!id.isEmpty())
					currentCaptures.insert(id);
			}
			calldata_free(&data);
			return true;
		},
		nullptr);
	for (const auto &id : currentCaptures) {
		try {
			SteamCaptureStore().markCaptured(id);
		} catch (const std::exception &) { /* Capture continues even if history cannot be saved. */
		}
	}
}

class CaptureSetup : public QDialog {
	QString root;
	SteamCaptureStore store;
	std::vector<SteamAccount> accounts;
	std::vector<SteamGame> games;
	QComboBox *account;
	QLabel *steamStatus;
	QLabel *notice;
	QCheckBox *vulkan;
	QCheckBox *automaticSteam;
	QLineEdit *search;
	QTableWidget *table;
	QString selectedAccount;
	void error(const std::exception &problem)
	{
		QMessageBox::warning(this, "Game Capture Setup", QString::fromUtf8(problem.what()));
	}
	void populate()
	{
		table->setRowCount(0);
		if (account->currentIndex() < 0 || static_cast<size_t>(account->currentIndex()) >= accounts.size())
			return;
		const auto &selected = accounts[static_cast<size_t>(account->currentIndex())];
		selectedAccount = selected.id;
		VdfDocument config(readFile(selected.config));
		bool running = steamRunning();
		bool automatic = store.steamAutomatic();
		table->setColumnHidden(2, automatic);
		steamStatus->setText(running ? "Steam: Running — close Steam to change game setup" : "Steam: Closed");
		QString filter = search->text().trimmed();
		for (const auto &game : games) {
			if (!game.name.contains(filter, Qt::CaseInsensitive))
				continue;
			const auto *option = config.find(launchPath(game.id));
			QByteArray value = option && !option->object ? option->value : QByteArray();
			bool managed = store.managed(selected, game, value);
			bool manual = value.contains("cherries-gamecapture") && !managed;
			bool changed = !store.record(selected, game).isEmpty() && !managed && !manual;
			QString status = managed                ? "Configured"
					 : manual               ? "Enabled outside setup"
					 : changed              ? "Options changed"
					 : game.nativeDeadCells ? "Not configured"
								: "Not checked";
			if (automatic)
				status = "Automatic enabled · Not checked";
			if (currentCaptures.contains(game.id))
				status = "Capturing now";
			else if (store.capturedBefore(game.id))
				status += " · Captured before";
			int row = table->rowCount();
			table->insertRow(row);
			auto *name = new QTableWidgetItem(
				game.name + (game.nativeDeadCells ? "\nNative Dead Cells profile available" : ""));
			name->setToolTip(
				game.nativeDeadCells
					? "Handles the native Linux launcher's cleared capture hook. Does not change Steam's Proton selection."
					: "Graphics API has not been inferred from the game name. Enable setup if automatic Vulkan capture does not connect.");
			table->setItem(row, 0, name);
			table->setItem(row, 1, new QTableWidgetItem(status));
			auto *button = new QPushButton(managed  ? "Disable capture"
						       : manual ? "Already enabled"
								: "Enable capture",
						       table);
			button->setEnabled(!running && !manual);
			connect(button, &QPushButton::clicked, this, [this, selected, game, managed]() {
				try {
					store.configure(selected, game, !managed);
					notice->setText(
						managed ? "Original launch options restored. Restart the game."
							: "Capture configured. Open Steam normally and restart the game. Configuration is not a capture test.");
					populate();
				} catch (const std::exception &problem) {
					error(problem);
				}
			});
			table->setCellWidget(row, 2, button);
			table->setRowHeight(row, game.nativeDeadCells ? 60 : 44);
		}
	}
	void refresh()
	{
		try {
			root = nativeSteamRoot();
			if (root.isEmpty()) {
				notice->setText(
					"Native Steam was not found. This setup currently supports RPM/native Steam, including its additional library folders.");
				return;
			}
			accounts = steamAccounts(root);
			games = installedGames(root);
			QSignalBlocker blocker(account);
			account->clear();
			int index = 0;
			for (const auto &entry : accounts) {
				if (entry.id == selectedAccount)
					index = account->count();
				account->addItem(entry.name + " (" + entry.id + ")");
			}
			if (!accounts.empty())
				account->setCurrentIndex(index);
			if (accounts.empty())
				notice->setText("Sign in to Steam once, then close Steam and refresh games.");
			else if (games.empty())
				notice->setText(
					"No installed games found. Mount any external game libraries, then refresh.");
			populate();
		} catch (const std::exception &problem) {
			error(problem);
		}
	}

public:
	explicit CaptureSetup(QWidget *parent) : QDialog(parent)
	{
		setWindowTitle("Game Capture Setup");
		resize(850, 560);
		auto *layout = new QVBoxLayout(this);
		automaticSteam = new QCheckBox("Enable automatic capture through normal Steam", this);
		layout->addWidget(automaticSteam);
		auto *automaticHelp = new QLabel(
			"One-time setup: close Steam, enable this option, then open your usual Steam shortcut. Compatible games connect automatically when started, including game audio. No special launcher or per-game setup is needed.",
			this);
		automaticHelp->setWordWrap(true);
		layout->addWidget(automaticHelp);
		try {
			automaticSteam->setChecked(store.steamAutomatic());
		} catch (const std::exception &problem) {
			error(problem);
		}
		connect(automaticSteam, &QCheckBox::toggled, this, [this](bool enabled) {
			try {
				store.setSteamAutomatic(enabled);
				notice->setText(
					enabled ? "Automatic capture enabled. Open your normal Steam shortcut, then start the game. Keep a Game Capture source in your OBS scene."
						: "Your original Steam shortcuts have been restored.");
				populate();
			} catch (const std::exception &problem) {
				QSignalBlocker blocker(automaticSteam);
				automaticSteam->setChecked(!enabled);
				error(problem);
			}
		});
		vulkan = new QCheckBox("Enable automatic Vulkan capture", this);
		layout->addWidget(vulkan);
		auto *vulkanHelp = new QLabel(
			"Launch compatible Vulkan/Proton games normally. Restart games already running when enabled.",
			this);
		vulkanHelp->setWordWrap(true);
		layout->addWidget(vulkanHelp);
		try {
			vulkan->setChecked(store.vulkanEnabled());
		} catch (const std::exception &problem) {
			error(problem);
		}
		connect(vulkan, &QCheckBox::toggled, this, [this](bool enabled) {
			try {
				store.setVulkan(enabled);
			} catch (const std::exception &problem) {
				QSignalBlocker blocker(vulkan);
				vulkan->setChecked(!enabled);
				error(problem);
			}
		});
		auto *heading = new QLabel("Installed Steam games", this);
		QFont font = heading->font();
		font.setBold(true);
		font.setPointSize(font.pointSize() + 3);
		heading->setFont(font);
		layout->addWidget(heading);
		auto *help = new QLabel(
			"Enable capture once, then launch the game normally through Steam. Steam must be closed while applying changes.",
			this);
		help->setWordWrap(true);
		layout->addWidget(help);
		auto *accountRow = new QHBoxLayout();
		accountRow->addWidget(new QLabel("Steam account:", this));
		account = new QComboBox(this);
		accountRow->addWidget(account, 1);
		layout->addLayout(accountRow);
		steamStatus = new QLabel(this);
		layout->addWidget(steamStatus);
		search = new QLineEdit(this);
		search->setPlaceholderText("Search installed Steam games");
		layout->addWidget(search);
		table = new QTableWidget(0, 3, this);
		table->setHorizontalHeaderLabels({"Game", "Capture status", "Action"});
		table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		table->setSelectionBehavior(QAbstractItemView::SelectRows);
		table->verticalHeader()->hide();
		table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
		table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
		table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
		layout->addWidget(table, 1);
		notice = new QLabel(
			"This updates Steam launch options for you and keeps their original value for Disable capture. Unfamiliar games remain Not checked until tested.",
			this);
		notice->setWordWrap(true);
		layout->addWidget(notice);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
		auto *reload = buttons->addButton("Refresh games", QDialogButtonBox::ActionRole);
		connect(reload, &QPushButton::clicked, this, [this]() { refresh(); });
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
		layout->addWidget(buttons);
		connect(account, &QComboBox::currentIndexChanged, this, [this]() {
			try {
				populate();
			} catch (const std::exception &problem) {
				error(problem);
			}
		});
		connect(search, &QLineEdit::textChanged, this, [this]() {
			try {
				populate();
			} catch (const std::exception &problem) {
				error(problem);
			}
		});
		auto *timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, [this]() {
			try {
				populate();
			} catch (const std::exception &) { /* Refresh will show any persistent error. */
			}
		});
		timer->start(2500);
		refresh();
	}
};
} // namespace

void CherriesInstallCaptureSetup(QMenu *menu, QWidget *owner)
{
	menu->addSeparator();
	auto *action = menu->addAction("Game Capture Setup...");
	QObject::connect(action, &QAction::triggered, owner, [owner]() {
		CaptureSetup dialog(owner);
		dialog.exec();
	});
	try {
		SteamCaptureStore store;
		if (store.vulkanEnabled())
			store.setVulkan(true); // Refresh owned hook copies after RPM upgrades.
	} catch (const std::exception &problem) {
		blog(LOG_WARNING, "Automatic Vulkan capture setup: %s", problem.what());
	}
	auto *timer = new QTimer(owner);
	QObject::connect(timer, &QTimer::timeout, owner, []() { observeCaptures(); });
	timer->start(2500);
}
