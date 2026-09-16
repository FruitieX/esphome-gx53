# ESPHome GX53 RGB+CCT firmware

ESPHome firmware for a Tuya/BK7231N GX53 RGB+CCT bulb using a BP5768 LED
driver through ESPHome's compatible BP5758D component.

## Local configuration

Copy `secrets.yaml.example` to `secrets.yaml` and set the local Wi-Fi and
fallback-access-point values. The resulting `secrets.yaml` is ignored by Git.

The checked-in configuration has safe development defaults for `node_name`
and `friendly_name`. Override both substitutions for each physical bulb:

```sh
.venv/bin/esphome \
  -s node_name gx53-01 \
  -s friendly_name "GX53 01" \
  run gx53-test.yaml --device 192.168.13.75
```

`node_name` controls the ESPHome hostname and MQTT topic prefix. It must be
unique for every bulb. `friendly_name` is the human-readable device name.

Use `--no-logs` when provisioning several bulbs and a log tail is not needed:

```sh
.venv/bin/esphome \
  -s node_name gx53-01 \
  -s friendly_name "GX53 01" \
  run gx53-test.yaml --device 192.168.13.75 --no-logs
```

## Safety configuration

The BP5758D `current` values and the mixer's corresponding `*_current_ma`
values must always match. The current configuration uses 4 mA for each RGB
channel, 6 mA for each white channel, and a 12 mA aggregate mixer budget.
