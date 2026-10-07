#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
hook=$(realpath "$1")
architecture=${2:-64}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
compiler_flags=()
if [[ "$architecture" == 32 ]]; then compiler_flags=(-m32); fi

# The installed hook must load GL lazily, after the game's module relocations.
if readelf -d "$hook" | grep -E 'NEEDED.*\[lib(GL|OpenGL|EGL)'; then
    echo 'Capture hook eagerly links a graphics library' >&2
    exit 1
fi
gcc "${compiler_flags[@]}" -std=gnu11 -Wall -Wextra -Werror -fPIC -shared \
    -DCHERRIES_TEST_GL_MODULE "$repo/tests/opengl-startup.c" -o "$work/sdl-fixture.so"
gcc "${compiler_flags[@]}" -std=gnu11 -Wall -Wextra -Werror \
    "$repo/tests/opengl-startup.c" -ldl -o "$work/startup-driver"
# Reproduce the old dependency order independently of the corrected hook.
printf 'void capture_fixture(void) {}\n' > "$work/eager-hook.c"
gcc "${compiler_flags[@]}" -fPIC -shared "$work/eager-hook.c" \
    -Wl,--no-as-needed -lGL -o "$work/eager-hook.so"
python3 - "$work" "$hook" <<'PY'
import os
import pathlib
import resource
import signal
import subprocess
import sys

work = pathlib.Path(sys.argv[1])
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
command = [str(work / "startup-driver"), str(work / "sdl-fixture.so")]
environment = dict(os.environ)
environment.pop("LD_PRELOAD", None)
environment.pop("OBS_VKCAPTURE_GLVULKAN", None)
environment["LD_PRELOAD"] = str(work / "eager-hook.so")
broken = subprocess.run(command, env=environment, capture_output=True, text=True)
assert broken.returncode == -signal.SIGSEGV, (broken.returncode, broken.stdout, broken.stderr)
print("Reproduced Dead Cells-style function-pointer write crash with eager GL dependency")
environment["LD_PRELOAD"] = sys.argv[2]
fixed = subprocess.run(command, env=environment, capture_output=True, text=True)
print(fixed.stdout, end="")
print(fixed.stderr, end="", file=sys.stderr)
assert fixed.returncode == 0, fixed.returncode
PY
