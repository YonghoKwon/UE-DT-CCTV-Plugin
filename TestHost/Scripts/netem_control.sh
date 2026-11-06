#!/bin/sh
# 이 작업이 새로 만든 플레이어와 같은 네트워크 네임스페이스의 도구 컨테이너에서만 실행한다.
set -eu
export PATH="/usr/sbin:/sbin:$PATH"
if [ ! -f /.dockerenv ] || [ "${DTCCTV_NETEM_OWNED:-}" != "1" ]; then
  printf '%s\n' '자체 Docker 시험 컨테이너에서만 실행할 수 있습니다.' >&2
  exit 2
fi
action=${1:-status}
interface=${DTCCTV_INTERFACE:-eth0}
ifb=dtcctvifb
limit=${DTCCTV_NETEM_LIMIT:-65536}
case "$limit" in ''|*[!0-9]*) exit 2;; esac
[ "$limit" -gt 0 ] || exit 2
state=/tmp/dtcctv-netem-owned
case "$interface" in ''|*[!a-zA-Z0-9_.:-]*) exit 2;; esac
if ! command -v tc >/dev/null 2>&1; then
  if [ -x /bin/busybox ] && /bin/busybox tc qdisc show >/dev/null 2>&1; then
    tc() { /bin/busybox tc "$@"; }
  else
    printf '%s\n' 'tc가 없습니다.' >&2
    exit 20
  fi
fi
command -v ip >/dev/null 2>&1 || { printf '%s\n' 'ip가 없습니다.' >&2; exit 20; }

case "$action" in
  init)
    [ ! -e "$state" ] || { printf '%s\n' '이미 준비된 시험입니다.' >&2; exit 2; }
    # 기존 설정을 교체하지 않는다. 새 시험 인터페이스/네임스페이스여야 한다.
    ip link add "$ifb" type ifb
    trap 'ip link del "$ifb" 2>/dev/null || true' EXIT
    ip link set "$ifb" up
    tc qdisc add dev "$interface" handle ffff: ingress
    trap 'tc qdisc del dev "$interface" ingress 2>/dev/null || true; ip link del "$ifb" 2>/dev/null || true' EXIT
    if [ -n "${DTCCTV_SOURCE_PORT:-}" ]; then
      case "$DTCCTV_SOURCE_PORT" in *[!0-9]*|'') exit 2;; esac
      [ "$DTCCTV_SOURCE_PORT" -gt 0 ] && [ "$DTCCTV_SOURCE_PORT" -le 65535 ] || exit 2
      # Windows TCP 중계에서는 원본 RTSP 서버에서 들어오는 데이터에만 장애를 건다.
      tc filter add dev "$interface" parent ffff: protocol ip u32 match ip protocol 6 0xff match ip sport "$DTCCTV_SOURCE_PORT" 0xffff action mirred egress redirect dev "$ifb"
    else
      tc filter add dev "$interface" parent ffff: protocol all u32 match u32 0 0 action mirred egress redirect dev "$ifb"
    fi
    tc qdisc add dev "$ifb" root netem limit "$limit" delay 0ms
    printf '%s\n' "$interface" > "$state"
    cat /proc/sys/kernel/random/uuid > "$state.epoch"
    trap - EXIT
    ;;
  apply)
    [ -f "$state" ] && [ "$(cat "$state")" = "$interface" ] || exit 2
    scenario=${2:-}
    case "$scenario" in
      N1) tc qdisc replace dev "$ifb" root netem limit "$limit" delay 50ms ;;
      N2) tc qdisc replace dev "$ifb" root netem limit "$limit" delay 150ms 30ms distribution normal ;;
      N3) tc qdisc replace dev "$ifb" root netem limit "$limit" loss random 0.5% ;;
      N4) tc qdisc replace dev "$ifb" root netem limit "$limit" loss random 2% ;;
      N5)
        aggregate=${3:-}
        case "$aggregate" in ''|*[!0-9]*) printf '%s\n' '실측한 전체 비트레이트(bps)가 필요합니다.' >&2; exit 2;; esac
        [ "${#aggregate}" -le 15 ] || exit 2
        [ "$aggregate" -gt 0 ] || exit 2
        rate=$((aggregate * 4 / 5))
        [ "$rate" -gt 0 ] || exit 2
        tc qdisc replace dev "$ifb" root netem limit "$limit" rate "${rate}bit"
        ;;
      N6)
        epoch=$(cat "$state.epoch")
        tc qdisc replace dev "$ifb" root netem limit "$limit" loss 100%
        date -u '+%Y-%m-%dT%H:%M:%SZ 단절 시작'
        sleep 10
        # 중단·재준비된 다음 시험의 인터페이스를 늦게 수정하지 않는다.
        [ -f "$state.epoch" ] && [ "$(cat "$state.epoch")" = "$epoch" ] || exit 3
        tc qdisc del dev "$ifb" root
        tc qdisc add dev "$ifb" root netem limit "$limit" delay 0ms
        date -u '+%Y-%m-%dT%H:%M:%SZ 복구 시작'
        ;;
      N7) tc qdisc replace dev "$ifb" root netem limit "$limit" delay 50ms 10ms distribution normal loss random 0.5% ;;
      *) exit 2 ;;
    esac
    ;;
  clear)
    [ -f "$state" ] && [ "$(cat "$state")" = "$interface" ] || exit 2
    # 종료 구간에서는 지연 큐를 비워 원래 조건으로 돌린다. 삭제 시 남은 패킷은 버려진다.
    tc qdisc del dev "$ifb" root
    tc qdisc add dev "$ifb" root netem limit "$limit" delay 0ms
    ;;
  cleanup)
    [ -f "$state" ] && [ "$(cat "$state")" = "$interface" ] || exit 2
    tc qdisc del dev "$interface" ingress
    ip link del "$ifb"
    rm -f "$state" "$state.epoch"
    exit 0
    ;;
  status) ;;
  *) exit 2 ;;
esac
date -u '+%Y-%m-%dT%H:%M:%SZ'
tc -s qdisc show dev "$interface"
if [ -f "$state" ]; then tc -s qdisc show dev "$ifb"; fi
