#!/usr/bin/env bash

# exit when any command fails
set -e

global_usage() {
  cat - <<EOF
Usage:
sh build.sh <command> [target]

Commands:
  help|usage|-h|--help: Shows this message.
  list|-l: List firmwares available to build.
  build-firmware <target>: Build the firmware for the given build target.
  build-firmwares: Build all firmwares (companion & repeater).
  build-matching-firmwares <build-match-spec>: Build targets containing the specified string.
  build-companion-firmwares: Build all companion firmwares.
  build-repeater-firmwares: Build all repeater firmwares.

Examples:
Build firmware for the "RAK_4631_repeater" device target:
$ sh build.sh build-firmware RAK_4631_repeater

Build all companion firmwares:
$ sh build.sh build-companion-firmwares

Build all repeater firmwares:
$ sh build.sh build-repeater-firmwares

Environment Variables:
  DISABLE_DEBUG=1: Disables all debug logging flags.
EOF
}

# get a list of pio env names that start with "env:"
get_pio_envs() {
  pio project config | grep 'env:' | sed 's/env://'
}

# Catch cries for help before doing anything else.
case $1 in
  help|usage|-h|--help)
    global_usage
    exit 1
    ;;
  list|-l)
    get_pio_envs
    exit 0
    ;;
esac

# cache project config json for use in get_platform_for_env()
PIO_CONFIG_JSON=$(pio project config --json-output)

get_pio_envs_containing_string() {
  shopt -s nocasematch
  envs=($(get_pio_envs))
  for env in "${envs[@]}"; do
      if [[ "$env" == *${1}* ]]; then
        echo $env
      fi
  done
}

get_pio_envs_ending_with_string() {
  shopt -s nocasematch
  envs=($(get_pio_envs))
  for env in "${envs[@]}"; do
    if [[ "$env" == *${1} ]]; then
      echo $env
    fi
  done
}

get_platform_for_env() {
  local env_name=$1
  printf '%s' "$PIO_CONFIG_JSON" | python3 -c "
import sys, json, re
data = json.load(sys.stdin)
for section, options in data:
    if section == 'env:$env_name':
        for key, value in options:
            if key == 'build_flags':
                for flag in value:
                    match = re.search(r'(ESP32_PLATFORM|NRF52_PLATFORM|STM32_PLATFORM|RP2040_PLATFORM)', flag)
                    if match:
                        print(match.group(1))
                        sys.exit(0)
"
}

disable_debug_flags() {
  if [ "$DISABLE_DEBUG" == "1" ]; then
    export PLATFORMIO_BUILD_FLAGS="${PLATFORMIO_BUILD_FLAGS} -UMESH_DEBUG -UBLE_DEBUG_LOGGING -UWIFI_DEBUG_LOGGING -UBRIDGE_DEBUG -UGPS_NMEA_DEBUG -UCORE_DEBUG_LEVEL -UESPNOW_DEBUG_LOGGING -UDEBUG_RP2040_WIRE -UDEBUG_RP2040_SPI -UDEBUG_RP2040_CORE -UDEBUG_RP2040_PORT -URADIOLIB_DEBUG_SPI -UCFG_DEBUG -URADIOLIB_DEBUG_BASIC -URADIOLIB_DEBUG_PROTOCOL"
  fi
}

build_firmware() {
  ENV_PLATFORM=($(get_platform_for_env $1))
  COMMIT_HASH=$(git rev-parse --short HEAD)
  FIRMWARE_BUILD_DATE=$(date '+%d-%b-%Y')

  if [ -z "$FIRMWARE_VERSION" ]; then
    echo "FIRMWARE_VERSION must be set in environment"
    exit 1
  fi

  FIRMWARE_VERSION_STRING="${FIRMWARE_VERSION}-${COMMIT_HASH}"
  FIRMWARE_FILENAME="$1-${FIRMWARE_VERSION_STRING}"

  export PLATFORMIO_BUILD_FLAGS="${PLATFORMIO_BUILD_FLAGS} -DFIRMWARE_BUILD_DATE='\"${FIRMWARE_BUILD_DATE}\"' -DFIRMWARE_VERSION='\"${FIRMWARE_VERSION_STRING}\"'"

  disable_debug_flags

  pio run -e $1

  if [ "$ENV_PLATFORM" == "ESP32_PLATFORM" ]; then
    pio run -t mergebin -e $1
    cp .pio/build/$1/firmware.bin out/${FIRMWARE_FILENAME}.bin 2>/dev/null || true
    cp .pio/build/$1/firmware-merged.bin out/${FIRMWARE_FILENAME}-merged.bin 2>/dev/null || true
  fi

  if [ "$ENV_PLATFORM" == "NRF52_PLATFORM" ]; then
    python3 bin/uf2conv/uf2conv.py .pio/build/$1/firmware.hex -c -o .pio/build/$1/firmware.uf2 -f 0xADA52840
    cp .pio/build/$1/firmware.uf2 out/${FIRMWARE_FILENAME}.uf2 2>/dev/null || true
    cp .pio/build/$1/firmware.zip out/${FIRMWARE_FILENAME}.zip 2>/dev/null || true
  fi

  if [ "$ENV_PLATFORM" == "STM32_PLATFORM" ]; then
    cp .pio/build/$1/firmware.bin out/${FIRMWARE_FILENAME}.bin 2>/dev/null || true
    cp .pio/build/$1/firmware.hex out/${FIRMWARE_FILENAME}.hex 2>/dev/null || true
  fi

  if [ "$ENV_PLATFORM" == "RP2040_PLATFORM" ]; then
    cp .pio/build/$1/firmware.bin out/${FIRMWARE_FILENAME}.bin 2>/dev/null || true
    cp .pio/build/$1/firmware.uf2 out/${FIRMWARE_FILENAME}.uf2 2>/dev/null || true
  fi
}

build_all_firmwares_matching() {
  envs=($(get_pio_envs_containing_string "$1"))
  for env in "${envs[@]}"; do
      build_firmware $env
  done
}

build_all_firmwares_by_suffix() {
  envs=($(get_pio_envs_ending_with_string "$1"))
  for env in "${envs[@]}"; do
    build_firmware $env
  done
}

build_repeater_firmwares() {
  build_all_firmwares_by_suffix "_repeater"
}

build_companion_firmwares() {
  build_all_firmwares_by_suffix "_companion_radio_usb"
  build_all_firmwares_by_suffix "_companion_radio_ble"
}

build_firmwares() {
  build_companion_firmwares
  build_repeater_firmwares
}

# clean build dir
rm -rf out
mkdir -p out

# handle script args
if [[ $1 == "build-firmware" ]]; then
  TARGETS=${@:2}
  if [ "$TARGETS" ]; then
    for env in $TARGETS; do
      build_firmware $env
    done
  else
    echo "usage: $0 build-firmware <target>"
    exit 1
  fi
elif [[ $1 == "build-matching-firmwares" ]]; then
  if [ "$2" ]; then
     build_all_firmwares_matching $2
  else
     echo "usage: $0 build-matching-firmwares <build-match-spec>"
    exit 1
  fi
elif [[ $1 == "build-firmwares" ]]; then
  build_firmwares
elif [[ $1 == "build-companion-firmwares" ]]; then
  build_companion_firmwares
elif [[ $1 == "build-repeater-firmwares" ]]; then
  build_repeater_firmwares
elif [[ $1 == "get-companion-firmwares-to-build" ]]; then
  get_pio_envs_ending_with_string "_companion_radio_usb"
  get_pio_envs_ending_with_string "_companion_radio_ble"
elif [[ $1 == "get-repeater-firmwares-to-build" ]]; then
  get_pio_envs_ending_with_string "_repeater"
fi
