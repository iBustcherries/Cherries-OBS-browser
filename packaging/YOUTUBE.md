# Native YouTube integration

Release 14 enables the pinned upstream OBS YouTube integration, including account
connection, channel identification, automatic stream-key configuration, broadcast
creation and selection, scheduling, title/description, category, thumbnail,
privacy, made-for-kids, latency, DVR, auto-start/stop, broadcast transitions,
chat and the YouTube Live Control Panel browser dock. The forms, API wrappers,
stream lifecycle and Controls behavior are upstream OBS code.

In Settings > Stream, choose YouTube - RTMPS and click Connect Account, or click
Connect another YouTube account from an additional destination's account chooser.
Google login opens in your default browser and returns to a loopback listener.
Sign-in starts only from a user's connection action. Startup, opening a dock and
background token refresh do not launch Google sign-in. Failed refresh does not
automatically restart authorization; the user must reconnect explicitly.

Release 23 packaging supports a bundled Cherries OBS Google Desktop OAuth client,
so new users do not import JSON or supply a stream key. The distribution owner
must first register the application in Google Cloud, enable YouTube Data API v3,
configure the consent screen and create a Desktop app client. Store its downloaded
JSON as the repository Actions secret `CHERRIES_YOUTUBE_CLIENT_JSON`. The Fedora
release build requires valid configuration and refuses to package without it.
It cannot reuse OBS Project's application credentials. Testing projects must add
their testers' Google accounts; refresh tokens may expire after seven days.
Production availability remains subject to Google's verification and quota rules.

The generator puts only the Desktop application's client ID and client secret
into the compiled application, without printing them to build logs or committing
the real JSON to source control. Desktop client credentials are extractable from
distributed binaries; this is not storage for private server credentials or user
access/refresh tokens. Never use a Web client or service-account configuration.
User tokens are obtained only after consent and are stored locally as before.

Previously imported profile/application clients retain precedence so existing
tokens continue to use their original application registration. Custom builds
without a bundled client can still import a Desktop client JSON explicitly when
connecting, as in releases 14–22; the Fedora release workflow requires bundling.

The adaptation requests offline access, uses PKCE S256, URL-encodes form values,
accumulates fragmented callback requests and validates state and unique query
parameters. Favicon requests and unrelated connections do not cancel login.
Authorization codes are excluded from callback logs. Refreshed tokens are
persisted immediately.

OAuth account authorization and the Google website's browser cookies are
separate sessions. The upstream YouTube website docks may require a browser
sign-in; OAuth alone does not manufacture a Google website session. Native
broadcast management works through the API independently of those cookies.

The main OBS service remains profile-specific. Cherries Studio also supports
additional landscape/portrait destinations and per-destination YouTube broadcast
management; see `STUDIO.md` for that integration.
