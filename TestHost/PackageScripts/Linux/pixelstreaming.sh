#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
signalling_url="${1:-ws://127.0.0.1:8888}"
if (( $# > 0 )); then shift; fi
exec "$root/view.sh" "-PixelStreamingURL=$signalling_url" "$@"
