# Linux Game Capture

Release 14 bundles obs-vkcapture 1.5.6 at
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

Automatic selection means selection among games that have connected to the
capture hooks. It is not universal capture of every process on Linux. Vulkan
and OpenGL are supported; Proton games using DXVK/vkd3d can use Vulkan capture.
Software renderers, anti-cheat restrictions and games/runtimes that reject or
strip injection may still need PipeWire Window/Screen Capture. Fullscreen does
not bypass those limitations. Hook compatibility must be tested on the target
Steam runtime and games. The host library build cannot be injected into
Flatpak Steam's sandbox: that installation requires the corresponding Flatpak
obs-vkcapture runtime extension and launch configuration.

This source captures video, not per-game audio. Use OBS audio sources normally.
VkCapture is upstream GPL-2.0-or-later code; its license is included in the RPM.
