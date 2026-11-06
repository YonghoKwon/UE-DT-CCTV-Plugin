#!/bin/sh
set -eu
test -f /.dockerenv
test "${DTCCTV_NETEM_OWNED:-}" = 1
command -v node >/dev/null
node /runtime.js rpm /rpms
export PATH="/usr/sbin:/sbin:$PATH"
tc -V
