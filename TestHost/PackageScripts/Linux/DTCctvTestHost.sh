#!/usr/bin/env bash
# CCTV 테스트 패키지 실행 스크립트 (Linux, 하나로 통합)
#
#   ./DTCctvTestHost.sh                               일반 창 (주소 입력 → 보기)
#   ./DTCctvTestHost.sh ps [ws://서버:8888]           Pixel Streaming
#   ./DTCctvTestHost.sh view <목록.json>              목록으로 바로 보기 (화면 필요)
#   ./DTCctvTestHost.sh check <목록.json> [초=130]    화면 없는 서버 점검 (-nullrhi)
#
# 모드 뒤의 나머지 인자는 UE에 그대로 전달한다. 모드 없이 '-'로 시작하는 인자만 주면 일반 창에 전달한다.
set -euo pipefail

# UE는 Linux에서 root 실행을 엔진 차원에서 거부한다(UnixPlatformMemory.cpp). 알아보기 힘든 abort 대신 안내하고 멈춘다.
if [ "$(id -u)" -eq 0 ]; then
    cat >&2 <<'MSG'
[DTCctvTestHost] root 계정으로는 실행할 수 없습니다(Unreal Engine 제한).
일반 계정으로 실행하세요. 예:
  useradd -m cctv                                   # 일반 계정이 없을 때 한 번
  chown -R cctv:cctv "<이 폴더>"                     # root로 압축을 풀었다면 한 번
  su - cctv -c 'cd "<이 폴더>" && ./DTCctvTestHost.sh ps ws://서버:8888'
sudo로 실행했다면 sudo 없이 실행하세요.
MSG
    exit 1
fi

root="$(cd -- "$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")" && pwd)"
bin="$root/DTCctvTestHost/Binaries/Linux"
exe="$bin/DTCctvTestHost"

# zip으로 옮기면 실행 권한이 빠질 수 있어 실행 파일 권한을 보정한다.
[ -x "$exe" ] || chmod +x "$exe"
export LD_LIBRARY_PATH="$bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

usage() {
    sed -n '2,9p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//' >&2
    exit 64
}

run() {
    exec "$exe" DTCctvTestHost -PixelStreamingEncoderCodec=H264 "$@"
}

has_display() { [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; }
has_arg() { local a; for a in "${@:2}"; do [ "${a,,}" = "${1,,}" ] && return 0; done; return 1; }

# 화면(X11/Wayland)이 없으면 창을 만들 수 없어 SDL 오류로 끝난다. 미리 안내한다.
need_display() {
    if has_display || has_arg -nullrhi "$@" || has_arg -RenderOffScreen "$@"; then return 0; fi
    cat >&2 <<'MSG'
[DTCctvTestHost] 화면(DISPLAY)이 없어 창을 띄울 수 없습니다. SSH 등 화면 없는 접속에서는:
  ./DTCctvTestHost.sh ps ws://서버:8888        # Pixel Streaming(화면 없이 자동으로 -RenderOffScreen)
  ./DTCctvTestHost.sh check <목록.json> [초]     # 디코딩 점검(-nullrhi)
서버에 모니터·데스크톱이 있으면 그 데스크톱 터미널에서 실행하세요.
다른 계정(su)으로 실행한다면 데스크톱 로그인 계정에서 먼저 'xhost +SI:localuser:<실행계정>'을 한 뒤
  DISPLAY=:0 ./DTCctvTestHost.sh   로 실행하세요(:0이 아니면 데스크톱에서 'echo $DISPLAY'로 확인).
MSG
    exit 1
}

mode="${1:-}"
case "$mode" in
    -h|--help|help)
        usage
        ;;
    ""|-*)
        need_display "$@"
        run "$@"
        ;;
    ps|pixelstreaming)
        shift
        url="ws://127.0.0.1:8888"
        if (( $# > 0 )) && [[ "$1" != -* ]]; then url="$1"; shift; fi
        if ! has_display && ! has_arg -RenderOffScreen "$@"; then
            echo "[DTCctvTestHost] 화면이 없어 -RenderOffScreen으로 실행합니다(GPU·Vulkan 필요)." >&2
            set -- -RenderOffScreen "$@"
        fi
        run "-PixelStreamingURL=$url" "$@"
        ;;
    view)
        shift
        (( $# >= 1 )) || usage
        list="$1"; shift
        need_display "$@"
        run -CctvView "-CctvCheckList=$list" "$@"
        ;;
    check)
        shift
        (( $# >= 1 )) || usage
        list="$1"; shift
        seconds=130
        if (( $# > 0 )) && [[ "$1" != -* ]]; then seconds="$1"; shift; fi
        run -nullrhi -unattended "-CctvCheckList=$list" "-CctvCheckSeconds=$seconds" "$@"
        ;;
    *)
        echo "알 수 없는 모드: $mode" >&2
        usage
        ;;
esac
