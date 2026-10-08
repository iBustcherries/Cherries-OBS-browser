# Cherries Studio — Mockup A

The Fedora build integrates a portrait canvas and multiple streaming destinations
with OBS's native landscape canvas. The workspace uses native, movable Qt docks.
Cherries Studio > Restore Mockup A Layout restores the initial arrangement.

## Getting started

1. Configure the main OBS account in Settings > Stream. Its native broadcast,
   Twitch VOD and Enhanced Broadcasting controls remain available.
2. Use Scene Pairs > Copy Current Scene to Portrait to reuse existing sources.
   Select Portrait Sources to crop and position those source instances separately.
3. Assign a portrait scene to each landscape scene in Scene Pairs. Link scene
   changes can be disabled while editing or when independent switching is wanted.
4. Add landscape and portrait destinations in Destinations > Add / Edit
   Destinations. For YouTube, choose its preset and leave the key blank. Save,
   then open Manage Broadcast and Create / select broadcast for that destination.
   Reuse the main YouTube account or a previously saved channel, or connect a new
   account without changing the main OBS service. Create a new broadcast using
   OBS's native dialog, or select an existing one. Its ingest address and key
   are applied automatically to that destination. Other platforms retain their
   stream-server/key setup.
5. Choose Include in Start Selected on the desired outputs. Shared landscape
   outputs wait for OBS's primary stream to actually start. An unchecked primary
   output is never silently started. Dedicated encoders can run independently.

The OBS Account card uses the current native OBS service directly, including its
account authorization. Manage Broadcast lists additional YouTube destinations
and retains access to the native main Twitch/YouTube controls. Each YouTube
destination has create/select, edit title/description/privacy/schedule, Go live,
and End broadcast actions. Channels are authorized once per OBS profile and can
be reused by several destinations; their credentials stay in the profile, as
with native OBS authorization. No tokens are passed to the multistream plugin.
Creating or selecting a broadcast does not start any output. If a broadcast has
automatic start disabled, start its encoder and then use Go live. End broadcast
asks for confirmation and stops only that destination. A completed broadcast
needs to be replaced/reselected before the next session, as appropriate on YouTube.

Landscape and portrait assignments must use different ingest streams. Duplicate
broadcast IDs, stream IDs, and stream keys are rejected across destinations and
the main service. An active destination cannot be rebound. Additional broadcast
dialogs use isolated API state and never update the main service's stream key,
chat selection, broadcast ID, or remembered broadcast defaults. The compact-card
layout is a separate design proposal and has not been applied in this release.

## Feature map

| Feature | Interface / implementation |
| --- | --- |
| Independent portrait composition | Portrait Canvas and Portrait Sources |
| Shared capture sources | Copy Current Scene to Portrait uses OBS_SCENE_DUP_REFS |
| Linked and independent scenes | Scene Pairs and Link scene changes |
| Independent transitions and projectors | Portrait Transitions and preview context menu |
| Main and portrait stream destinations | Destinations, platform presets and custom endpoints |
| Individual and grouped start/stop | Each card, Start Selected, Stop All Destinations |
| Shared landscape video encoding | Inherit OBS encoder; starts after STREAMING_STARTED |
| Per-destination encoding and scaling | Advanced destination video settings |
| Per-destination audio tracks | Advanced destination audio settings |
| Portrait recording and replay clips | Capture & Clips and Portrait Settings |
| Recording formats, split/chapter controls | Upstream portrait settings and hotkeys |
| Virtual camera modes | Portrait Settings; main/portrait/both as supported by host |
| Hotkeys and websocket commands | Upstream engine registrations retained |
| Twitch extra canvas / multitrack support | Upstream engine integration retained; account/service support required |
| Custom layouts | OBS movable/tabbed docks; restore command available |

Landscape and portrait have different pixel layouts and need separate video
encoding. Matching landscape destinations can reuse the main encoded video;
different bitrates, resolutions or codecs may need additional encoders.

## Verification

The RPM workflow compiles and installs the package, verifies the prior game/audio
regressions, and launches the installed OBS with a CI-only test plugin in an
isolated profile. Three local RTMP receivers check actual frames, landscape
encoder reuse, independent portrait dimensions, decoded audio-track routing and
output stopping. The test plugin is not included in the RPM. The workflow saves
a real workspace screenshot and logs for review. Platform OAuth authorization,
physical virtual-camera availability and production streaming are host-dependent.

## Upstream source and attribution

Cherries Studio adapts **Aitum Vertical** and **Aitum Multistream**, copyright
their respective contributors, under their GPL-2.0 licenses. Attribution is
retained in source, module authors, the About dialog and installed license files.
This distribution is not an official Aitum release.

* Vertical 1.6.6: https://github.com/Aitum/obs-vertical-canvas/tree/106a55398d2cfa7aa039b02424c6a8d8619b6966
* Multistream 1.0.8: https://github.com/Aitum/obs-aitum-multistream/tree/c48b4bf182f8b354255df7329c9afe3dee4f9820
* Cherries source and modifications: https://github.com/iBustcherries/Cherries-OBS-browser/tree/wayland-osr-docks/packaging/studio

The Fedora workflow fetches these exact upstream revisions and applies
`studio/integrate.py`. `engine.cmake` uses OBS's build and installation helpers.
Internal module and websocket IDs retain upstream names for compatibility.
Fork updates ship through the RPM rather than the upstream update service.

Release 22 uses compact destination rows with expandable details. Portrait Canvas is preview-only; recording, replay and camera actions remain in Capture & Clips, with settings available from Portrait Settings. Donation and promotional buttons were removed from the two dock footers; engine copyright and GPL attribution remain intact.
