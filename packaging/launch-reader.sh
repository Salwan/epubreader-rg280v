#!/bin/sh
APPDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$APPDIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_VIDEO_KMSDRM_SCALING_MODE=0
exec "$APPDIR/epubreader" --assets "$APPDIR/assets" "$@"
