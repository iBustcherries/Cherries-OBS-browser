# Native accounts and shared multistream

Use **Settings → Stream** to add multiple Twitch and YouTube accounts. Check
**Use** for each destination you want to stream to. Account connections and
selection changes save immediately, including when the settings dialog is closed
with Cancel. Existing saved accounts from release 5 are retained; the separate
Accounts & Multistream dock is removed.

Each account has a **Live audio** selector: **OBS default** or **Track 1–6**.
OBS default follows the normal OBS streaming track. Existing accounts retain this
default until you choose a track. Assign sources to those tracks in **Advanced
Audio Properties**. For example, send Twitch track 1 and YouTube track 2, then
include the microphone/game on both tracks and music only on track 1. Choose one
mixed audio track per destination. Track choices save immediately and cannot be
changed while streaming or preparing a stream.

Twitch accounts also have a **Twitch VOD** selector: **OBS default**, **Disabled**,
or **Track 1–6**. OBS default follows Settings → Output's Twitch VOD setting.
A selected track supplies the Twitch archive independently of the live mix;
Disabled archives the live mix. If live and VOD use the same mix, OBS sends the
normal live track only. YouTube never receives Twitch's additional archive track.
For example: Twitch live track 1, Twitch VOD track 2, YouTube live track 2. Put
microphone/game on both tracks and music only on track 1. Tracks shared between
live destinations and VODs reuse an AAC encoder. Enable past-broadcast storage on
Twitch if you want Twitch to save VODs.

Connected-platform indicators appear only for saved connected sessions. They
refer to account authorization, not whether you are currently live. Twitch Chat
Add-Ons appears in Advanced Options when a Twitch account is connected and
applies BetterTTV/FrankerFaceZ to all connected Twitch chat docks.
Accounts added through the account list load the standard platform docks:
Twitch chat, stream information, statistics and activity feed; YouTube chat and
the YouTube Live Control Panel. Additional accounts have separate dock names and
browser sessions. Use the **Docks** menu to show the panels you want. The Twitch
stream-info page remains in Manage Broadcast as well.

The Controls dock has separate full-width Manage Broadcast and Start Streaming
buttons. **Manage Broadcast** opens a separate window with three tabs:

Connected platforms also appear as buttons immediately below Manage Broadcast:
**Twitch Stream Info** and **YouTube Broadcasts**. They open the corresponding
tab using your saved connection and disappear when no connected account remains
for that platform.

- **Twitch Stream Info:** Twitch's actual embedded stream-info webpage, including
  the settings Twitch provides there. Twitch sign-in in Settings → Stream runs
  inside OBS with the same persistent, account-specific browser session used by
  this page and Twitch's other docks. Connecting an account does not publish
  changes or start streaming. Twitch controls the page's appearance and fields.
- **Create New YouTube Stream:** OBS's native creation form, including title,
  description, privacy, category, thumbnail, scheduling and latency settings.
- **Select Existing YouTube Stream:** a refreshable broadcast table showing
  title, local scheduled time, privacy and status, with selected-broadcast details.

When several accounts are saved for a platform, Manage Broadcast asks which
account to edit. Prepare each selected YouTube account before starting. Both
automatic and manual broadcast start are supported. For manual start, first use
**Start Streaming** in Controls, then **Start YouTube broadcast** in Manage
Broadcast once YouTube is receiving video. **Refresh status** queries YouTube;
**End YouTube broadcast** ends that selected broadcast after confirmation.
An ended broadcast cannot be resumed. If that YouTube account owns the primary
OBS output, ending it also stops the other selected destinations; the confirmation
explains this. A selected Twitch account is preferred as primary when available.
Automatic stop follows the broadcast's YouTube setting. With automatic stop off,
use End YouTube broadcast or YouTube Studio to finish it.

Creating/selecting a broadcast prepares it without starting streaming. Broadcast
and stream IDs are saved; the current stream key is fetched again from YouTube
before streaming. Completed broadcasts must be replaced. Broadcast selection is
locked while that account is streaming. Standard creation options include
thumbnails, privacy, category, made-for-kids, scheduling, latency, DVR and 360°.

**Start Streaming** in Controls starts all selected destinations, including
starts requested through OBS hotkeys and the frontend API. Stopping OBS's primary
stream stops all destinations. By default, OBS renders once and shares the same
H.264 video encoder across outputs; video resolution, FPS and bitrate are shared. Destinations
using the same OBS audio track share one AAC encoder. Different tracks use separate
AAC encoders with the primary output's audio encoder settings. This does not change
recording tracks or the normal OBS streaming-track setting.
Additional destinations add upload bandwidth and a small amount of connection
and packet handling. Each connection reconnects independently. API-provided stream keys are not saved in the account file. A manually entered
YouTube portrait key is saved with the account, with the same owner-only file permissions. **Sending** reports transmitted bytes, not verified
platform live status. Kick account integration is not included in this release.

### Optional Twitch Enhanced Broadcasting

The **Enhanced** checkbox on a Twitch account opts into OBS's existing Enhanced
Broadcasting pipeline. It is off for newly added accounts unless imported from
an existing native OBS connection with that setting enabled. Enable it on one
selected Twitch account at a time. Twitch may request multiple video and audio
renditions, subject to the available encoders and Twitch's configuration. This
mode can consume more encoding resources and bandwidth. The standard OBS maximum
bandwidth/video-track settings continue to apply.

The enhanced Twitch account becomes the primary output. Other destinations reuse
an H.264 rendition and AAC audio when Twitch supplies compatible encoders; they
do not create another video encoder. That rendition may differ in resolution or
bitrate from Twitch's first rendition. If no compatible rendition exists, OBS
reports the incompatibility before connecting. Turn Enhanced off for the normal
shared H.264 path, or select only the enhanced Twitch destination. VOD/live mix
choices are passed into the native Enhanced Broadcasting configuration.

### Account sessions

Existing Twitch connections authorized in the system browser may need one
reconnection through **Settings → Stream → Add Twitch**, using the same account.
This updates the account in place and preserves its track and destination choices.
Existing embedded stream-info sessions from releases 6/7 are reused where possible.
A website may still request sign-in again if its session expires or is revoked.
OAuth access tokens are never substituted for Twitch website cookies.

Single-destination streaming keeps the native platform's supported video codecs.
Shared destinations require a common H.264 rendition and AAC audio. The native
YouTube RTMP, RTMPS and HLS service/server selection is respected, including the
HLS output type; accounts added with Add YouTube default to RTMPS.

The native Twitch server choice and bandwidth-test setting are preserved. Twitch
bandwidth-test mode affects Twitch only; other selected platforms can still go
live. Uncheck other destinations when testing only Twitch's connection.

Account sessions persist in an atomic, owner-readable/writable file. Removing
an account deletes its saved session locally. Twitch validates at connection
and hourly, and rotating refresh tokens are saved. Google authorization uses
the system browser, loopback state validation and PKCE. Google's embedded website
session remains separate from its API authorization, as in standard OBS; YouTube
may require browser sign-in for chat posting or the Live Control Panel. Broadcast
creation and control use the saved OAuth account.

### Portrait canvas and YouTube dual-format streaming

Open **Docks → Portrait Canvas** (also in Tools), then **Add Portrait Canvas**.
Choose 1080×1920 or 720×1280 and a portrait H.264 encoder/bitrate. "Use landscape
encoder type" copies the landscape encoder's settings into a separate encoder;
it does not change the landscape encoder. Output settings are locked while
video outputs are active.

- **New scene** creates an independent portrait layout; **Copy landscape** copies
  the current program scene's layout while reusing its capture sources. Groups
  get independent transforms. Nested scene sources remain shared.
- **Reuse source** adds an existing camera/game/browser source. Create new source
  instances through the normal OBS Sources dock first.
- Drag sources in the portrait preview to move them. Drag a source's bottom-right
  corner to resize proportionally. **Transform** (or double-click a source name)
  exposes position, scale, rotation, bounding-box size and cropping. **Fit** and
  **Fill** preserve aspect ratio. Checkboxes control visibility; Move up/down
  controls layering. These portrait controls currently do not integrate with
  OBS's global undo stack.
- Link each portrait scene to a landscape scene. **Follow linked landscape scene
  changes** follows the live program scene (not the Studio Mode preview). It
  cuts to the linked portrait scene; independent portrait transitions are not
  included. An unlinked landscape scene leaves the current portrait scene active.
- Portrait layouts, links, dimensions and encoder options are saved with the
  scene collection. The portrait canvas does not mix a duplicate audio feed.

In **Manage Broadcast → YouTube Output**, enable landscape and portrait for the
chosen account. In YouTube Studio's Live Control Room, enable **Dual stream**,
choose **Encoder** for the vertical view and select its second stream key. Paste
that vertical key into OBS. It must differ from the landscape key. Configure
pairing before going live; YouTube's public broadcast-binding API does not expose
this Studio dual-input configuration. The new tab opens Studio for setup and
shows the separate landscape/portrait connection states.

**Start Streaming** sends the selected landscape destinations and portrait
feeds together. **Stop Streaming** stops both. Portrait outputs reconnect
independently; failure of a portrait output does not stop the landscape stream.
Confirm both previews in Studio before manually starting the broadcast. OBS's
"Sending" status confirms transmitted bytes, not YouTube's dual-stream pairing.

Landscape destinations retain shared encoding. All selected YouTube portrait
feeds share one additional H.264 encoder and the portrait canvas; audio reuses
each account's selected live AAC track. Portrait requires an additional render
pass, video encoder and upload connection per destination. Twitch Enhanced
Broadcasting remains optional and may add its own renditions. YouTube dual format
uses RTMPS for both orientations, including when a native HLS service was selected.

The portrait key is masked in the UI and saved in the owner-readable account file.
You can replace it if YouTube rotates the key. No additional Google OAuth login
is needed for the output; Studio's website session is separate, as before.

### Release 10 validation status

The earlier platform changes passed the release 9 Fedora compile. Portrait
geometry and shared-encoder/audio routing tests pass locally, as do syntax checks
for the new dock and modified controls. Full release 10 Fedora packaging and
installation checks are pending. Authenticated account, target Wayland desktop,
and live dual-format testing remain pending on the target system.

## Application setup

For a build without bundled application credentials, the first connection
offers setup in the UI. **Application setup…** lets you replace the defaults
for future connections without changing already saved accounts.

- Twitch: register a **Public** application at https://dev.twitch.tv/console/apps
  and enter its Client ID. The device authorization flow needs no client secret.
  Your old confidential chat application's secret is not suitable for bundling.
- YouTube: enable the YouTube Data API in your Google project and import a
  **Desktop app** OAuth client JSON. You may reuse `youtube_client_secret.json`
  from the chat project. Add the accounts as test users while the consent screen
  is in Testing. Google's verification and quota rules still apply.

Maintainers can bundle credentials once so other users only need to sign in:
set the GitHub repository variable **CHERRIES_TWITCH_CLIENT_ID** to the Public
application Client ID and repository secret **OBS_YOUTUBE_CLIENT_JSON** to the
Desktop app JSON. The Fedora build generates a private build header, never
commits the credentials, and never embeds a confidential Twitch secret or user
access tokens. A Google Desktop client is distributed with the application;
the repository secret keeps it out of source and logs, not out of the binary.
