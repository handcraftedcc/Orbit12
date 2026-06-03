#!/bin/zsh
set -e

python3 tools/build_mpy.py

rsync -avh --itemize-changes --progress \
  --exclude=.git \
  --exclude=.idea \
  --exclude=.vscode \
  --exclude=.venv \
  --exclude=lib/ \
  '--exclude=._*' \
  --exclude=.DS_Store \
  --exclude=libArchive/ \
  --exclude=stubArchive/ \
  --exclude=userdata/ \
  --exclude=apps/Orion/ \
  src/ /Volumes/CIRCUITPY/

rsync -avh --delete --itemize-changes --progress \
  src_mpy/lib/ /Volumes/CIRCUITPY/lib/

rsync -avh --delete --itemize-changes --progress \
  src_mpy/apps/Orion/ /Volumes/CIRCUITPY/apps/Orion/
