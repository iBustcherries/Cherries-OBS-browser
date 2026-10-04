# Native accounts and multistream

The experimental Fedora RPM includes **Docks → Accounts & Multistream**.
Add multiple Twitch and YouTube accounts, check the destinations you want,
and click **Start selected streams**. OBS renders once and all destinations
share the primary stream's H.264 video and AAC audio encoders. Set resolution,
frame rate and bitrate in OBS as usual. Upload bandwidth increases for each
destination. Connections have independent reconnect handling; stopping the
primary OBS stream stops all destinations.

Each selected YouTube channel opens OBS's broadcast dialog before starting.
Choose **Stream now** or a broadcast with automatic start enabled. Broadcast
title, privacy and other broadcast settings remain available in that dialog.
The connection label **Sending** means OBS is transmitting; it does not verify
that a platform has made the broadcast public/live.

Account sessions persist across restarts in an atomic, owner-readable/writable
account file. Removing an account deletes its saved session locally. Twitch
sessions are validated at connection and hourly; rotating refresh tokens are
saved after refresh. Google authorization uses the system browser, a loopback
callback with state validation, and PKCE.

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

Native OBS account connections in **Settings → Stream** are also enabled.
Use the multistream dock to start all checked accounts together. Existing OBS
service/account settings are temporarily replaced for that session and restored
after stopping. The dock currently uses one video track and one audio track,
with identical encoding settings for all destinations.
