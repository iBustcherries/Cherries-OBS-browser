"""Brand the pinned OBS frontend while retaining configuration and source identity."""
from pathlib import Path
import base64
import hashlib
import shutil
import sys

root = Path(sys.argv[1])
here = Path(__file__).resolve().parent

# Chunked transport preserves the original uploaded PNG byte for byte.
logo = base64.b64decode("".join(p.read_text() for p in sorted((here / "logo-parts").glob("*.b64"))), validate=True)
if hashlib.sha256(logo).hexdigest() != "4fbe54e7749b1d920f1ab8ddd7b556367aa6564f4ddbbbb055cb268fa6ee1474":
    raise RuntimeError("Branding image checksum mismatch")
(here / "cherries-obs.png").write_bytes(logo)

def change(relative, old, new, count=1):
    path = root / relative
    text = path.read_text()
    assert text.count(old) == count, (relative, old, text.count(old))
    path.write_text(text.replace(old, new))

shutil.copyfile(here / 'cherries-obs.png', root / 'frontend/forms/images/obs.png')
shutil.copyfile(here / 'CherriesBranding.hpp', root / 'frontend/widgets/CherriesBranding.hpp')
change('frontend/widgets/OBSBasic.hpp', '#pragma once', '#pragma once\n#include "CherriesBranding.hpp"')
for file in ('frontend/OBSApp.cpp', 'frontend/widgets/OBSBasicStats.cpp', 'frontend/widgets/OBSProjector.cpp'):
    change(file, 'QIcon::fromTheme("obs", QIcon(":/res/images/obs.png"))',
           'QIcon(":/res/images/obs.png")')
change('frontend/OBSApp.cpp', 'installNativeEventFilter(new OBS::NativeEventFilter);',
       'setDesktopFileName("cherries-obs-wayland");\n\tinstallNativeEventFilter(new OBS::NativeEventFilter);')
change('frontend/OBSApp.cpp', 'setDesktopFileName("com.obsproject.Studio");',
       'setDesktopFileName("cherries-obs-wayland");\n\tsetApplicationDisplayName("Cherries OBS");')
change('frontend/widgets/OBSBasic.cpp', 'name << "OBS "', 'name << "Cherries OBS "', count=2)
change('frontend/obs-main.cpp', 'std::cout << "OBS Studio - "', 'std::cout << "Cherries OBS - "')
change('frontend/forms/OBSAbout.ui', '<string notr="true">OBS Studio</string>',
       '<string notr="true">Cherries OBS</string>')
change('frontend/dialogs/OBSAbout.cpp', '\tui->setupUi(this);',
       '\tui->setupUi(this);\n\tsetWindowTitle("About Cherries OBS");')
change('frontend/dialogs/OBSAbout.cpp', 'QTStr("About.Donate")', 'QStringLiteral("Support OBS Project")')
change('frontend/widgets/OBSBasic_SysTray.cpp', '"OBS Studio"', '"Cherries OBS"', count=2)
for file in ('frontend/widgets/OBSBasic.hpp', 'frontend/widgets/OBSBasic_SysTray.cpp', 'frontend/widgets/OBSBasic_Recording.cpp'):
    path = root / file
    text = path.read_text()
    for old, new in [
        ('QIcon::fromTheme("obs-tray", trayIconFile)', 'CherriesTrayIcon()'),
        ('QIcon::fromTheme("obs-tray-active", trayIconFile)', 'CherriesTrayIcon(1)'),
        ('QIcon::fromTheme("obs-tray-paused", trayIconFile)', 'CherriesTrayIcon(2)'),
        ('QIcon::fromTheme("obs-tray-active", QIcon(":/res/images/tray_active.png"))', 'CherriesTrayIcon(1)'),
    ]:
        text = text.replace(old, new)
    path.write_text(text)
# About describes this distribution; original author and license tabs remain.
p = root / 'frontend/dialogs/OBSAbout.cpp'
s = p.read_text()
a = s.index('\tQPointer<OBSAbout> about(this);')
b = s.index('\nvoid OBSAbout::ShowAuthors()', a)
s = s[:a] + '''\tShowAbout();
}

void OBSAbout::ShowAbout()
{
    ui->info->setText("<h2>Cherries OBS</h2>"
        "<p>Streaming and recording with native Wayland browser docks, game capture with audio, "
        "portrait canvases and multiple destinations.</p>"
        "<p>An independent distribution based on OBS Studio. "
        "OBS Studio copyright its contributors; licensed under GPL-2.0-or-later. "
        "Canvas and multistream engines derived from Aitum Vertical and Aitum Multistream, "
        "copyright their respective contributors; GPL-2.0.</p>"
        "<p><a href='https://github.com/iBustcherries/Cherries-OBS-browser'>Cherries OBS source and builds</a></p>");
}
''' + s[b:]
p.write_text(s)
