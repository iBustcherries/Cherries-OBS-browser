# Native Twitch on Wayland

Release 13 enables OBS's existing TwitchAuth class and its native Stream settings, Chat, Stream Information, Twitch Stats and Activity Feed docks. BetterTTV/FrankerFaceZ, live/VOD audio track selection, bandwidth testing and Enhanced Broadcasting use the upstream OBS implementations. The Controls dock is unchanged.

The native integration is patched onto pinned OBS cffa83ba552f1ef6a0a05851c3aa07b3811d7e58. This browser repository does not contain the OBS frontend, so the Fedora workflow applies packaging/obs-twitch-integration.patch and copies the small Twitch login adapter into its OAuth directory.

## Authentication

OBS's official OAuth relay credentials are unavailable to third-party forks. The login adapter uses Twitch's public-client device authorization flow inside CEF. Authentication and every native Twitch dock share OBS's profile cookie manager. Session cookies persist, Twitch authorization navigations remain in that context, and the native Wayland clipboard is forwarded to CEF text fields. Access tokens are validated on load and hourly, with refresh-token rotation saved through OBS's native profile configuration.

The owner-provided Public Twitch application ID is bundled by default. CHERRIES_TWITCH_CLIENT_ID can override it for another distributor. An unset or empty repository variable keeps the bundled default. No client secret is distributed. Existing profile client IDs retain precedence for token compatibility, and successful authorization remembers the ID at application level for new profiles. Normal account connection does not prompt for a Client ID. Authorization may require entering the code shown above the embedded Twitch page. The application login, chat, and account data are not sent to a custom backend.

## Usage

Settings → Stream → Twitch → Connect Account. Sign in and authorize inside the embedded Twitch window. Apply Settings. Chat and Stream Information appear initially; show Twitch Stats and Activity Feed from Docks. Move or float all docks normally. Settings → Stream retains optional Enhanced Broadcasting and its automatic/manual resource limits. Settings → Output offers Twitch VOD Track and live/VOD audio track selection; route sources using Advanced Audio Properties.

Enhanced Broadcasting uses the upstream encoder negotiation and can require extra GPU/CPU resources; available codecs depend on the system. There is no new multistream output, account manager, custom broadcast manager or portrait canvas.

## Validation

Fedora CI runs dock mouse-grab, browser frame resize/DPI and Twitch token/form/popup regression checks, then builds OBS, installs the RPM and checks executable loading. An installed KDE/Wayland desktop is still required to verify live Twitch authorization, dock loading and restoration, clipboard paste, VOD separation, and Enhanced Broadcasting negotiation. DevTools, general native browser popups and full IME remain experimental.
