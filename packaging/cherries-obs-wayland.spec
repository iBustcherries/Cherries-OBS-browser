%global debug_package %{nil}
%global __provides_exclude_from ^/opt/cherries-obs/.*$
%global __requires_exclude ^(libobs|libcef|libEGL|libGLESv2|libvk_swiftshader|libvulkan).*\.so.*$

Name: cherries-obs-wayland
Version: 0.1
Release: 21.experimental%{?dist}
Summary: Experimental OBS build with Wayland browser docks
License: GPL-2.0-or-later AND BSD-3-Clause
URL: https://github.com/iBustcherries/Cherries-OBS-browser
Requires: fontconfig
Requires: qt6-qtwayland
Requires: mesa-libEGL
Requires: vulkan-loader
Requires: procps-ng
Requires: zenity

%description
Experimental OBS Studio and CEF build using off-screen browser docks on
Wayland. Installed under /opt/cherries-obs with a separate launcher.
Includes native OBS Twitch and YouTube integration, plus automatic selection
of compatible Vulkan/OpenGL games using VkCapture.
Cherries Studio adds portrait canvases, linked scenes, recording/replay tools
and multiple streaming destinations with per-output encoder and audio settings.
DevTools and full IME support are incomplete.

%prep

%build

%install
mkdir -p %{buildroot}/opt %{buildroot}/usr/bin %{buildroot}/usr/share/applications
cp -a %{stage_dir}/opt/cherries-obs %{buildroot}/opt/
install -m 0755 %{stage_dir}/../browser-fork/packaging/gamecapture/cherries-gamecapture %{buildroot}/usr/bin/cherries-gamecapture
install -m 0755 %{stage_dir}/../browser-fork/packaging/gamecapture/cherries-steam %{buildroot}/usr/bin/cherries-steam
cat > %{buildroot}/usr/share/applications/cherries-steam.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Cherries Steam (Game Capture)
Comment=Launch native Steam with automatic game capture enabled
Exec=cherries-steam
Icon=steam
Terminal=false
Categories=Game;
EOF
cat > %{buildroot}/usr/bin/cherries-obs-wayland <<'EOF'
#!/bin/sh
umask 077
export QT_QPA_PLATFORM=wayland
export CHERRIES_HOST_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
export XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}/cherries-obs-experimental"
exec /opt/cherries-obs/bin/obs "$@"
EOF
chmod 0755 %{buildroot}/usr/bin/cherries-obs-wayland
cat > %{buildroot}/usr/share/applications/cherries-obs-wayland.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Cherries OBS (Experimental Wayland)
Comment=OBS with experimental Wayland browser docks
Exec=cherries-obs-wayland
Icon=com.obsproject.Studio
Terminal=false
Categories=AudioVideo;Recorder;
EOF

%files
/opt/cherries-obs
/usr/bin/cherries-obs-wayland
/usr/bin/cherries-gamecapture
/usr/bin/cherries-steam
/usr/share/applications/cherries-steam.desktop
/usr/share/applications/cherries-obs-wayland.desktop
