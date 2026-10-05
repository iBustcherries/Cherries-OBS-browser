# Native YouTube integration

Release 14 enables the pinned upstream OBS YouTube integration, including account
connection, channel identification, automatic stream-key configuration, broadcast
creation and selection, scheduling, title/description, category, thumbnail,
privacy, made-for-kids, latency, DVR, auto-start/stop, broadcast transitions,
chat and the YouTube Live Control Panel browser dock. The forms, API wrappers,
stream lifecycle and Controls behavior are upstream OBS code.

In Settings > Stream, choose YouTube - RTMPS and Connect Account. Google login
opens in your default browser and returns to a loopback listener. This build
uses a Google Desktop OAuth client belonging to your project; it cannot reuse
OBS Project's credentials. Enable YouTube Data API v3, configure the OAuth
consent screen, create a Desktop app client, and download the client JSON. When
prompted, import that file once. Successful login remembers the credentials
in the profile and application configuration, so switching OBS profiles does
not require importing them again. Testing projects must include your Google
account as a test user; their refresh tokens may expire after seven days.
Production availability is subject to Google's verification and quota rules.

Alternatively, the owner can set the private repository Actions secret
OBS_YOUTUBE_CLIENT_JSON before building. The workflow generates a build-only
header. No Google credentials are committed. Desktop clients are distributed
applications, so an embedded client secret is not confidential from someone
who has the installed binary. Never use a Web application client here.

The adaptation requests offline access, uses PKCE S256, URL-encodes form values,
accumulates fragmented callback requests and validates state and unique query
parameters. Favicon requests and unrelated connections do not cancel login.
Authorization codes are excluded from callback logs. Refreshed tokens are
persisted immediately.

OAuth account authorization and the Google website's browser cookies are
separate sessions. The upstream YouTube website docks may require a browser
sign-in; OAuth alone does not manufacture a Google website session. Native
broadcast management works through the API independently of those cookies.

This retains standard OBS's one streaming service per profile. It does not
restore the removed custom multistream manager or add portrait canvases.
