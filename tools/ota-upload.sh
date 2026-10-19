#!/usr/bin/env bash
# Compile and OTA-update one bulb with an OTA-only UF2.
#
# The UF2 that ESPHome uploads also carries the serial-flasher images, which
# the bulb discards during OTA: 2.5 MB on the air instead of about 1 MB. On
# bulbs with a weak signal the full upload rarely survives, so this rebuilds
# the UF2 with only the OTA (device:download) part using ltchiptool.
#
# Usage: tools/ota-upload.sh <node_name> "<friendly name>" <address>
set -euo pipefail

if [ $# -ne 3 ]; then
  echo "usage: $0 <node_name> \"<friendly name>\" <address>" >&2
  exit 2
fi
node=$1
friendly=$2
address=$3

cd "$(dirname "$0")/.."
esphome=${ESPHOME:-.venv/bin/esphome}
ltchiptool=${LTCHIPTOOL:-$HOME/.platformio/penv/.libretiny/bin/ltchiptool}

"$esphome" -s node_name "$node" -s friendly_name "$friendly" compile gx53-test.yaml

build=.esphome/build/$node/.pioenvs/$node
uf2=$build/firmware.ota-only.uf2
lt_version=$("$ltchiptool" uf2 info "$build/firmware.uf2" 2>&1 | sed -n 's/.*LT_VERSION: //p')
fw_version=$("$ltchiptool" uf2 info "$build/firmware.uf2" 2>&1 | sed -n 's/.*- VERSION: //p')

"$ltchiptool" uf2 write \
  --output "$uf2" \
  --board "$build/board.json" \
  --lt-version "$lt_version" \
  --fw "esphome:$fw_version" \
  --date "$(date +%s)" \
  --legacy \
  "$build/image_bk7231n_app.ota.rbl=device:download"

# Refuse to upload unless the OTA payload matches the official UF2 exactly.
dump=$(mktemp -d)
trap 'rm -rf "$dump"' EXIT
"$ltchiptool" uf2 dump -o "$dump/full" "$build/firmware.uf2" >/dev/null
"$ltchiptool" uf2 dump -o "$dump/ota" "$uf2" >/dev/null
cmp "$dump/full/uf2dump_bk7231n_device_0x12A000.bin" "$dump/ota/uf2dump_bk7231n_device_0x12A000.bin"

"$esphome" -s node_name "$node" -s friendly_name "$friendly" \
  upload gx53-test.yaml --device "$address" --file "$uf2"
