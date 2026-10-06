# Linux Game Capture

The package bundles obs-vkcapture 1.5.6 at
8a2c43295fae2ede8ffd382a1581281ceb6153c0 with the OBS source plugin and both
64-bit and 32-bit Vulkan/OpenGL hooks. Add Sources > Game Capture and leave
Window set to Automatic (latest compatible game). The newest hooked game is
selected; when it exits, capture falls back to the remaining matching game.
Common launcher processes are excluded in automatic mode. The executable
selector remains available for explicit selection, including launcher UI.

For native Steam, quit Steam completely, then launch **Cherries Steam (Game
Capture)** from your application menu. Games started by that Steam process
inherit the capture hooks without per-game launch options. Existing Steam
processes cannot acquire environment changes retroactively, so the launcher
asks you to quit Steam instead of silently pretending capture is enabled.

For another native launcher/game, run `cherries-gamecapture COMMAND ...`.
For a single Steam game's launch options, use
`cherries-gamecapture %command%` when the regular launcher is preferred.
The launcher preserves existing preload and Vulkan layer configuration,
adds the Cherries manifests and exposes /opt/cherries-obs to Steam's runtime.
Architecture-specific manifests and loader-token aliases support native and
Steam runtime library directory layouts.

Release 16 automatically handles the native Steam version of Dead Cells
(Steam app 588650). Its supplied `deadcells.sh` clears `LD_PRELOAD` before
starting the rendering process. When Cherries capture is enabled, the OpenGL
hook restores its own preload for that final `deadcells` process. The
`detect.hl` renderer detection step keeps the game's normal environment.
The original game scripts and binaries are never edited, and other games and
launches without Cherries capture enabled are unchanged. This applies to both
the Cherries Steam launcher and `cherries-gamecapture %command%`; restarting
the game is necessary after enabling capture. If a previous manual edit was
made, restore the saved original `deadcells.sh` before testing this behavior.
Tests reproduce the game's two-step shell launcher, check exec/spawn paths,
and verify preload/argument preservation and an unchanged script checksum.
Actual Dead Cells graphics and audio must also be verified on the user's machine.

Automatic selection means selection among games that have connected to the
capture hooks. It is not universal capture of every process on Linux. Vulkan
and OpenGL are supported; Proton games using DXVK/vkd3d can use Vulkan capture.
Software renderers, anti-cheat restrictions and games/runtimes that reject or
strip injection may still need PipeWire Window/Screen Capture. Fullscreen does
not bypass those limitations. Hook compatibility must be tested on the target
Steam runtime and games. The host library build cannot be injected into
Flatpak Steam's sandbox: that installation requires the corresponding Flatpak
obs-vkcapture runtime extension and launch configuration.

Game Capture also includes PipeWire application audio, enabled by default with
its Capture game audio checkbox. The selected game's host process ID comes
from the capture socket's kernel credentials. Audio follows that process and
its descendants and stops when the source is hidden or the game disconnects.
The source has one normal OBS mixer channel: volume, mute, monitoring, filters
and track assignments apply to its audio. This does not change the game's
existing speaker/headphone output. If Desktop Audio captures the same device,
disable that Desktop Audio capture to avoid recording the game twice.

The backend reuses obs-pipewire-audio-capture at
6120aa622792908e4529b0e171b2acce5efb3f18. It matches PipeWire process metadata,
including PulseAudio-compatible streams, instead of recording the full desktop
monitor. Processes without usable PID metadata or separate unrelated audio
helper processes may require explicit application-audio routing. Capture
needs the user's PipeWire server; it does not silently fall back to desktop
sound if no game stream matches.
VkCapture is upstream GPL-2.0-or-later code; its license is included in the RPM.
