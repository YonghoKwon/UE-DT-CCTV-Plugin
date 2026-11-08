#!/usr/bin/env bash
set -euo pipefail
if (( $# < 1 )); then
    echo 'Usage: ./check-nullrhi.sh /path/to/channels.json [seconds=130] [extra UE arguments...]' >&2
    exit 64
fi
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
list="$1"
seconds="${2:-130}"
shift
if (( $# > 0 )); then shift; fi
exec "$root/view.sh" -nullrhi -unattended "-CctvCheckList=$list" "-CctvCheckSeconds=$seconds" "$@"
