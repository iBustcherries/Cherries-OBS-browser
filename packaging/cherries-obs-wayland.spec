%global debug_package %{nil}
%global __provides_exclude_from ^/opt/cherries-obs/.*$
%global __requires_exclude ^(libobs|libcef|libEGL|libGLESv2|libvk_swiftshader|libvulkan).*\.so.*$

Name: cherries-obs-wayland
Version: 0.1
Release: 1.experimental%{?dist}
Summary: Experimental OBS build with Wayland browser docks
License: GPL-2.0-or-later AND BSD-3-Clause
URL: https://github.com/iBustcherries/Cherries-OBS-browser
Requires: fontconfig

%description
Experimental OBS Studio and CEF build using off-screen browser docks on
Wayland. Installed under /opt/cherries-obs with a separate launcher.
Embedded OAuth popups, DevTools, and full IME support are incomplete.

%prep

%build

%install
mkdir -p %{buildroot}/opt %{buildroot}/usr/bin %{buildroot}/usr/share/applications
cp -a %{stage_dir}/opt/cherries-obs %{buildroot}/opt/
cat > %{buildroot}/usr/bin/cherries-obs-wayland <<'EOF'
#!/bin/sh
export QT_QPA_PLATFORM=wayland
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
/usr/share/applications/cherries-obs-wayland.desktop
