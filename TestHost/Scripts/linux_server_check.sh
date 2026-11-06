#!/usr/bin/env bash
# 사용: linux_server_check.sh <패키지 경로> <RTSP URL> [--software-only|--decode-only] [--codec hevc --fps 30]
set -uo pipefail
set +x
if [[ $# -lt 2 ]]; then printf '사용법: %s <패키지 경로> <RTSP URL> [--software-only|--decode-only] [--codec hevc --fps 30]\n' "$0" >&2; exit 2; fi
package=$(realpath -- "$1") || exit 2
url=$2
scope=gpu
expected_codec=
source_fps=60
codec_mode=0
shift 2
while [[ $# -gt 0 ]]; do
	case $1 in
		--software-only) scope=software; shift ;;
		--decode-only) scope=decode; shift ;;
		--codec) [[ $# -ge 2 ]] || exit 2; expected_codec=$2; codec_mode=1; shift 2 ;;
		--fps) [[ $# -ge 2 ]] || exit 2; source_fps=$2; codec_mode=1; shift 2 ;;
		*) printf '알 수 없는 옵션\n' >&2; exit 2 ;;
	esac
done
[[ $source_fps =~ ^[1-9][0-9]*$ ]] && (( source_fps <= 240 )) || exit 2
[[ -z $expected_codec || $expected_codec =~ ^[a-zA-Z0-9_]+$ ]] || exit 2
duration=80
game_fps=144
if (( codec_mode )); then duration=100; game_fps=60; fi
helper=$(cd -- "$(dirname -- "$0")" && pwd)/linux_server_check.py
exe=$package/DTCctvTestHost/Binaries/Linux/DTCctvTestHost
[[ -x $exe && -f $helper ]] || { printf '실행 파일 또는 점검 도우미 없음\n' >&2; exit 2; }
python_bin=$(command -v python3 || true)
if [[ -z $python_bin && -x /usr/libexec/platform-python ]]; then python_bin=/usr/libexec/platform-python; fi
[[ -n $python_bin ]] || { printf 'FAIL | Python3 실행 파일 없음\n' >&2; exit 2; }
export PYTHONIOENCODING=utf-8
out=$(pwd)/cctv-linux-check-$(date +%Y%m%d-%H%M%S)-$$
mkdir -m 700 -- "$out" || exit 2
runtime=$(mktemp -d "$out/.runtime.XXXXXX") || exit 2
game_pid= filter_pid= start_ticks=
cleanup() {
	if [[ -n $game_pid && -r /proc/$game_pid/stat ]]; then
		now_ticks=$(awk '{print $22}' "/proc/$game_pid/stat")
		if [[ $now_ticks == "$start_ticks" ]]; then
			kill -TERM "$game_pid" 2>/dev/null || true
			sleep 2
			if [[ -r /proc/$game_pid/stat && $(awk '{print $22}' "/proc/$game_pid/stat") == "$start_ticks" ]]; then kill -KILL "$game_pid" 2>/dev/null || true; fi
		fi
	fi
	[[ -n $game_pid ]] && wait "$game_pid" 2>/dev/null || true
	[[ -n $filter_pid ]] && wait "$filter_pid" 2>/dev/null || true
	if [[ $runtime == "$out"/.runtime.* && -d $runtime ]]; then rm -rf -- "$runtime"; fi
}
trap cleanup EXIT INT TERM
ulimit -c 0
mkfifo "$runtime/output.fifo"
export DTCCTV_CHECK_URL=$url
"$python_bin" "$helper" redact < "$runtime/output.fifo" > "$out/game.log" &
filter_pid=$!
args=(-RenderOffScreen -vulkan -ForceRes -ResX=1920 -ResY=1080 -unattended -nosound -nosplash
	-stdout -FullStdOutLogOutput -abslog=/dev/null -NoCrashReports -CrashForUAT
	"-userdir=$runtime/ue" -CctvUrlEnv=DTCCTV_CHECK_URL -CctvCount=16 "-CctvSeconds=$duration"
	"-CctvCsv=$out/samples.csv" "-CctvShot=$out/final.png" "-ExecCmds=r.VSync 0,t.MaxFPS $game_fps")
if [[ $scope == software ]]; then args+=(-nullrhi -CctvSoftware); fi
if [[ $scope == decode ]]; then args+=(-nullrhi); fi
"$exe" "${args[@]}" > "$runtime/output.fifo" 2>&1 &
game_pid=$!
start_ticks=$(awk '{print $22}' "/proc/$game_pid/stat" 2>/dev/null || true)
printf '{"pid":%s,"start_ticks":"%s"}\n' "$game_pid" "$start_ticks" > "$out/owned-process.json"
deadline=$((SECONDS+150))
timed_out=0
while kill -0 "$game_pid" 2>/dev/null; do
	if (( SECONDS >= deadline )); then timed_out=1; break; fi
	sleep 1
done
if (( timed_out )); then
	if [[ -r /proc/$game_pid/stat && $(awk '{print $22}' "/proc/$game_pid/stat") == "$start_ticks" ]]; then kill -TERM "$game_pid" 2>/dev/null || true; fi
	sleep 2
	if [[ -r /proc/$game_pid/stat && $(awk '{print $22}' "/proc/$game_pid/stat") == "$start_ticks" ]]; then kill -KILL "$game_pid" 2>/dev/null || true; fi
fi
wait "$game_pid"
code=$?
game_pid=
wait "$filter_pid"
filter_pid=
unset DTCCTV_CHECK_URL
"$python_bin" "$helper" analyze "$out" "$scope" "$code" "$timed_out" "$expected_codec" "$source_fps" "$codec_mode"
