#!/usr/bin/env python3
"""Integrate pinned GPL engines; fail closed if an expected patch point changes."""
from pathlib import Path
import shutil
import sys
import subprocess

root = Path(sys.argv[1])
here = Path(__file__).resolve().parent
vertical = root / 'plugins/vertical-canvas'
multi = root / 'plugins/aitum-multistream'

def change(path, old, new, count=1):
    text = path.read_text()
    assert text.count(old) == count, (path, old[:80], text.count(old))
    path.write_text(text.replace(old, new))

# A real Qt slot bridges the plugin to the current native account. The upstream
# BroadcastButtonClicked method is private and not invokable through Qt metadata.
change(root / 'frontend/widgets/OBSBasic.hpp', '\tvoid SetupBroadcast();',
       '\tvoid SetupBroadcast();\n\tvoid CherriesManageBroadcast();')
shutil.copyfile(here / 'CherriesBroadcast.inc', root / 'frontend/widgets/CherriesBroadcast.inc')
with (root / 'frontend/widgets/OBSBasic_YouTube.cpp').open('a') as f:
    f.write('\n#include "CherriesBroadcast.inc"\n')
change(root / 'frontend/oauth/TwitchAuth.hpp', '\tvoid LoadSecondaryUIPanes();',
       '\tvoid LoadSecondaryUIPanes();\n\tvoid CherriesShowStreamInfo();')
with (root / 'frontend/oauth/TwitchAuth.cpp').open('a') as f:
    f.write('''
void TwitchAuth::CherriesShowStreamInfo()
{
    if (!uiLoaded) LoadUI();
    if (cef && uiLoaded) LoadSecondaryUIPanes();
}
''')

for path in (vertical, multi):
    shutil.copyfile(here / 'engine.cmake', path / 'CMakeLists.txt')
    # Fork updates ship through the Cherries RPM; retain upstream attribution.
    main = path / ('vertical-canvas.cpp' if path == vertical else 'multistream.cpp')
    text = main.read_text()
    start = text.index('\tstd::string url = "https://api.aitum.tv/')
    end = text.index(';', text.index('update_info_create_single(', start)) + 1
    main.write_text(text[:start] + '\t// Cherries builds are updated through their RPM distribution.\n' + text[end:])

change(vertical / 'vertical-canvas.cpp', 'void CanvasDock::MainSceneChanged()\n{',
       'void CanvasDock::MainSceneChanged()\n{\n\tif (!config_get_bool(obs_frontend_get_profile_config(), "CherriesStudio", "LinkScenes")) return;')
change(vertical / 'vertical-canvas.cpp', '\t\t\tcd->AskUpdate();', '\t\t\t// Update notices are provided by the Cherries distribution.')
change(vertical / 'vertical-canvas.cpp', 'bool obs_module_load(void)\n{',
       'bool obs_module_load(void)\n{\n\tconfig_set_default_bool(obs_frontend_get_profile_config(), "CherriesStudio", "LinkScenes", true);')
change(vertical / 'vertical-canvas.hpp', 'class CanvasDock : public QFrame {\n\tQ_OBJECT',
       'class CanvasDock : public QFrame {\n\tQ_OBJECT\n\tQ_PROPERTY(uint cherriesWidth READ GetCanvasWidth)\n\tQ_PROPERTY(uint cherriesHeight READ GetCanvasHeight)')
change(vertical / 'vertical-canvas.hpp', '\tvoid ConfigButtonClicked();', '\tvoid ConfigButtonClicked();\n\tvoid CherriesCopyMain();')
# Refreshing the scene list must not insert the same audio wrapper into another channel.
change(vertical / 'vertical-canvas.cpp', '''void CanvasDock::LoadScenes()
{
\tclearing = false;
\tswitching = false;
\tfor (uint32_t i = MAX_CHANNELS - 1; i > 0; i--) {
\t\tauto s = obs_get_output_source(i);''', '''void CanvasDock::LoadScenes()
{
\tclearing = false;
\tswitching = false;
\tbool wrapperPresent = false;
\tfor (uint32_t i = 1; i < MAX_CHANNELS; ++i) {
\t\tauto s = obs_get_output_source(i);
\t\twrapperPresent = wrapperPresent || s == transitionAudioWrapper;
\t\tobs_source_release(s);
\t}
\tfor (uint32_t i = MAX_CHANNELS - 1; !wrapperPresent && i > 0; i--) {
\t\tauto s = obs_get_output_source(i);''')
with (vertical / 'vertical-canvas.cpp').open('a') as f:
    f.write('''
void CanvasDock::CherriesCopyMain()
{
    auto source = obs_frontend_get_current_scene();
    auto original = source ? obs_scene_from_source(source) : nullptr;
    if (!original || !canvas) { obs_source_release(source); return; }
    QString base = QString::fromUtf8(obs_source_get_name(source)) + " Portrait";
    QString name = base;
    for (int n = 2;; ++n) {
        auto existing = obs_canvas_get_source_by_name(canvas, name.toUtf8().constData());
        if (!existing) break;
        obs_source_release(existing); name = base + " " + QString::number(n);
    }
    auto duplicate = obs_scene_duplicate(original, name.toUtf8().constData(), OBS_SCENE_DUP_REFS);
    if (duplicate) {
        obs_canvas_move_scene(duplicate, canvas);
        name = QString::fromUtf8(obs_source_get_name(obs_scene_get_source(duplicate)));
        LoadScenes(); SwitchScene(name, false); SetLinkedScene(source, name);
        obs_scene_release(duplicate); obs_frontend_save(); save_canvas();
    }
    obs_source_release(source);
}
''')

shutil.copyfile(here / 'CherriesDestinations.inc', multi / 'CherriesDestinations.inc')
change(multi / 'multistream.hpp', '#include <QFrame>', '#include <QFrame>\n#include <QCheckBox>\n#include <QPointer>\n#include <QGroupBox>\n#include <QVariantMap>')
change(multi / 'multistream.hpp', 'private:\n', '''private:
    void CherriesHeader();
    void CherriesCard(QGroupBox *, obs_data_t *, bool, QPushButton *);
    void CherriesStartSelected();
    void CherriesStartExtras();
    void CherriesBroadcastMenu();
    bool CherriesCanStart(obs_data_t *);
    void CherriesManageYouTube(obs_data_t *, bool, const QString &);
    obs_output_t *CherriesGetOutput(obs_data_t *, bool);
    QVariantMap CherriesBindingRequest(obs_data_t *);
    Q_INVOKABLE bool CherriesApplyYouTubeBinding(QString, bool, QVariantMap);
    bool cherriesWaiting = false;
    QPointer<QCheckBox> cherriesMainSelected;
''', count=2)
# The replacement above also lands in the small pixmap helper; remove those declarations there.
p = multi / 'multistream.hpp'
s = p.read_text(); marker = s.index('class AspectRatioPixmapLabel')
s = s[:marker] + s[marker:].replace('''    void CherriesHeader();
    void CherriesCard(QGroupBox *, obs_data_t *, bool, QPushButton *);
    void CherriesStartSelected();
    void CherriesStartExtras();
    void CherriesBroadcastMenu();
    bool CherriesCanStart(obs_data_t *);
    void CherriesManageYouTube(obs_data_t *, bool, const QString &);
    obs_output_t *CherriesGetOutput(obs_data_t *, bool);
    QVariantMap CherriesBindingRequest(obs_data_t *);
    Q_INVOKABLE bool CherriesApplyYouTubeBinding(QString, bool, QVariantMap);
    bool cherriesWaiting = false;
    QPointer<QCheckBox> cherriesMainSelected;
''', '')
p.write_text(s)
change(multi / 'multistream.cpp', '#include <QScrollArea>', '#include <QScrollArea>\n#include <QElapsedTimer>\n#include <QDockWidget>')
change(multi / 'multistream.cpp', '\tbutton->setIcon(button->isChecked() ? streamActiveIcon : streamInactiveIcon);',
       '\tbutton->setIcon(button->isChecked() ? streamActiveIcon : streamInactiveIcon);\n\tbutton->setText(button->isChecked() ? "Stop" : "Start");')
change(multi / 'multistream.cpp', '\tmainStreamGroup->setLayout(mainStreamLayout);',
       '\tmainStreamGroup->setLayout(mainStreamLayout);\n\tCherriesCard(mainStreamGroup, nullptr, false, mainStreamButton);')
change(multi / 'multistream.cpp', '\tstreamGroup->setLayout(streamLayout);',
       '\tstreamGroup->setLayout(streamLayout);\n\tCherriesCard(streamGroup, output_data, vertical, streamButton);')
change(multi / 'multistream.cpp', '\tLoadSettingsFile();\n}', '\tLoadSettingsFile();\n\tCherriesHeader();\n}')
change(multi / 'multistream.cpp', '\t\tmd->finished_loading = true;', '\t\tmd->finished_loading = true;\n\t\tQTimer::singleShot(0, md, [md] { md->LoadVerticalOutputs(false); });')
change(multi / 'multistream.cpp', '} else if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGING || event == OBS_FRONTEND_EVENT_PROFILE_RENAMED) {',
       '} else if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGING || event == OBS_FRONTEND_EVENT_PROFILE_RENAMED) {\n\t\tmd->cherriesWaiting = false;')
change(multi / 'multistream.cpp', 'if (cn == "QPushButton") {',
       'if (cn == "QPushButton" && c->objectName() == "canvasStream") {', count=3)
change(multi / 'multistream.cpp', '\tconfigButton->setMinimumHeight(30);',
       '\tconfigButton->setObjectName("cherriesOutputSettings");\n\tconfigButton->setText("Add / Edit Destinations");\n\tconfigButton->setMinimumHeight(30);')
change(multi / 'multistream.cpp', '\t\tmd->mainStreamButton->setIcon(md->streamActiveIcon);',
       '\t\tmd->mainStreamButton->setIcon(md->streamActiveIcon);\n\t\tif (event == OBS_FRONTEND_EVENT_STREAMING_STARTED && md->cherriesWaiting) { md->cherriesWaiting = false; md->CherriesStartExtras(); }')
with (multi / 'multistream.cpp').open('a') as f:
    f.write('\n#include "CherriesDestinations.inc"\n')
    f.write('\n#include "CherriesYouTubeOutputs.inc"\n')
shutil.copyfile(here / 'CherriesYouTubeOutputs.inc', multi / 'CherriesYouTubeOutputs.inc')

change(multi / 'multistream.cpp', 'auto outputPlatformIconSize = 36;', 'auto outputPlatformIconSize = 24;')
# Compact destinations and a preview-only portrait canvas. Keep backing controls
# alive under a hidden parent for hotkeys and Capture & Clips actions.
for path, start_marker, end_marker in [
    (multi / 'multistream.cpp', '\t// Contribute Button', '\tbuttonRow->addWidget(aitumButton);'),
    (vertical / 'vertical-canvas.cpp', '\tauto aitumButtonGroupLayout =', '\tbuttonRow->addLayout(aitumButtonGroupLayout);')]:
    text = path.read_text()
    start = text.index(start_marker)
    end = text.index(end_marker, start) + len(end_marker)
    path.write_text(text[:start] + text[end:])
change(vertical / 'vertical-canvas.cpp', '\tauto buttonRow = new QHBoxLayout(this);',
       '\tauto hiddenControls = new QWidget(this);\n\thiddenControls->setObjectName("cherriesPortraitControls");\n\tauto buttonRow = new QHBoxLayout(hiddenControls);')
change(vertical / 'vertical-canvas.cpp', '\tmainLayout->addLayout(buttonRow);', '\thiddenControls->hide();')
change(vertical / 'vertical-canvas.cpp', '\tl->addWidget(enablePreviewButton);', '''\tl->addWidget(enablePreviewButton);
    enablePreviewButton->hide();
    auto disabledHint = new QLabel("Preview disabled — right-click to enable");
    disabledHint->setAlignment(Qt::AlignCenter);
    l->addWidget(disabledHint);
    previewDisabledWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(previewDisabledWidget, &QWidget::customContextMenuRequested, this,
        [this, enablePreviewButton](const QPoint &point) {
            QMenu menu(previewDisabledWidget);
            menu.addAction("Enable preview", enablePreviewButton, &QPushButton::click);
            menu.exec(previewDisabledWidget->mapToGlobal(point));
        });''')
change(vertical / 'vertical-canvas.cpp', '\tconst auto sceneRow = new QHBoxLayout(this);',
       '\tauto hiddenScenes = new QWidget(this);\n\tconst auto sceneRow = new QHBoxLayout(hiddenScenes);')
change(vertical / 'vertical-canvas.cpp', '\tmainLayout->insertLayout(0, sceneRow);', '\thiddenScenes->hide();')
for label in ('mainCanvasLabel', 'verticalCanvasLabel'):
    change(multi / 'multistream.cpp', f'\t{label}->setStyleSheet(canvasGroupHeaderStyle);',
           f'\t{label}->hide();')
change(multi / 'multistream.cpp', 'configButton->setText("Add / Edit Destinations");',
       'configButton->setText("Add / Edit");')

# User-facing names; internal module/procedure IDs stay compatible with the engines.
for path, names in [(vertical, {'Vertical':'Portrait', 'VerticalSettings':'Portrait Settings', 'Backtrack':'Replay Clips'}),
                    (multi, {'AitumMultistream':'Destinations', 'AitumMultistreamSettings':'Destination Settings',
                             'BuiltinStream':'OBS Account', 'MainCanvas':'Landscape', 'VerticalCanvas':'Portrait'})]:
    p = path / 'data/locale/en-US.ini'
    lines = p.read_text().splitlines()
    p.write_text('\n'.join(f'{line.split("=",1)[0]}="{names[line.split("=",1)[0]]}"' if line.split('=',1)[0] in names else line for line in lines)+'\n')

studio = root / 'plugins/cherries-studio'
studio.mkdir(exist_ok=True)
for name in ('CMakeLists.txt', 'CherriesStudio.cpp'):
    shutil.copyfile(here / name, studio / name)
with (root / 'plugins/CMakeLists.txt').open('a') as f:
    f.write('\nadd_subdirectory(vertical-canvas)\nadd_subdirectory(aitum-multistream)\nadd_subdirectory(cherries-studio)\nset_obs_core_modules()\n')

subprocess.run([sys.executable, str(here / 'integrate-youtube.py'), str(root)], check=True)
