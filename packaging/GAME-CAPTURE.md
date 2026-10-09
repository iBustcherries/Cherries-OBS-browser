# Linux Game Capture

The package bundles obs-vkcapture 1.5.6 at
8a2c43295fae2ede8ffd382a1581281ceb6153c0 with the OBS source plugin and both
64-bit and 32-bit Vulkan/OpenGL hooks. Add Sources > Game Capture and leave
Window set to Automatic (latest compatible game). The newest hooked game is
selected; when it exits, capture falls back to the remaining matching game.
Common launcher processes are excluded in automatic mode. The executable
selector remains available for explicit selection, including launcher UI.

Release 18 removes the OpenGL hook's eager dependency on the graphics library.
Graphics entry points remain resolved lazily, after the game's modules load.
This avoids preempting the writable OpenGL function-pointer variables exported
by native Dead Cells' HashLink SDL module. Regression checks reproduce the
resulting initialization crash with the old dependency order and verify that
the installed 64-bit and 32-bit hooks allow initialization. Actual Dead Cells
video and game-audio capture still require testing on the user's machine.

Release 17 adds **Tools > Game Capture Setup** inside OBS. For normal Steam
use, quit Steam once and select **Enable automatic capture through normal
Steam**. Open your usual Steam shortcut again. Compatible future Vulkan and
OpenGL games inherit the hooks and connect automatically to an active Game
Capture source, including its process-matched audio. No per-game launch-option
setup or separate launcher is required in this mode. The per-user override keeps
Steam's desktop ID, name, icon and URI actions. Existing Steam autostart is
updated if present; none is created. Disable restores the original shortcuts.
Steam started directly through a terminal or another launcher does not use this
desktop override. Capture still requires compatible games and the host hooks to
be accepted by their runtime; there is no remote injection into games already
running. Unknown launcher scripts that strip hooks may need future compatibility
profiles like Dead Cells'.

As an independent option, enable automatic
Vulkan capture once to launch compatible Vulkan/Proton games normally, without
Steam launch options or a special launcher. This installs per-user implicit
layer manifests and architecture-specific hook copies in the user's data
folder; the copies remain visible inside native Steam's runtime. Disable the
checkbox to remove those manifests. Unrelated non-Steam Vulkan programs are
not connected unless capture was explicitly requested. Restart already-running games after
changing the setting. OBS refreshes these owned copies after package upgrades.

As an optional per-game alternative, choose the Steam account and click **Enable capture** beside
the game. Quit Steam completely before applying changes. Installed games are
read from Steam's library list, including additional mounted drives and paths
with spaces; use **Refresh games** after installing games or mounting a drive.
Setup modifies only that game's launch option, preserves environment variables,
wrappers and arguments, saves the original option and a private configuration
backup, and lets **Disable capture** restore the original value. It refuses to
overwrite Steam settings while Steam is running, malformed/ambiguous files,
unsupported command syntax, or options edited externally after setup. Existing
manually enabled capture options are recognized and left untouched.

The native Dead Cells profile is offered when its Linux launcher is installed;
it does not change the game's Proton selection. Unknown games remain **Not
checked**. **Configured** means a launch option was saved, not that capture was
tested. **Capturing now** requires an active Game Capture source with a valid
imported video texture and a matching Steam app ID from the game's process.
After capture ends, the history is labeled **Captured before**. This setup
currently supports native/RPM Steam; Flatpak Steam requires its own runtime
integration. Games without readable Steam process metadata may capture without
appearing in the learned history.

The older launcher remains available as an alternative. For native Steam, quit Steam completely, then launch **Cherries Steam (Game
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

Release 19 corrects process selection for games using PulseAudio through
PipeWire. For a `client.api=pipewire-pulse` client, `pipewire.sec.pid` belongs
to the bridge, even when the client binary/name describes the game. Match the
game's `application.process.id` instead; native PipeWire clients keep the
protocol's host PID. Missing or invalid Pulse app PIDs never fall back to the
shared bridge PID. The fix applies to all PulseAudio games, not just Dead Cells.
Live tests cover native PipeWire and PulseAudio game isolation, switching,
stopping, and rejection of the shared Pulse bridge as a game target. A legacy
PID-selection control must reproduce silent game capture before the corrected
Pulse test can pass.

The backend reuses obs-pipewire-audio-capture at
6120aa622792908e4529b0e171b2acce5efb3f18. It matches PipeWire process metadata,
including PulseAudio-compatible streams, instead of recording the full desktop
monitor. Processes without usable PID metadata or separate unrelated audio
helper processes may require explicit application-audio routing. Capture
needs the user's PipeWire server; it does not silently fall back to desktop
sound if no game stream matches.
VkCapture is upstream GPL-2.0-or-later code; its license is included in the RPM.
