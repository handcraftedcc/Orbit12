#!/usr/bin/env bash
# Workspace-local toolchain bootstrap for Linux, macOS and CI (Git Bash users may use bootstrap.ps1).
#
# Installs the pinned arduino-cli 1.5.1 under tools/arduino/.tool-cache, writes the workspace config,
# and installs the Arduino-Pico core and libraries listed in firmware/Orbit12/sketch.yaml. Nothing is
# installed globally and PATH is not changed. The CLI archive is verified against its pinned SHA-256.
set -euo pipefail

CLI_VERSION="1.5.1"
PLATFORM_INDEX_URL="https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json"
RELEASE_URL="https://github.com/arduino/arduino-cli/releases/download/v$CLI_VERSION"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
cache="$script_dir/.tool-cache"
install_dir="$cache/arduino-cli-$CLI_VERSION"
config="$cache/arduino-cli.yaml"
sketch_yaml="$repo_root/firmware/Orbit12/sketch.yaml"

case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)  archive="arduino-cli_${CLI_VERSION}_Linux_64bit.tar.gz"
                 sha256="28a8e119c498a25607821c36cb2dc49e8463941b261a0d99091baa7bc692dd2b" ;;
  Darwin-x86_64) archive="arduino-cli_${CLI_VERSION}_macOS_64bit.tar.gz"
                 sha256="c982e940027996bea9901050e95fae99c59c1dcfee54beedecaf28141e7bf2e7" ;;
  Darwin-arm64)  archive="arduino-cli_${CLI_VERSION}_macOS_ARM64.tar.gz"
                 sha256="cb952e8c1621c95ef5f1d17831c945e3d0ec5973f89c557a7ec8feb9c4f7d4c9" ;;
  *) echo "error: no pinned arduino-cli archive for $(uname -s)-$(uname -m); use bootstrap.ps1 on Windows" >&2
     exit 2 ;;
esac

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

mkdir -p "$install_dir"
cli="$install_dir/arduino-cli"
if [[ ! -x "$cli" ]]; then
  archive_path="$install_dir/$archive"
  if [[ ! -f "$archive_path" ]]; then
    curl -fsSL -o "$archive_path" "$RELEASE_URL/$archive"
  fi
  actual="$(sha256_of "$archive_path")"
  if [[ "$actual" != "$sha256" ]]; then
    rm -f "$archive_path"
    echo "error: arduino-cli archive SHA-256 mismatch: got $actual, expected $sha256" >&2
    exit 1
  fi
  tar -xzf "$archive_path" -C "$install_dir" arduino-cli
  rm -f "$archive_path"
fi

# Workspace config keeps data, downloads and user dirs inside the cache (absolute paths).
mkdir -p "$cache"
cat > "$config" <<EOF
board_manager:
  additional_urls:
    - $PLATFORM_INDEX_URL
directories:
  data: "$cache/data"
  downloads: "$cache/downloads"
  user: "$cache/user"
EOF

"$cli" --config-file "$config" version
"$cli" --config-file "$config" core update-index

# Platform and libraries come from the sketch profile so the pins live in one place.
platform_spec=""
library_specs=()
section=""
# "  - [platform: ]Name (version)" entries; the pattern lives in a variable for portable regex parsing.
pin_pattern='^[[:space:]]+-[[:space:]]+(platform:[[:space:]]*)?(.+)[[:space:]]+[(]([0-9][^)]*)[)][[:space:]]*$'
while IFS= read -r line; do
  if [[ "$line" =~ ^[[:space:]]+libraries:[[:space:]]*$ ]]; then section="libraries"; continue; fi
  if [[ "$line" =~ ^[[:space:]]+platforms:[[:space:]]*$ ]]; then section="platforms"; continue; fi
  if [[ "$line" =~ ^[[:space:]]+(fqbn|notes):  ]]; then section=""; continue; fi
  if [[ "$line" =~ $pin_pattern ]]; then
    name="${BASH_REMATCH[2]}"
    ver="${BASH_REMATCH[3]}"
    if [[ "$section" == platforms ]]; then
      platform_spec="$name@$ver"
    elif [[ "$section" == libraries ]]; then
      library_specs+=("$name@$ver")
    fi
  fi
done < "$sketch_yaml"

[[ -n "$platform_spec" ]] || { echo "error: no platform pin found in $sketch_yaml" >&2; exit 1; }
[[ ${#library_specs[@]} -gt 0 ]] || { echo "error: no library pins found in $sketch_yaml" >&2; exit 1; }

"$cli" --config-file "$config" core install "$platform_spec"
for spec in "${library_specs[@]}"; do
  "$cli" --config-file "$config" lib install "$spec"
done

echo "Workspace-local Arduino CLI: $cli"
echo "Workspace configuration: $config"
echo "Installed platform $platform_spec and ${#library_specs[@]} libraries from sketch.yaml"
