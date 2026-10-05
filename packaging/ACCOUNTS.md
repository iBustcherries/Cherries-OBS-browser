# Native accounts and shared multistream

Use **Settings → Stream** to add multiple Twitch and YouTube accounts. Check
**Use** for each destination you want to stream to. Account connections and
selection changes save immediately, including when the settings dialog is closed
with Cancel. Existing saved accounts from release 5 are retained; the separate
Accounts & Multistream dock is removed.

Each account has an **Audio track** selector: **OBS default** or **Track 1–6**.
OBS default follows the normal OBS streaming track. Existing accounts retain this
default until you choose a track. Assign sources to those tracks in **Advanced
Audio Properties**. For example, send Twitch track 1 and YouTube track 2, then
include the microphone/game on both tracks and music only on track 1. Choose one
mixed audio track per destination. Track choices save immediately and cannot be
changed while streaming or preparing a stream.

Connected-platform indicators appear only for saved connected sessions. They
refer to account authorization, not whether you are currently live. Twitch Chat
Add-Ons appears in Advanced Options when a Twitch account is connected and
applies BetterTTV/FrankerFaceZ to the native Twitch chat dock.

The Controls dock has separate full-width Manage Broadcast and Start Streaming
buttons. **Manage Broadcast** opens a separate window with three tabs:

Connected platforms also appear as buttons immediately below Manage Broadcast:
**Twitch Stream Info** and **YouTube Broadcasts**. They open the corresponding
tab using your saved connection and disappear when no connected account remains
for that platform.

- **Twitch Stream Info:** a native form for title, category search, tags and
  stream language. It uses the same Twitch authorization as Settings → Stream;
  there is no separate embedded-browser login. Save Stream Info updates Twitch
  without starting a stream. Additional Twitch website-only settings are not
  included in this native form.
- **Create New YouTube Stream:** OBS's native creation form, including title,
  description, privacy, category, thumbnail, scheduling and latency settings.
- **Select Existing YouTube Stream:** a refreshable broadcast table showing
  title, local scheduled time, privacy and status, with selected-broadcast details.

When several accounts are saved for a platform, Manage Broadcast asks which
account to edit. Prepare each selected YouTube account before starting. Use a
broadcast with automatic start enabled. Creating/selecting a broadcast prepares
its stream key for this OBS session; it does not start streaming. After restarting
OBS, select the broadcast again. Completed broadcasts must be replaced.

**Start Streaming** in Controls starts all selected destinations, including
starts requested through OBS hotkeys and the frontend API. Stopping OBS's primary
stream stops all destinations. OBS renders once and shares the same H.264 video
encoder across outputs; video resolution, FPS and bitrate are shared. Destinations
using the same OBS audio track share one AAC encoder. Different tracks use separate
AAC encoders with the primary output's audio encoder settings. This does not change
recording tracks or the normal OBS streaming-track setting.
Additional destinations add upload bandwidth and a small amount of connection
and packet handling. Each connection reconnects independently. Stream keys are
not saved in the account file. **Sending** reports transmitted bytes, not verified
platform live status. Kick account integration is not included in this release.

Account sessions persist in an atomic, owner-readable/writable file. Removing
an account deletes its saved session locally. Twitch validates at connection
and hourly, and rotating refresh tokens are saved. Google authorization uses
the system browser, loopback state validation and PKCE.

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
