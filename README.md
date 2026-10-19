# ESPHome GX53 RGB+CCT firmware

ESPHome firmware for a Tuya/BK7231N GX53 RGB+CCT bulb using a BP5768 LED
driver through ESPHome's compatible BP5758D component.

## Local configuration

Copy `secrets.yaml.example` to `secrets.yaml` and set the local Wi-Fi and
recovery-network values. The resulting `secrets.yaml` is ignored by Git.

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

## Updating bulbs over the air

The UF2 that ESPHome uploads also carries the serial-flasher images, which a
bulb discards during OTA, so about 2.5 MB goes over the air. On bulbs with a
weak signal that upload rarely completes. `tools/ota-upload.sh` compiles the
firmware, rebuilds the UF2 with only the OTA part (about 1 MB, verified to
match the official payload) and uploads that:

```sh
tools/ota-upload.sh gx53-01 "GX53 01" 192.168.13.75
```

If an upload fails, wait at least 90 seconds before retrying: the bulb keeps
the stalled session open until its data timeout, and an earlier retry
fails the handshake.

## Safety configuration

The BP5758D `current` values and the mixer's corresponding `*_current_ma`
values must always match. The current configuration uses 6 mA for each RGB
channel, 8 mA for each white channel, and a 14 mA aggregate mixer budget.

The mixer also owns the chip-temperature interlock. It starts with the output
locked until a safe temperature reading is available, shuts down at 85 °C,
and rejects turn-on commands for at least 60 seconds. It rearms only after the
chip has cooled to 75 °C or below, then republishes the OFF state so a controller
can retry its desired state safely. The diagnostic `Thermal Lockout` binary
sensor reports whether commands are currently inhibited.

## Status light

While the light is off, the bulb glows faintly to show connectivity problems:

| Color | Meaning |
| --- | --- |
| Red | Not connected to Wi-Fi |
| Blue | Wi-Fi connected, MQTT broker not connected |
| Dark | Connected, thermal lockout, or the firmware is not running |

Until the first full connection after boot, problems are shown immediately,
so a normal boot may glow red and then blue for a few seconds. Afterwards a
problem must last for `status_indicator_delay` (30 s) before it is shown.

## Recovery

A bulb that can't join the main network also tries the recovery network
(`recovery_wifi_ssid`), which normally doesn't exist. The password is in
1Password (Agents vault, "GX53 recovery Wi-Fi"). Host it near the bulb and
update the bulb over it:

```sh
nmcli dev wifi hotspot ifname wlp2s0 con-name gx53-recovery \
  ssid gx53-recovery band bg channel 11 password "<recovery password>"
# the bulb's address appears here once it has joined; its client MAC is
# one lower than the MAC of its old `-fb` access point
cat /var/lib/NetworkManager/dnsmasq-wlp2s0.leases
tools/ota-upload.sh <node> "<name>" <address>
nmcli connection delete gx53-recovery
```

The host's firewall must accept DHCP (UDP 67) on the hotspot interface.
Otherwise the bulb associates, gets no address and drops the connection
every minute or two. On a NixOS host with the firewall enabled, that means
`networking.firewall.interfaces.<iface>.allowedUDPPorts = [ 67 ]` or a
temporary `iptables -I nixos-fw 1 -i <iface> -p udp --dport 67 -j
nixos-fw-accept`. A phone hotspot handles DHCP itself.

A blocked bulb found and joined the recovery network within about 25
minutes in testing. Updates over a nearby hotspot take about 10 seconds
instead of several minutes. Without MQTT, ESPHome reboots the bulb after
15 minutes; it rejoins the recovery network right away.

There is deliberately no fallback access point. On BK7231N it was barely
reachable while the bulb retried the main network, and while it was up
ESPHome stopped scanning (so the recovery network was never found) and
skipped the 15-minute Wi-Fi reboot.
