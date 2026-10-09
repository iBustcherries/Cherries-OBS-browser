"""Bundle the public Twitch application ID without including a client secret."""
import json
import os
import re
import sys
from pathlib import Path
default_header = Path(__file__).parent / "twitch" / "CherriesTwitchClient.hpp"
match = re.search(r'CherriesBundledTwitchId = ("[^"\n]*");', default_header.read_text())
if not match:
    raise SystemExit("Missing bundled Twitch application ID")
client = os.environ.get("CHERRIES_TWITCH_CLIENT_ID", "").strip() or json.loads(match.group(1))
if client and not re.fullmatch(r"[A-Za-z0-9]{10,128}", client):
    raise SystemExit("CHERRIES_TWITCH_CLIENT_ID must be a Twitch Public application Client ID")
Path(sys.argv[1]).write_text("#pragma once\ninline constexpr const char *CherriesBundledTwitchId = " + json.dumps(client) + ";\n")
