#!/bin/bash
set -euo pipefail
export CHERRIES_TEST_ARTIFACTS="$(realpath "${1:-studio-results}")"
mkdir -p "$CHERRIES_TEST_ARTIFACTS"
scratch=$(mktemp -d)
export XDG_CONFIG_HOME="$scratch/config" XDG_RUNTIME_DIR="$scratch/runtime"
mkdir -p "$XDG_CONFIG_HOME/obs-studio" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
export QT_QPA_PLATFORM=xcb DISPLAY=:99 LIBGL_ALWAYS_SOFTWARE=1
export OBS_LEGACY_PLUGINS_PATH="$(realpath studio-test-plugins)" OBS_LEGACY_PLUGINS_DATA_PATH="$scratch/data"
mkdir -p "$scratch/data/studio-smoke"
pids=()
cleanup() { for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done; wait 2>/dev/null || true; rm -rf "$scratch"; }
trap cleanup EXIT
python3 - <<'PY'
import json,os,pathlib
r=pathlib.Path(os.environ['XDG_CONFIG_HOME'])/'obs-studio'
(r/'global.ini').write_text('[General]\nLastVersion=553648128\n[Basic]\nProfile=StudioTest\nProfileDir=StudioTest\n')
(r/'user.ini').write_text('[General]\nFirstRun=true\n[Basic]\nProfile=StudioTest\nProfileDir=StudioTest\n[BasicWindow]\nWarnBeforeStartingStream=false\nWarnBeforeStoppingStream=false\n')
p=r/'basic/profiles/StudioTest';p.mkdir(parents=True)
(p/'basic.ini').write_text('[General]\nName=StudioTest\n[Video]\nBaseCX=640\nBaseCY=360\nOutputCX=640\nOutputCY=360\nFPSType=0\nFPSCommon=30\n[Output]\nMode=Simple\n[SimpleOutput]\nStreamEncoder=x264\nVBitrate=500\nABitrate=128\nUseAdvanced=false\nPreset=ultrafast\n[Audio]\nSampleRate=48000\nChannelSetup=Stereo\n')
(p/'service.json').write_text(json.dumps({'type':'rtmp_custom','settings':{'server':'rtmp://127.0.0.1:19350/live','key':'main'}}))
for name in ['aitum-multistream','vertical-canvas']:(r/'plugin_config'/name).mkdir(parents=True)
v={'bitrate':500,'rate_control':'CBR','keyint_sec':2,'preset':'ultrafast'}
shared={'name':'Shared Test','stream_server':'rtmp://127.0.0.1:19351/live','stream_key':'shared','advanced':True,'video_encoder':'','audio_encoder':'ffmpeg_aac','audio_encoder_settings':{'bitrate':128},'audio_track':1}
(r/'plugin_config/aitum-multistream/config.json').write_text(json.dumps({'profiles':[{'name':'StudioTest','outputs':[shared]}]}))
portrait=dict(shared,name='Portrait Test',stream_server='rtmp://127.0.0.1:19352/live',stream_key='portrait',video_encoder='obs_x264',video_encoder_settings=v,enabled=True)
(r/'plugin_config/vertical-canvas/config.json').write_text(json.dumps({'canvas':[{'width':360,'height':640,'stream_outputs':[portrait],'stream_advanced_settings':True,'stream_encoder':'obs_x264','stream_encoder_settings':v,'audio_bitrate':128,'backtrack':False}]}))
PY
Xvfb :99 -screen 0 1600x1000x24 -nolisten tcp > "$CHERRIES_TEST_ARTIFACTS/xvfb.log" 2>&1 & pids+=("$!")
for attempt in {1..30}; do [ -S /tmp/.X11-unix/X99 ] && break; sleep 0.2; done
for pair in '19350 main' '19351 shared' '19352 portrait'; do
    read -r port name <<< "$pair"
    ffmpeg -nostdin -y -listen 1 -i "rtmp://127.0.0.1:$port/live/$name" -c copy "$CHERRIES_TEST_ARTIFACTS/$name.flv" > "$CHERRIES_TEST_ARTIFACTS/$name-receiver.log" 2>&1 & pids+=("$!")
done
result=0
timeout 100s /opt/cherries-obs/bin/obs --disable-shutdown-check --disable-updater --profile StudioTest --allow-opengl > "$CHERRIES_TEST_ARTIFACTS/obs.log" 2>&1 || result=$?
cat "$CHERRIES_TEST_ARTIFACTS/obs.log"
test "$result" = 0
test -f "$CHERRIES_TEST_ARTIFACTS/studio-pass.txt"
python3 - <<'PY'
import json,os,pathlib
r=pathlib.Path(os.environ['XDG_CONFIG_HOME'])/'obs-studio/plugin_config'
main=json.loads((r/'aitum-multistream/config.json').read_text())['profiles'][0]['outputs'][0]
portrait=json.loads((r/'vertical-canvas/config.json').read_text())['canvas'][0]['stream_outputs'][0]
assert main['cherries_yt_broadcast_id']=='landscape-test'
assert portrait['cherries_yt_broadcast_id']=='portrait-test'
assert main['cherries_yt_account_id']==portrait['cherries_yt_account_id']=='test-channel'
assert main['cherries_yt_stream_id']!=portrait['cherries_yt_stream_id']
assert main['stream_key']=='shared' and portrait['stream_key']=='portrait'
print('Destination bindings persist independently and share one account identity')
PY
for name in main shared portrait; do
    ffprobe -v error -show_entries stream=codec_type,width,height -of json "$CHERRIES_TEST_ARTIFACTS/$name.flv" > "$CHERRIES_TEST_ARTIFACTS/$name-streams.json"
    ffmpeg -nostdin -i "$CHERRIES_TEST_ARTIFACTS/$name.flv" -vn -af volumedetect -f null - > "$CHERRIES_TEST_ARTIFACTS/$name-volume.txt" 2>&1
done
python3 - <<'PY'
import json,os,pathlib,re
r=pathlib.Path(os.environ['CHERRIES_TEST_ARTIFACTS'])
for name in ['main','shared','portrait']:
 s=json.loads((r/f'{name}-streams.json').read_text())['streams']; video=next(x for x in s if x['codec_type']=='video')
 assert (video['width'],video['height'])==((360,640) if name=='portrait' else (640,360)),(name,video)
 volume=float(re.search(r'mean_volume: ([-\d.]+) dB',(r/f'{name}-volume.txt').read_text())[1])
 assert volume < -65 if name=='main' else volume > -40,(name,volume)
 print(f'{name}: expected video dimensions and audio track verified; mean volume {volume} dB')
PY
