#!/bin/bash
#
# build_stage_machine.sh - build one Pi4 machine kernel and stage it.
#
# Single replacement for the old build_stage_{amiga,atarist,dos}.sh
# scripts (they were byte-identical apart from the machine name and the
# BUILD TAG extractor below). Any machine from sd_layout.py kernel-machines
# works, including the VICE ones (c64, atari800, ...).
#
# Rule: NEVER stage without building (build_pi4.sh --build-only refreshes
# the per-machine BUILD TAG first, so the staged kernel's log tag always
# identifies exactly what is running).
#
# Usage: tools/pi4/build_stage_machine.sh --machine <name> [--jobs N] ...
#   Extra args are passed through to build_pi4.sh (e.g. --jobs 8 --clean).
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

MACHINE=""
ARGS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --machine)
      [ $# -ge 2 ] || { echo "--machine requires a machine name" >&2; exit 1; }
      MACHINE="$2"
      shift 2
      ;;
    *)
      ARGS+=("$1")
      shift
      ;;
  esac
done
if [ -z "$MACHINE" ]; then
  echo "usage: build_stage_machine.sh --machine <name> [build_pi4.sh args]" >&2
  exit 1
fi

"$SCRIPT_DIR/build_pi4.sh" --build-only --machine "$MACHINE" "${ARGS[@]}"

# Stage ONLY on build success (set -e already guarantees this).
IMAGE="$(ls -t "$SRC_DIR"/build/pi4/variants/*/images/kernel8.img."$MACHINE" 2>/dev/null | head -1)"
if [ -z "${IMAGE:-}" ] || [ ! -s "$IMAGE" ]; then
  echo "build_stage_machine: no kernel8.img.$MACHINE produced" >&2
  exit 1
fi
cp "$IMAGE" "$SRC_DIR/pi4-test/sdcard/kernel8.img.$MACHINE"

# Per-machine BUILD TAG extractors (best effort; size is authoritative).
TAG=""
case "$MACHINE" in
  amiga|atarist|atari800)
    TAG="$(grep -o 'BUILD TAG: .*' "$SRC_DIR/src/$MACHINE/${MACHINE}_debug.cpp" 2>/dev/null | head -1)"
    ;;
  dos)
    TAG="$(grep -o 'DOS_BUILD_TAG "[^"]*"' "$SRC_DIR/src/dos/dos_build_tag.h" 2>/dev/null | cut -d'"' -f2)"
    ;;
esac
echo "build_stage_machine: OK machine=$MACHINE size=$(stat -c%s "$SRC_DIR/pi4-test/sdcard/kernel8.img.$MACHINE") staged=pi4-test/sdcard/kernel8.img.$MACHINE${TAG:+ tag=$TAG}"
