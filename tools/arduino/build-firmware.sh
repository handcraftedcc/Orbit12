#!/usr/bin/env bash
# Build the Orbit12 macropad firmware with the pinned arduino-cli (Git Bash on Windows, Linux, macOS).
#
#   tools/arduino/build-firmware.sh                        compile only (version from git describe)
#   tools/arduino/build-firmware.sh --version 0.1.0-pre.1  compile with an explicit release version
#   tools/arduino/build-firmware.sh --upload COM6          compile, then flash the port (explicit only)
#
# Options and environment (all optional):
#   --version V / ORBIT_VERSION   firmware version stamped into app/build_info.hpp (semver-like).
#                                 Default: git describe --tags --always --dirty with a leading 'v' removed.
#   ORBIT_ARDUINO_CLI             arduino-cli executable. Default: the workspace-pinned 1.5.1 if present,
#                                 otherwise arduino-cli on PATH. Must report version 1.5.1.
#   ORBIT_ARDUINO_CONFIG          arduino-cli config file. Default: the workspace config if present.
#   ORBIT_FLASH_BUDGET            flash size gate in bytes (default 409600 = 400 KiB).
#   ORBIT_RAM_BUDGET              global RAM size gate in bytes (default 112640 = 110 KiB).
#   ORBIT_PYTHON                  python interpreter for the ELF check (default: python3, then python).
#
# Outputs:
#   .tool-cache/artifacts/release/                 compile output, ELF, map, compile.log
#   .tool-cache/artifacts/release/dist/Orbit12-<version>.uf2   release image
#   .tool-cache/artifacts/release/dist/release-manifest.json and SHA256SUMS (semver versions only)
#
# The sketch is built from its own folder without -I flags: sources include their headers relative
# to the including file. app/build_info.hpp is stamped for the build and restored to its committed
# placeholder on exit, so the git tree stays clean.
set -euo pipefail

# Wrapped TinyUSB symbols (see usb_core_guard.cpp for why each one is reachable from yield()).
# Keep in sync with WRAP_SYMBOLS in tools/arduino/elf_symbols.py.
WRAP_SYMBOLS=(TinyUSB_Device_Task TinyUSB_Device_FlushCDC tud_task_ext tud_cdc_n_write_flush)
PINNED_CLI_VERSION="1.5.1"
PROFILE="macropad"
FLASH_BUDGET="${ORBIT_FLASH_BUDGET:-409600}"
RAM_BUDGET="${ORBIT_RAM_BUDGET:-112640}"

usage() {
  sed -n '2,21p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

fail() { echo "error: $*" >&2; exit "${2:-2}"; }

version=""
upload_port=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)
      [[ $# -ge 2 && -n "$2" ]] || fail "--version needs a value, e.g. --version 0.1.0-pre.1"
      version="$2"; shift 2 ;;
    --upload)
      [[ $# -ge 2 && -n "$2" ]] || fail "--upload needs a serial port, e.g. --upload COM6"
      upload_port="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "error: unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
cache="$script_dir/.tool-cache"
sketch_dir="$repo_root/firmware/Orbit12"
build_info="$sketch_dir/src/app/build_info.hpp"
build_dir="$cache/build/release"
out_dir="$cache/artifacts/release"
dist_dir="$out_dir/dist"

# ---- tools -----------------------------------------------------------------------------------------
# A native Windows arduino-cli needs absolute paths with forward slashes; cygpath -m gives that under
# Git Bash. On Linux and macOS the POSIX path is already correct.
native_path() {
  if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi
}

if [[ -n "${ORBIT_ARDUINO_CLI:-}" ]]; then
  cli="$ORBIT_ARDUINO_CLI"
elif [[ -f "$cache/arduino-cli-$PINNED_CLI_VERSION/arduino-cli.exe" ]]; then
  cli="$cache/arduino-cli-$PINNED_CLI_VERSION/arduino-cli.exe"
elif [[ -f "$cache/arduino-cli-$PINNED_CLI_VERSION/arduino-cli" ]]; then
  cli="$cache/arduino-cli-$PINNED_CLI_VERSION/arduino-cli"
elif command -v arduino-cli >/dev/null 2>&1; then
  cli="$(command -v arduino-cli)"
else
  fail "arduino-cli not found; run tools/arduino/bootstrap.sh (or bootstrap.ps1) or set ORBIT_ARDUINO_CLI"
fi
cli_version_line="$("$cli" version 2>/dev/null | head -n 1 || true)"
case "$cli_version_line" in
  *"Version: $PINNED_CLI_VERSION "*|*"Version: $PINNED_CLI_VERSION") ;;
  *) fail "arduino-cli must be $PINNED_CLI_VERSION, got: ${cli_version_line:-no output} ($cli)" ;;
esac

config=""
if [[ -n "${ORBIT_ARDUINO_CONFIG:-}" ]]; then
  config="$ORBIT_ARDUINO_CONFIG"
elif [[ -f "$cache/arduino-cli.yaml" ]]; then
  config="$cache/arduino-cli.yaml"
fi
cli_config_args=()
if [[ -n "$config" ]]; then
  [[ -f "$config" ]] || fail "arduino-cli config not found at $config"
  cli_config_args=(--config-file "$(native_path "$config")")
fi

python_bin="${ORBIT_PYTHON:-}"
if [[ -z "$python_bin" ]]; then
  for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 9) else 1)' 2>/dev/null; then
      python_bin="$candidate"
      break
    fi
  done
fi
[[ -n "$python_bin" ]] || fail "python 3.9+ is required for the ELF check (set ORBIT_PYTHON)"

[[ -f "$sketch_dir/Orbit12.ino" ]] || fail "sketch not found at $sketch_dir"
[[ -f "$build_info" ]] || fail "build identity header missing: $build_info"

# ---- version and identity --------------------------------------------------------------------------
if [[ -z "$version" ]]; then version="${ORBIT_VERSION:-}"; fi
if [[ -z "$version" ]]; then
  version="$(git -C "$repo_root" describe --tags --always --dirty 2>/dev/null || echo 0.0.0-dev)"
  version="${version#v}"
fi
if ! [[ "$version" =~ ^[0-9A-Za-z][0-9A-Za-z.+-]*$ ]]; then
  fail "version '$version' contains characters that are not allowed in the firmware string"
fi
git_sha="$(git -C "$repo_root" rev-parse HEAD 2>/dev/null || echo unknown)"
if [[ "$git_sha" != unknown ]] && git -C "$repo_root" status --porcelain 2>/dev/null | grep -q .; then
  git_sha="$git_sha-dirty"
fi

# The committed placeholder is restored on every exit path, so the git tree stays clean.
placeholder_backup="$(mktemp)"
cp "$build_info" "$placeholder_backup"
restore_placeholder() {
  cp "$placeholder_backup" "$build_info"
  rm -f "$placeholder_backup"
}
trap restore_placeholder EXIT

sed -E -e "s|(kFirmwareVersion\[\] = \")[^\"]*(\";)|\1$version\2|" \
       -e "s|(kGitSha\[\] = \")[^\"]*(\";)|\1$git_sha\2|" "$placeholder_backup" > "$build_info"
grep -qF "kFirmwareVersion[] = \"$version\";" "$build_info" || fail "could not stamp kFirmwareVersion"
grep -qF "kGitSha[] = \"$git_sha\";" "$build_info" || fail "could not stamp kGitSha"

# ---- compile ---------------------------------------------------------------------------------------
mkdir -p "$build_dir" "$out_dir" "$dist_dir"
wrap_flags=""
for sym in "${WRAP_SYMBOLS[@]}"; do wrap_flags+=" -Wl,--wrap=$sym"; done
wrap_flags="${wrap_flags# }"

log="$out_dir/compile.log"
sketch_native="$(native_path "$sketch_dir")"
echo "== compile profile=$PROFILE version=$version git=$git_sha"
echo "== sketch=$sketch_native"
echo "== link flags: $wrap_flags"
set +e
"$cli" compile ${cli_config_args[@]+"${cli_config_args[@]}"} --profile "$PROFILE" \
  --build-path "$(native_path "$build_dir")" --output-dir "$(native_path "$out_dir")" \
  --build-property "compiler.c.elf.extra_flags=$wrap_flags" \
  "$sketch_native" 2>&1 | tee "$log"
status=${PIPESTATUS[0]}
set -e
if [[ $status -ne 0 ]]; then
  fail "compile failed (exit $status); full log: $log" "$status"
fi

# ---- size gates ------------------------------------------------------------------------------------
echo
echo "== summary"
flash_used="$(sed -nE 's/^Sketch uses ([0-9]+) bytes.*/\1/p' "$log" | tail -n 1)"
ram_used="$(sed -nE 's/^Global variables use ([0-9]+) bytes.*/\1/p' "$log" | tail -n 1)"
[[ -n "$flash_used" && -n "$ram_used" ]] || fail "could not read the size report from $log"
echo "flash: $flash_used bytes (budget $FLASH_BUDGET)"
echo "globals: $ram_used bytes (budget $RAM_BUDGET)"
warnings="$(grep -c "warning:" "$log" || true)"
echo "compiler warnings: $warnings"
[[ "$flash_used" -le "$FLASH_BUDGET" ]] || fail "flash $flash_used bytes exceeds budget $FLASH_BUDGET"
[[ "$ram_used" -le "$RAM_BUDGET" ]] || fail "globals $ram_used bytes exceed budget $RAM_BUDGET"

elf="$out_dir/Orbit12.ino.elf"
uf2="$out_dir/Orbit12.ino.uf2"
[[ -f "$uf2" && -f "$elf" ]] || fail "compile did not produce $uf2 and $elf"

# ---- ELF wrap check with the pinned toolchain's nm -------------------------------------------------
pqt_path="$("$cli" ${cli_config_args[@]+"${cli_config_args[@]}"} compile --profile "$PROFILE" --show-properties "$sketch_native" 2>/dev/null \
  | tr -d '\r' | sed -nE 's/^runtime\.tools\.pqt-gcc\.path=(.*)$/\1/p' | head -n 1)"
[[ -n "$pqt_path" ]] || fail "could not resolve the pqt-gcc toolchain path from arduino-cli properties"
pqt_path="$(printf '%s' "$pqt_path" | tr '\\' '/')"
nm_bin=""
for candidate in "$pqt_path/bin/arm-none-eabi-nm.exe" "$pqt_path/bin/arm-none-eabi-nm"; do
  if [[ -f "$candidate" ]]; then nm_bin="$candidate"; break; fi
done
[[ -n "$nm_bin" ]] || fail "arm-none-eabi-nm not found under $pqt_path/bin"
"$python_bin" "$script_dir/elf_symbols.py" check --nm "$nm_bin" --elf "$elf" || fail "ELF wrap check failed"

# ---- release image ---------------------------------------------------------------------------------
release_uf2="$dist_dir/Orbit12-$version.uf2"
cp "$uf2" "$release_uf2"
echo "UF2: $release_uf2"
echo "ELF: $elf"
echo "log: $log"

# ---- release manifest (semver builds only) -----------------------------------------------------------
# Dev builds with a git-describe version skip the manifest; pass --version X.Y.Z[-pre.N] for a release
# image. A -dirty tree is accepted for local checks only (the manifest tool refuses it otherwise).
if [[ "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$ ]]; then
  manifest_args=(--uf2 "$release_uf2" --elf "$elf" --nm "$nm_bin" --version "$version"
    --git-sha "$git_sha" --out-dir "$dist_dir")
  if [[ -n "${ORBIT_REPO:-}" ]]; then manifest_args+=(--repo "$ORBIT_REPO"); fi
  if [[ "$git_sha" == *-dirty ]]; then
    echo "warning: working tree is dirty; the manifest is for local checks, not for publishing"
    manifest_args+=(--allow-dirty)
  fi
  "$python_bin" "$script_dir/make_release_manifest.py" "${manifest_args[@]}" || fail "release manifest failed"
else
  echo "note: version '$version' is not semver; no release-manifest.json (pass --version X.Y.Z)"
fi

# ---- explicit upload -------------------------------------------------------------------------------
if [[ -n "$upload_port" ]]; then
  echo "== upload to $upload_port (explicit --upload)"
  "$cli" upload ${cli_config_args[@]+"${cli_config_args[@]}"} --profile "$PROFILE" -p "$upload_port" \
    --input-dir "$(native_path "$out_dir")" "$sketch_native"
fi
