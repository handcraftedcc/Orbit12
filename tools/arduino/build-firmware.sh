#!/usr/bin/env bash
# Build the Orbit12 macropad firmware with the workspace-pinned arduino-cli (Git Bash).
#
#   tools/arduino/build-firmware.sh                 compile only
#   tools/arduino/build-firmware.sh --upload COM6   compile, then flash the port (explicit only)
#
# Fixed locations: build tree .tool-cache/build/release, artifacts .tool-cache/artifacts/release.
# The sketch, build and output folders are passed as Windows 8.3 short paths (derived at run time
# with Scripting.FileSystemObject), so the repository's own path with spaces never reaches the
# compiler's -I flags. Nothing here is hardcoded to a particular user or drive.
#
# Link-time USB guard: the wrapped TinyUSB entry points below are redirected to
# firmware/Orbit12/src/platform/macropad/usb_core_guard.cpp so that core 1 never services USB
# (yield() from the display/GFX libraries). Keep this list in sync with that file.
set -euo pipefail

usage() {
  sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

upload_port=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --upload)
      [[ $# -ge 2 && -n "$2" ]] || { echo "error: --upload needs a serial port, e.g. --upload COM6" >&2; exit 2; }
      upload_port="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "error: unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
cache="$script_dir/.tool-cache"
cli="$cache/arduino-cli-1.5.1/arduino-cli.exe"
config="$cache/arduino-cli.yaml"
sketch_dir="$repo_root/firmware/Orbit12"
build_dir="$cache/build/release"
out_dir="$cache/artifacts/release"
profile="macropad"

[[ -f "$cli" ]] || { echo "error: pinned arduino-cli not found at $cli (run tools/arduino/bootstrap.ps1)" >&2; exit 2; }
[[ -f "$config" ]] || { echo "error: arduino-cli config not found at $config" >&2; exit 2; }
[[ -f "$sketch_dir/Orbit12.ino" ]] || { echo "error: sketch not found at $sketch_dir" >&2; exit 2; }

mkdir -p "$build_dir" "$out_dir"

# 8.3 short name of an existing folder. Fails loudly if the volume has 8.3 names disabled,
# because a long path with spaces would break the -I flags.
short_path() {
  local win short
  win="$(cygpath -w "$1")"
  short="$(powershell.exe -NoProfile -NonInteractive -Command \
    "(New-Object -ComObject Scripting.FileSystemObject).GetFolder('$win').ShortPath" | tr -d '\r')"
  if [[ -z "$short" || "$short" == *" "* ]]; then
    echo "error: no 8.3 short path for $win (got '$short'); enable 8.3 names on this volume" >&2
    exit 2
  fi
  printf '%s' "$short"
}

sketch_short="$(short_path "$sketch_dir")"
build_short="$(short_path "$build_dir")"
out_short="$(short_path "$out_dir")"

# Wrapped TinyUSB symbols (see usb_core_guard.cpp for why each one is reachable from yield()).
wrap_symbols=(TinyUSB_Device_Task TinyUSB_Device_FlushCDC tud_task_ext tud_cdc_n_write_flush)
wrap_flags=""
for sym in "${wrap_symbols[@]}"; do wrap_flags+=" -Wl,--wrap=$sym"; done
wrap_flags="${wrap_flags# }"

# Sketch sources include "app/...", "domain/...", "platform/..." relative to src/, so src/ is the
# include root. Forward slashes keep the 8.3 path usable inside the recipe's flag list.
src_include="$(printf '%s' "$sketch_short" | tr '\\' '/')/src"

log="$out_dir/compile.log"
echo "== compile profile=$profile sketch=$sketch_short"
echo "== build-path=$build_short output-dir=$out_short"
echo "== include root: $src_include"
echo "== link flags: $wrap_flags"
set +e
"$cli" compile --config-file "$config" --profile "$profile" \
  --build-path "$build_short" --output-dir "$out_short" \
  --build-property "build.extra_flags=-I$src_include" \
  --build-property "compiler.c.elf.extra_flags=$wrap_flags" \
  "$sketch_short" 2>&1 | tee "$log"
status=${PIPESTATUS[0]}
set -e
if [[ $status -ne 0 ]]; then
  echo "error: compile failed (exit $status); full log: $log" >&2
  exit "$status"
fi

echo
echo "== summary"
grep -E "^Sketch uses|^Global variables use" "$log" || true
warnings="$(grep -c "warning:" "$log" || true)"
echo "compiler warnings: $warnings"
uf2="$(find "$out_dir" -maxdepth 1 -name '*.uf2' | head -n 1)"
if [[ -z "$uf2" ]]; then
  echo "error: no UF2 produced in $out_dir" >&2
  exit 3
fi
echo "UF2: $uf2"
sha256sum "$uf2"
echo "log: $log"

if [[ -n "$upload_port" ]]; then
  echo "== upload to $upload_port (explicit --upload)"
  "$cli" upload --config-file "$config" --profile "$profile" -p "$upload_port" \
    --input-dir "$out_short" "$sketch_short"
fi
