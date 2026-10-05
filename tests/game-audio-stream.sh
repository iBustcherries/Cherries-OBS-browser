#!/bin/bash
set -euo pipefail
export XDG_RUNTIME_DIR
XDG_RUNTIME_DIR=$(mktemp -d)
export XDG_CONFIG_HOME="$XDG_RUNTIME_DIR/config"
mkdir -p "$XDG_CONFIG_HOME/pipewire/pipewire.conf.d"
cat > "$XDG_CONFIG_HOME/pipewire/pipewire.conf.d/99-cherries-test.conf" <<'EOF'
context.objects = [
  { factory = adapter
    args = {
      factory.name = support.null-audio-sink
      node.name = cherries-test-sink
      media.class = Audio/Sink
      audio.position = [ FL FR ]
    }
  }
]
EOF
pipewire > "$XDG_RUNTIME_DIR/pipewire.log" 2>&1 &
server=$!
for attempt in {1..30}; do
    if [ -S "$XDG_RUNTIME_DIR/pipewire-0" ]; then break; fi
    sleep 0.2
done
wireplumber > "$XDG_RUNTIME_DIR/wireplumber.log" 2>&1 &
manager=$!
player=''
cleanup() {
    if [ -n "$player" ]; then kill "$player" 2>/dev/null || true; fi
    kill "$manager" "$server" 2>/dev/null || true
    rm -rf "$XDG_RUNTIME_DIR"
}
trap cleanup EXIT
ready=false
for attempt in {1..30}; do
    if pw-metadata -n default 0 default.audio.sink '{"name":"cherries-test-sink"}' >/dev/null 2>&1; then
        ready=true
        break
    fi
    sleep 1
done
if [ "$ready" != true ]; then
    cat "$XDG_RUNTIME_DIR/pipewire.log" "$XDG_RUNTIME_DIR/wireplumber.log"
    exit 1
fi
python3 - "$XDG_RUNTIME_DIR" <<'PY'
import array, math, pathlib, sys
root = pathlib.Path(sys.argv[1])
for name, amplitude, frequency in [('game', .2, 440), ('unrelated', .6, 880)]:
    samples = array.array('f')
    for i in range(48000 * 30):
        sample = amplitude * math.sin(2 * math.pi * frequency * i / 48000)
        samples.extend([sample, sample])
    (root / (name + '.raw')).write_bytes(samples.tobytes())
PY
pw-cat --playback --target=cherries-test-sink --rate=48000 --channels=2 --format=f32 - < "$XDG_RUNTIME_DIR/unrelated.raw" > "$XDG_RUNTIME_DIR/player.log" 2>&1 &
player=$!
if ! ./game-audio-stream-test "$XDG_RUNTIME_DIR/game.raw" "$player"; then
    cat "$XDG_RUNTIME_DIR/pipewire.log" "$XDG_RUNTIME_DIR/wireplumber.log" "$XDG_RUNTIME_DIR/player.log"
    exit 1
fi
