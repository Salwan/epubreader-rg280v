#!/bin/sh
# The launcher starts with the package as its working directory.
APPDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$APPDIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Full-panel scaling is necessary for a native 320x480 surface: SDL's
# aspect-correct mode otherwise interprets it as square-pixel content.
export SDL_VIDEO_KMSDRM_SCALING_MODE=0
exec "$APPDIR/epubreader-diagnostic" --assets "$APPDIR/assets" "$@"
