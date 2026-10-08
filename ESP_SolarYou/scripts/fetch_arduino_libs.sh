#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
DOWNLOAD_ROOT="$PROJECT_ROOT/.downloads"

mkdir -p "$DOWNLOAD_ROOT"

install_library() {
    local name="$1"
    local version="$2"
    local url="$3"
    local component="$4"
    local extracted="$5"

    local component_dir="$PROJECT_ROOT/components/$component"
    local vendor_dir="$component_dir/vendor"
    local version_file="$vendor_dir/.dependency-version"
    local expected="$name $version"

    if [[ -f "$version_file" ]]; then
        local installed
        installed="$(tr -d '\r\n' < "$version_file")"

        if [[ "$installed" == "$expected" ]]; then
            echo "Already present: $expected"
            return
        fi

        echo "Version mismatch in $vendor_dir."
        echo "Installed: $installed"
        echo "Expected:  $expected"
        echo "Remove that vendor directory and rerun this script."
        exit 1
    fi

    if [[ -e "$vendor_dir" ]]; then
        echo "Unversioned vendor directory exists at:"
        echo "  $vendor_dir"
        echo "Inspect/remove it before rerunning."
        exit 1
    fi

    local archive="$DOWNLOAD_ROOT/${name}-${version}.zip"
    local extract_parent="$DOWNLOAD_ROOT/${name}-${version}"
    local source_dir="$extract_parent/$extracted"

    echo "Downloading $expected from the official Adafruit repository..."

    rm -rf "$extract_parent"
    mkdir -p "$extract_parent"

    curl -L "$url" -o "$archive"

    unzip -q "$archive" -d "$extract_parent"

    if [[ ! -d "$source_dir" ]]; then
        echo "Expected extracted directory not found:"
        echo "  $source_dir"
        exit 1
    fi

    mkdir -p "$vendor_dir"
    cp -a "$source_dir"/. "$vendor_dir"/

    printf '%s' "$expected" > "$version_file"

    echo "Installed $expected"
}

install_library \
    "Adafruit_BNO08x" \
    "1.2.7" \
    "https://github.com/adafruit/Adafruit_BNO08x/archive/refs/tags/1.2.7.zip" \
    "adafruit_bno08x" \
    "Adafruit_BNO08x-1.2.7"

install_library \
    "Adafruit_INA219" \
    "1.2.3" \
    "https://github.com/adafruit/Adafruit_INA219/archive/refs/tags/1.2.3.zip" \
    "adafruit_ina219" \
    "Adafruit_INA219-1.2.3"

install_library \
    "Adafruit_BusIO" \
    "1.17.4" \
    "https://github.com/adafruit/Adafruit_BusIO/archive/refs/tags/1.17.4.zip" \
    "adafruit_busio" \
    "Adafruit_BusIO-1.17.4"

install_library \
    "Adafruit_Sensor" \
    "1.1.15" \
    "https://github.com/adafruit/Adafruit_Sensor/archive/refs/tags/1.1.15.zip" \
    "adafruit_unified_sensor" \
    "Adafruit_Sensor-1.1.15"

echo "All pinned Arduino libraries are ready."