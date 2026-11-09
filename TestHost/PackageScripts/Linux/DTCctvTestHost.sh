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

mode="${1:-}"
case "$mode" in
    -h|--help|help)
        usage
        ;;
    ""|-*)
        run "$@"
        ;;
    ps|pixelstreaming)
        shift
        url="ws://127.0.0.1:8888"
        if (( $# > 0 )) && [[ "$1" != -* ]]; then url="$1"; shift; fi
        run "-PixelStreamingURL=$url" "$@"
        ;;
    view)
        shift
        (( $# >= 1 )) || usage
        list="$1"; shift
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
