#!/bin/bash
# End-to-end tests of the running program (Linux, headless with xvfb):
#  1. asynchronous (PBO) upload gives exactly the same picture as the direct one
#     for H.264, ProRes 10-bit and HAP Q;
#  2. redundancy: a backup follows a GO given to the main, jumps to the main's
#     video time, and takes over (running its command) when the main dies.
# Needs: ffmpeg, xvfb-run, python3. Usage: scripts/test-integration.sh [build dir]
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LUMEN="$(cd "${1:-$ROOT/build}" && pwd)/lumen"
WORK="$(mktemp -d)"
trap 'pkill -f "$WORK" 2>/dev/null || true; rm -rf "$WORK"' EXIT
cd "$WORK"
fail=0

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
  xvfb-run -a "$LUMEN" p.lumen --frames 4 --launch 1 --no-outputs --screenshot-canvas a.ppm >/dev/null 2>&1
  xvfb-run -a "$LUMEN" p.lumen --frames 4 --launch 1 --no-outputs --sync-upload --screenshot-canvas b.ppm >/dev/null 2>&1
  if [ -s a.ppm ] && cmp -s a.ppm b.ppm; then echo "ok   $v"; else echo "FAIL $v: images differ"; fail=1; fi
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
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
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
}
ok = True
for name, passed in checks.items():
    print(("ok   " if passed else "FAIL ") + name)
    ok = ok and passed
if not ok: print(log)
sys.exit(0 if ok else 1)
EOF
python3 drive.py "$LUMEN" "$WORK" || fail=1

[ $fail = 0 ] && echo "integration tests: OK"
exit $fail
