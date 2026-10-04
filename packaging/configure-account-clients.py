"""Generate optional desktop application credentials without logging their values."""
import json
import os
import re
import sys
from pathlib import Path

twitch = os.environ.get("CHERRIES_TWITCH_CLIENT_ID", "").strip()
if twitch and not re.fullmatch(r"[A-Za-z0-9]{10,128}", twitch):
    raise SystemExit("CHERRIES_TWITCH_CLIENT_ID must be a Twitch Public application Client ID")
google_id = google_secret = ""
raw = os.environ.get("OBS_YOUTUBE_CLIENT_JSON", "").strip()
if raw:
    try:
        installed = json.loads(raw)["installed"]
        google_id = installed["client_id"]
        google_secret = installed["client_secret"]
        assert isinstance(google_id, str) and google_id.endswith(".apps.googleusercontent.com")
        assert isinstance(google_secret, str) and google_secret
    except (ValueError, KeyError, TypeError, AssertionError):
        raise SystemExit("OBS_YOUTUBE_CLIENT_JSON must contain a Google Desktop app OAuth client") from None

target = Path(sys.argv[1])
target.write_text("#pragma once\n" + "\n".join(
    "inline constexpr const char *" + name + " = " + json.dumps(value) + ";"
    for name, value in [("CherriesBundledTwitchId", twitch),
                        ("CherriesBundledGoogleId", google_id),
                        ("CherriesBundledGoogleSecret", google_secret)]
) + "\n")
