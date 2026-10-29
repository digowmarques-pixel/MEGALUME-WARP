#!/bin/bash
# End-to-end tests of the running program (Linux, headless with xvfb):
#  1. asynchronous (PBO) upload gives exactly the same picture as the direct one
#     for H.264, ProRes 10-bit and HAP Q;
#  2. redundancy: a backup follows a GO given to the main, jumps to the main's
#     video time, and takes over (running its command) when the main dies.
#  3. regressions found in the review of v0.10 (sound card unplugged, crossfade,
#     seek past the end of a frozen clip, "dmx" command next to an LED strip).
# Needs: ffmpeg, xvfb-run, python3. Usage: scripts/test-integration.sh [build dir]
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LUMEN="$(cd "${1:-$ROOT/build}" && pwd)/lumen"
WORK="$(mktemp -d)"
trap 'pkill -f "$WORK" 2>/dev/null || true; rm -rf "$WORK"' EXIT
cd "$WORK"
fail=0
# A sanitizer build (ASan / UBSan / TSan) reports to stderr: any report fails the test.
sanitizer() { grep -E "ERROR: AddressSanitizer|runtime error:|WARNING: ThreadSanitizer: data race.*src/" "$@" >/dev/null 2>&1; }

echo "== media"
ffmpeg -v error -y -f lavfi -i testsrc2=size=1280x720:rate=30 -t 2 -c:v libx264 -pix_fmt yuv420p h264.mp4
ffmpeg -v error -y -f lavfi -i testsrc2=size=1280x720:rate=30 -t 2 -c:v prores_ks -profile:v 3 -pix_fmt yuv422p10le prores.mov
ffmpeg -v error -y -f lavfi -i "testsrc2=size=1280x720:rate=30,format=rgba" -t 2 -c:v hap -format hap_q hapq.mov
ffmpeg -v error -y -f lavfi -i testsrc2=size=640x360:rate=30 -f lavfi -i "sine=frequency=440:sample_rate=48000" \
  -t 4 -c:v libx264 -g 48 -pix_fmt yuv420p -c:a aac av.mp4

project() { # $1 = media file
  cat <<EOF
{"format":"lumen","version":1,"canvas":{"width":1280,"height":720},
 "osc":{"enabled":true,"port":18000},
 "redundancy":{"timeout":1.0,"tolerance":0.07,"takeover":"osc 127.0.0.1:19999 /matriz/entrada 2"},
 "library":[{"id":1,"name":"v","type":0,"path":"$WORK/$1"},{"id":2,"name":"card","type":2,"generator":0}],
 "layers":[{"id":1,"name":"L1"},{"id":2,"name":"L2"}],
 "columns":[{"id":5,"name":"A","transition":0.3,"cells":[{"layer":1,"media":1,"loop":true,"speed":1}]},
            {"id":6,"name":"B","transition":0.3,"cells":[{"layer":1,"media":1,"loop":true,"speed":1},{"layer":2,"media":2,"loop":true,"speed":1}]}]}
EOF
}

echo "== PBO upload = direct upload"
for v in h264.mp4 prores.mov hapq.mov; do
  project $v > p.lumen
  xvfb-run -a "$LUMEN" p.lumen --frames 4 --launch 1 --no-outputs --screenshot-canvas a.ppm >a.log 2>&1
  xvfb-run -a "$LUMEN" p.lumen --frames 4 --launch 1 --no-outputs --sync-upload --screenshot-canvas b.ppm >b.log 2>&1
  if [ -s a.ppm ] && cmp -s a.ppm b.ppm; then echo "ok   $v"; else echo "FAIL $v: images differ"; fail=1; fi
  if sanitizer a.log b.log; then echo "FAIL $v: sanitizer report"; grep -m3 -E "AddressSanitizer|runtime error" a.log b.log; fail=1; fi
done

echo "== redundancy (main + backup on this computer)"
project av.mp4 > show.lumen
cat > drive.py <<'EOF'
import socket, subprocess, sys, time
lumen, work = sys.argv[1], sys.argv[2]
def osc(addr):
    pad = lambda b: b + b"\0" * (4 - len(b) % 4)
    return pad(addr.encode()) + pad(b",")
rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); rx.bind(("127.0.0.1", 19999)); rx.settimeout(0.2)
common = ["xvfb-run", "-a", lumen, work + "/show.lumen", "--no-outputs"]
main = subprocess.Popen(common + ["--launch", "1", "--role", "main", "--sync-port", "17400",
                        "--sync-peer", "127.0.0.1:17401", "--quit-after", "60"],
                        stdout=open(work + "/main.log", "w"), stderr=subprocess.STDOUT, start_new_session=True)
time.sleep(5) # the backup starts late: it must jump to the main's video time
backup = subprocess.Popen(common + ["--role", "backup", "--sync-port", "17401", "--sync-peer", "127.0.0.1:17400",
                          "--quit-after", "16"], stdout=open(work + "/backup.log", "w"), stderr=subprocess.STDOUT)
time.sleep(5)
socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(osc("/lumen/go"), ("127.0.0.1", 18000))
time.sleep(3)
import os, signal
os.killpg(main.pid, signal.SIGKILL) # the main computer dies
got = b""
t0 = time.time()
while time.time() - t0 < 8 and not got:
    try: got = rx.recvfrom(2000)[0]
    except socket.timeout: pass
backup.wait(timeout=60)
log = open(work + "/backup.log").read()
checks = {
  "segue o GO dado na principal": "[test] column=2" in log,
  "salta para o tempo da principal": "(tempo da principal)" in log,
  "assume quando a principal cai": "assumiu o show" in log,
  "roda o comando de troca": got.startswith(b"/matriz/entrada"),
  "sem erros do sanitizer": not any(k in open(work + f).read() for f in ("/main.log", "/backup.log")
                                    for k in ("ERROR: AddressSanitizer", "runtime error:")),
}
ok = True
for name, passed in checks.items():
    print(("ok   " if passed else "FAIL ") + name)
    ok = ok and passed
if not ok: print(log)
sys.exit(0 if ok else 1)
EOF
python3 drive.py "$LUMEN" "$WORK" || fail=1

echo "== regressions found in the review of v0.10"
ffmpeg -v error -y -f lavfi -i testsrc2=size=320x240:rate=25 -t 4 -c:v libx264 -pix_fmt yuv420p freeze.mp4
cat > review.py <<'EOF2'
import json, socket, struct, subprocess, sys, threading, time
lumen, work = sys.argv[1], sys.argv[2]
ok = True
def check(name, cond, detail=""):
    global ok
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  -> " + str(detail)))
    ok = ok and cond
def run(project, args, name):
    open(work + "/" + name + ".lumen", "w").write(json.dumps(project))
    r = subprocess.run(["xvfb-run", "-a", lumen, work + "/" + name + ".lumen", "--no-outputs"] + args,
                       capture_output=True, text=True, timeout=180)
    out = r.stdout + r.stderr
    check(name + ": sem erros do sanitizer", not any(k in out for k in ("ERROR: AddressSanitizer", "runtime error:")))
    return out
def ppm(path):
    d = open(path, "rb").read()
    return d[d.index(b"255\n") + 4:]
lib_av = {"id": 1, "name": "v", "type": 0, "path": work + "/av.mp4"}
# Sound card that stops (unplugged): the video keeps going, the card is reopened, the sound paces again.
out = run({"format": "lumen", "version": 1, "canvas": {"width": 320, "height": 180}, "library": [lib_av],
           "layers": [{"id": 1, "name": "L1"}], "columns": [{"id": 5, "name": "A", "transition": 0,
           "cells": [{"layer": 1, "media": 1, "loop": True, "speed": 1}]}]},
          ["--launch", "1", "--quit-after", "8", "--test-audio-stall", "2"], "audio_stall")
t = [float(l.split("t=")[1].split()[0]) for l in out.splitlines() if "layer L1: t=" in l]
check("placa de som parou: o vídeo continuou", bool(t) and t[0] > 6, t)
check("placa de som parou: reaberta e o som volta a comandar", "reabrindo" in out and "(paces)" in out)
# White to white crossfade: no dip in the middle (was 25% darker).
white = {"id": 2, "name": "w", "type": 2, "generator": 1, "color1": [1, 1, 1, 1], "size": [320, 180]}
xf = {"format": "lumen", "version": 1, "canvas": {"width": 320, "height": 180, "background": [0, 0, 0, 1]},
      "library": [white, dict(white, id=3)], "layers": [{"id": 1, "name": "L1"}],
      "columns": [{"id": 5, "name": "A", "transition": 0, "autoGo": {"wait": 0.1, "afterTransition": False, "target": 0},
                   "cells": [{"layer": 1, "media": 2}]},
                  {"id": 6, "name": "B", "transition": 1.0, "cells": [{"layer": 1, "media": 3}]}]}
run(xf, ["--launch", "1", "--frames", "37", "--screenshot-canvas", work + "/xf.ppm"], "crossfade")
check("crossfade branco -> branco sem escurecer no meio", min(ppm(work + "/xf.ppm")[::3]) >= 250)
# A clip that holds its last frame, sent past its end: the last frame stays (was black).
fr = {"format": "lumen", "version": 1, "canvas": {"width": 320, "height": 240}, "osc": {"enabled": True, "port": 18350},
      "library": [{"id": 1, "name": "f", "type": 0, "path": work + "/freeze.mp4"}], "layers": [{"id": 1, "name": "L1"}],
      "columns": [{"id": 5, "name": "A", "transition": 0, "cells": [{"layer": 1, "media": 1, "loop": False, "end": 1}]}]}
def seek_later():
    time.sleep(2)
    pad = lambda b: b + b"\0" * (4 - len(b) % 4)
    socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(pad(b"/lumen/layer/1/time") + pad(b",f") + struct.pack(">f", 10.0), ("127.0.0.1", 18350))
threading.Thread(target=seek_later, daemon=True).start()
run(fr, ["--launch", "1", "--quit-after", "4", "--screenshot-canvas", work + "/fr.ppm"], "freeze_seek")
check("clipe congelado, posicionado depois do fim: mostra o último quadro", max(ppm(work + "/fr.ppm")[::50]) > 20)
# "dmx" command on a universe shared with an LED strip: merged and resent (was 1 packet, zeroed by the LEDs).
pk = []
def rx():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", 6454)); s.settimeout(0.5); t0 = time.time()
    while time.time() - t0 < 15:
        try: pk.append(s.recvfrom(2000)[0])
        except socket.timeout: pass
threading.Thread(target=rx, daemon=True).start()
area = [[0, 0], [320, 0], [320, 180], [0, 180]]
run({"format": "lumen", "version": 1, "canvas": {"width": 320, "height": 180},
     "library": [{"id": 1, "name": "solid", "type": 2, "generator": 1, "color1": [1.0, 0.5, 0.0, 1.0]}],
     "layers": [{"id": 1, "name": "L1"}],
     "dmx": {"master": 1, "devices": [{"id": 1, "name": "local", "enabled": True, "protocol": 0, "host": "127.0.0.1", "rate": 40}],
             "leds": [{"id": 1, "name": "rgb", "enabled": True, "size": [4, 1], "format": 0, "wide": False, "gamma": 1.0,
                       "brightness": 1.0, "device": 1, "universe": 3, "channel": 1, "area": area, "canvas": 0}]},
     "columns": [{"id": 5, "name": "A", "transition": 0, "commands": ["dmx 3 500 255"],
                  "cells": [{"layer": 1, "media": 1, "loop": True, "speed": 1}]}]},
    ["--launch", "1", "--quit-after", "4"], "dmx_led")
time.sleep(0.6)
u3 = [p for p in pk if p[:8] == b"Art-Net\0" and struct.unpack("<H", p[14:16])[0] == 3 and len(p) >= 518]
both = [p for p in u3 if p[517] == 255 and p[18] == 255]
check("comando dmx + fita LED no mesmo universo: os dois juntos, reenviados", len(both) >= 10, (len(u3), len(both)))
# Pause by OSC for 2 s, then play: the clip time stops and the sound paces the picture again.
def pause_later():
    pad = lambda b: b + b"\0" * (4 - len(b) % 4)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    time.sleep(2.5)
    sock.sendto(pad(b"/lumen/layer/1/pause") + pad(b","), ("127.0.0.1", 18351))
    time.sleep(2)
    sock.sendto(pad(b"/lumen/layer/1/play") + pad(b","), ("127.0.0.1", 18351))
threading.Thread(target=pause_later, daemon=True).start()
out = run({"format": "lumen", "version": 1, "canvas": {"width": 320, "height": 180}, "osc": {"enabled": True, "port": 18351},
           "library": [lib_av], "layers": [{"id": 1, "name": "L1"}], "columns": [{"id": 5, "name": "A", "transition": 0,
           "cells": [{"layer": 1, "media": 1, "loop": True, "speed": 1}]}]},
          ["--launch", "1", "--quit-after", "7"], "pause")
t = [float(l.split("t=")[1].split()[0]) for l in out.splitlines() if "layer L1: t=" in l]
check("pausa de 2 s: o tempo do clipe parou e voltou", bool(t) and 3.0 < t[0] < 5.6, t)
check("pausa: o som volta a comandar a imagem", "(paces)" in out)
sys.exit(0 if ok else 1)
EOF2
python3 review.py "$LUMEN" "$WORK" || fail=1

[ $fail = 0 ] && echo "integration tests: OK"
exit $fail
