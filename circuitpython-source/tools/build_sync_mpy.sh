#!/bin/zsh
set -e

cd "$(dirname "$0")/.."

NO_LIB=""
SYNC_LIB=true

while [[ $# -gt 0 ]]; do
    case "$1" in
        --help|-h)
            print "Usage: $0 [--no-lib]"
            print "Build the CircuitPython .mpy deployment and sync it to /Volumes/CIRCUITPY."
            print "Run from any directory; paths are resolved from this script location."
            exit 0
            ;;
        --no-lib)
            NO_LIB="--no-lib"
            SYNC_LIB=false
            shift
            ;;
        *)
            shift
            ;;
    esac
done

if [ -n "$NO_LIB" ]; then
    python3 tools/build_mpy.py --no-lib
else
    python3 tools/build_mpy.py
fi

rsync -avh --itemize-changes --progress \
  --exclude=.git \
  --exclude=.idea \
  --exclude=.vscode \
  --exclude=.venv \
  --exclude='__pycache__/' \
  --exclude='*.pyc' \
  --exclude=lib/ \
  '--exclude=._*' \
  --exclude=.DS_Store \
  --exclude=libArchive/ \
  --exclude=stubArchive/ \
  --exclude=userdata/ \
  --exclude=apps/Orion/ \
  src/ /Volumes/CIRCUITPY/

if $SYNC_LIB; then
    rsync -avh --delete --itemize-changes --progress \
      src_mpy/lib/ /Volumes/CIRCUITPY/lib/
fi

rsync -avh --delete --itemize-changes --progress \
  src_mpy/apps/Orion/ /Volumes/CIRCUITPY/apps/Orion/
