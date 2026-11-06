#!/bin/sh
set -u
export PATH="/usr/sbin:/sbin:$PATH"
for tool in tc ip iptables nft socat tcpdump python3; do
  location=$(command -v "$tool" 2>/dev/null || true)
  printf '%s=%s\n' "$tool" "${location:-missing}"
done
if ! command -v tc >/dev/null 2>&1; then
  if [ -x /bin/busybox ] && /bin/busybox tc qdisc show >/dev/null 2>&1; then
    tc() { /bin/busybox tc "$@"; }
    printf 'TC_KIND_BUSYBOX\n'
  else
    printf 'NO_STANDALONE_OR_BUSYBOX_TC\n'
    exit 20
  fi
fi
tc -V
if tc qdisc add dev lo root netem delay 1ms; then
  printf 'NETEM_OK\n'
  tc -s qdisc show dev lo
  tc qdisc del dev lo root
else
  printf 'NETEM_UNAVAILABLE\n'
  exit 21
fi
if command -v ip >/dev/null 2>&1 && ip link add dtcctvifb type ifb; then
  ip link set dtcctvifb up
  if tc qdisc add dev dtcctvifb root netem delay 1ms; then
    printf 'IFB_NETEM_OK\n'
    tc qdisc del dev dtcctvifb root
  fi
  ip link del dtcctvifb
else
  printf 'IFB_UNAVAILABLE\n'
fi
