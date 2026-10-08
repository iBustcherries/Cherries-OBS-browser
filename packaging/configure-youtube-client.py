"""Generate desktop OAuth application configuration without logging its contents."""
import argparse
import json
import os
import re
from pathlib import Path


def configure(raw, required=False):
    if not raw.strip():
        if required:
            raise ValueError("Set the CHERRIES_YOUTUBE_CLIENT_JSON repository secret to the Google Desktop app client JSON before building this release.")
        return "", ""
    if len(raw.encode("utf-8")) > 65536:
        raise ValueError("Google Desktop app client JSON is too large.")
    try:
        document = json.loads(raw)
    except (ValueError, TypeError):
        raise ValueError("Google Desktop app client JSON is malformed.") from None
    installed = document.get("installed") if isinstance(document, dict) else None
    if not isinstance(installed, dict):
        raise ValueError("Use a Google Desktop app OAuth client, not a Web app or service account.")
    client_id = installed.get("client_id")
    secret = installed.get("client_secret")
    if not isinstance(client_id, str) or not re.fullmatch(r"[A-Za-z0-9_-]+\.apps\.googleusercontent\.com", client_id):
        raise ValueError("The Desktop app client ID is missing or invalid.")
    if not isinstance(secret, str) or not secret or any(ord(c) < 33 or ord(c) > 126 for c in secret):
        raise ValueError("The Desktop app client secret is missing or invalid.")
    return client_id, secret


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--required", action="store_true")
    args = parser.parse_args()
    try:
        client_id, secret = configure(os.environ.get("CHERRIES_YOUTUBE_CLIENT_JSON", ""), args.required)
    except ValueError as error:
        parser.exit(2, str(error) + "\n")
    args.output.write_text(
        "#pragma once\n"
        "// Generated desktop application configuration; not user access tokens.\n"
        f"inline constexpr const char *CherriesBundledYouTubeId = {json.dumps(client_id)};\n"
        f"inline constexpr const char *CherriesBundledYouTubeSecret = {json.dumps(secret)};\n"
    )


if __name__ == "__main__":
    main()
