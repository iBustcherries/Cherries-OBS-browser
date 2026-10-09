#!/usr/bin/env python3
"""Keep additional YouTube accounts and broadcast dialogs isolated from OBS's primary service."""
from pathlib import Path
import shutil
import sys

root = Path(sys.argv[1])
here = Path(__file__).resolve().parent

def change(relative, old, new, count=1):
    path = root / relative
    text = path.read_text()
    assert text.count(old) == count, (relative, old[:90], text.count(old))
    path.write_text(text.replace(old, new))

change('frontend/widgets/OBSBasic.hpp', '\tvoid CherriesManageBroadcast();',
       '\tvoid CherriesManageBroadcast();\n\tQVariantMap CherriesYouTubeDestination(QVariantMap request);')
with (root / 'frontend/widgets/OBSBasic.hpp').open('r+') as f:
    text = f.read(); f.seek(0); f.write('#include <QVariantMap>\n' + text)
shutil.copyfile(here / 'CherriesYouTubeDestinations.inc', root / 'frontend/widgets/CherriesYouTubeDestinations.inc')
with (root / 'frontend/widgets/CherriesBroadcast.inc').open('a') as f:
    f.write('\n#include "CherriesYouTubeDestinations.inc"\n')

change('frontend/oauth/YoutubeAuth.hpp', '#include "OAuth.hpp"', '#include "OAuth.hpp"\n#include <QVariantMap>')
change('frontend/oauth/YoutubeAuth.hpp', '\tvoid ReloadChat();', '''\tvoid ReloadChat();
    QVariantMap CherriesCredentials() const;
    void CherriesLoadCredentials(const QVariantMap &credentials);''')
with (root / 'frontend/oauth/YoutubeAuth.cpp').open('a') as f:
    f.write('''
QVariantMap YoutubeAuth::CherriesCredentials() const
{
    return {{"refresh_token", QString::fromStdString(refresh_token)},
            {"token", QString::fromStdString(token)},
            {"expire_time", QVariant::fromValue<qulonglong>(expire_time)},
            {"scope_version", currentScopeVer}};
}
void YoutubeAuth::CherriesLoadCredentials(const QVariantMap &credentials)
{
    refresh_token = credentials.value("refresh_token").toString().toStdString();
    token = credentials.value("token").toString().toStdString();
    expire_time = credentials.value("expire_time").toULongLong();
    currentScopeVer = credentials.value("scope_version").toInt();
    firstLoad = false;
}
''')

h = 'frontend/dialogs/OBSYoutubeActions.hpp'
c = 'frontend/dialogs/OBSYoutubeActions.cpp'
change(h, 'bool broadcastReady);', 'bool broadcastReady, bool destinationMode = false, QStringList blockedBroadcasts = {});')
change(h, '\tbool broadcastReady = false;', '\tbool broadcastReady = false;\n\tbool destinationMode = false;\n\tQStringList blockedBroadcasts;')
change(c, 'OBSYoutubeActions::OBSYoutubeActions(QWidget *parent, Auth *auth, bool broadcastReady)',
       'OBSYoutubeActions::OBSYoutubeActions(QWidget *parent, Auth *auth, bool broadcastReady, bool destinationMode, QStringList blockedBroadcasts)')
change(c, '\t  broadcastReady(broadcastReady)', '\t  broadcastReady(broadcastReady),\n\t  destinationMode(destinationMode),\n\t  blockedBroadcasts(blockedBroadcasts)')
change(c, '\tui->setupUi(this);', '''\tui->setupUi(this);
    if (destinationMode) {
        ui->okButton->hide();
        ui->saveButton->setText("Apply to destination");
        ui->checkRememberSettings->setChecked(false);
        ui->checkRememberSettings->hide();
    }''')
change(c, '\tif (rememberSettings) {', '\tif (!destinationMode && rememberSettings) {')
change(c, '\tif (ui->checkRememberSettings->isChecked()) {', '\tif (!destinationMode && ui->checkRememberSettings->isChecked()) {')
change(c, '\tif (OBSBasic::Get()->GetYouTubeAppDock()) {', '\tif (!destinationMode && OBSBasic::Get()->GetYouTubeAppDock()) {', count=2)
change(c, '\t\tif (broadcast.privacy != "private") {', '\t\tif (!destinationMode && broadcast.privacy != "private") {')
# The cloned account has no chat UI. Avoid even reset requests in this mode.
change(c, '\tif (broadcastPrivacy != "private") {', '\tif (!destinationMode && broadcastPrivacy != "private") {')
change(c, 'void OBSYoutubeActions::InitBroadcast()\n{', 'void OBSYoutubeActions::InitBroadcast()\n{\n\tif (destinationMode) { ReadyBroadcast(); return; }')
change(c, 'void OBSYoutubeActions::ReadyBroadcast()\n{',
       'void OBSYoutubeActions::ReadyBroadcast()\n{\n\tif (destinationMode) { workerThread->stop(); workerThread->wait(); }')
change(c, '\t\tui->pushButton->setVisible(true);\n\t}\n}',
       '\t\tui->pushButton->setVisible(true);\n\t}\n\tif (destinationMode) ui->saveButton->setText("Apply to destination");\n}')
needle = '\t\t      const QString &status, bool astart, bool astop) {'
# Exact whitespace in this callback is upstream-specific; fail if it changes.
path = root / c
text = path.read_text()
start = text.index('[&](const QString &title, const QString &dateTimeString, const QString &broadcast,')
brace = text.index('{', start)
text = text[:brace+1] + '\n\t\t\tif (this->destinationMode && this->blockedBroadcasts.contains(broadcast)) return;' + text[brace+1:]
path.write_text(text)

# Persist newly selected portrait bindings immediately, not just on shutdown.
change('plugins/vertical-canvas/vertical-canvas.cpp', '\t\t\tit->UpdateMulti();\n\t\t}',
       '\t\t\tit->UpdateMulti();\n\t\t\tsave_canvas();\n\t\t}')

change('plugins/aitum-multistream/output-dialog.cpp', '#include <QDialog>', '#include <QDialog>\n#include <QUrl>')
change('plugins/aitum-multistream/output-dialog.cpp', '} else if (outputKey.isEmpty()) {',
       '} else if (outputKey.isEmpty() && !QUrl(outputServer).host().endsWith(".youtube.com", Qt::CaseInsensitive)) {')
change('plugins/aitum-multistream/output-dialog.cpp', '\tformLayout->addWidget(generateInfoLabel("YouTubeStreamKeyInfo"));',
       '\tauto hint = new QLabel("Leave the key blank to connect your YouTube account and choose a broadcast through Manage Broadcast after saving this destination.");\n\thint->setWordWrap(true);\n\tformLayout->addWidget(hint);')
change('plugins/aitum-multistream/multistream.cpp', 'bool MultistreamDock::StartOutput(obs_data_t *settings, QPushButton *streamButton)\n{',
       'bool MultistreamDock::StartOutput(obs_data_t *settings, QPushButton *streamButton)\n{\n\tif (!CherriesCanStart(settings)) return false;')
change('plugins/aitum-multistream/multistream.cpp', '[this, streamButton, output_name] {',
       '[this, streamButton, output_name, output_data] {\n\t\t\tif (streamButton->isChecked() && !CherriesCanStart(output_data)) { streamButton->setChecked(false); return; }')

change('frontend/utility/YoutubeApiWrappers.hpp', '\tQString GetLastError()',
       '\tbool CherriesUpdateBroadcast(const json11::Json &broadcast, const QVariantMap &values);\n\tQString GetLastError()')
with (root / 'frontend/utility/YoutubeApiWrappers.cpp').open('a') as f:
    f.write('''
bool YoutubeApiWrappers::CherriesUpdateBroadcast(const json11::Json &broadcast, const QVariantMap &values)
{
    // Updating a part replaces its mutable fields. Preserve fields the form
    // does not edit, and leave contentDetails/monetization completely alone.
    Json::object snippet;
    for (const char *key : {"title", "description", "categoryId", "scheduledStartTime", "scheduledEndTime"})
        if (!broadcast["snippet"][key].is_null()) snippet[key] = broadcast["snippet"][key];
    snippet["title"] = values.value("title").toString().toStdString();
    snippet["description"] = values.value("description").toString().toStdString();
    snippet["scheduledStartTime"] = values.value("scheduled_start").toString().toStdString();
    Json::object status{{"privacyStatus", values.value("privacy").toString().toStdString()}};
    if (!broadcast["status"]["selfDeclaredMadeForKids"].is_null())
        status["selfDeclaredMadeForKids"] = broadcast["status"]["selfDeclaredMadeForKids"];
    const Json body = Json::object{{"id", broadcast["id"]}, {"snippet", snippet}, {"status", status}};
    Json result;
    return InsertCommand("https://www.googleapis.com/youtube/v3/liveBroadcasts?part=snippet,status",
                         "application/json", "PUT", body.dump().c_str(), result);
}
''')
