#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
bin="$root/DTCctvTestHost/Binaries/Linux"
export LD_LIBRARY_PATH="$bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$bin/DTCctvTestHost" -PixelStreamingEncoderCodec=H264 "$@"
