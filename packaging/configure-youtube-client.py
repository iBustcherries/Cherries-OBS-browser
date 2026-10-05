"""Optionally inject a private Google Desktop OAuth client into the CI build."""
import json
import os
import pathlib
import sys

raw = os.environ.get("OBS_YOUTUBE_CLIENT_JSON", "").strip()
if raw:
    try:
        installed = json.loads(raw)["installed"]
        assert installed["client_id"].endswith(".apps.googleusercontent.com")
        assert installed["client_secret"]
    except (ValueError, KeyError, TypeError, AssertionError, AttributeError):
        raise SystemExit("OBS_YOUTUBE_CLIENT_JSON must contain a Google Desktop OAuth client") from None
    raw = json.dumps({"installed": installed}, separators=(",", ":"))
pathlib.Path(sys.argv[1]).write_text(
    "#pragma once\ninline constexpr const char *CherriesBundledYouTubeJSON = "
    + json.dumps(raw, ensure_ascii=True) + ";\n"
)
