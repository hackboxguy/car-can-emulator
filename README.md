# car-can-emulator
CAN bus emulator for testing OBD2 devices such as Head-up-displays/car-TCUs/etc. For those of you developing CAN devices for the car but want to avoid sitting in an actual car for testing their CAN solution.

# Connection Diagram
![connection-diagram](/images/connection-diagram.png "connection-diagram")

# how to build and test
1. Plugin Canable usb-to-can dongle to your linux pc
2. ```sudo ip link set can0 type can bitrate 500000```
3. ```sudo ifconfig can0 up```
4. ```cmake -H. -BOutput```
5. ```cmake --build Output -- all```
6. ```./Output/car-can-emulator --node=can0 --car=ice --debugprint=true```
7. From a second terminal, run ``` echo -n speed | nc 127.0.0.1 8080``` to read the current speed
8. From a second terminal, run ``` echo -n "speed 90" | nc 127.0.0.1 8080``` to set the current speed

Without hardware, a virtual bus works the same way: `sudo ip link add dev vcan1 type vcan && sudo ip link set up vcan1`, then `--node=vcan1`.

# What it emulates

`--car=ice` (default), `--car=ev` or `--car=hybrid`. Every car type runs an
OBD-II ECU answering functional requests on `0x7DF` from `0x7E8`, plus a
body-controller style telltale broadcast on `0x420`. The ECU advertises
exactly the PIDs it serves in the supported-PID bitmaps (`0x00`, `0x20`, ...)
and, like a real ECU, does not answer a PID it lacks:

| Car | PIDs served |
|---|---|
| `ice` | `04 05 0B 0C 0D 10 2F 42 46 A6` |
| `ev` | `0D 42 46 5B A6` (no engine, no fuel) |
| `hybrid` | `04 05 0B 0C 0D 0F 10 11 2F 33 42 46 5B 5C A6` |

`ev` and `hybrid` add a battery/drive ECU answering UDS `0x22`
ReadDataByIdentifier over ISO-TP on `0x7E4`/`0x7EC` (Linux `can-isotp`
socket; load the module with `sudo modprobe can_isotp`). Its DIDs
carry pack voltage/current, state of charge/health, charging state, range,
consumption, odometer, gear, power state, motor speed, motor power, a
driver-assist record (eco score, posted speed limit, collision risk, lane
state, lead-vehicle gap) and, for contract 1.2, cruise control and drive
mode (`0x0105`), tires (`0x0106`), trip statistics (`0x0107`), the charge
session (`0x0108`) and occupancy (`0x0109`), all as multi-frame transfers. The record layouts are documented in
`car-can-proxy/docs/emulator-ev-profile.md`.

| PID | Signal | netcat knob | Unit |
|---|---|---|---|
| `0x04` | engine load | `load <A>` | raw byte |
| `0x05` | coolant temperature | `temp <A>` | raw byte (A - 40 = degC) |
| `0x0B` | intake manifold pressure | `intake <kPa>` | kPa (raw byte A) |
| `0x0C` | engine RPM | `rpm <A>` | raw byte A (rpm = A * 64) |
| `0x0D` | vehicle speed | `speed <kmh>` | km/h |
| `0x0F` (ice, hybrid) | intake air temperature | `iat <degC>` | degrees C |
| `0x10` | MAF air flow | `flow <raw>` | raw 16-bit |
| `0x11` (ice, hybrid) | throttle position | `throttle <pct>` | percent |
| `0x2F` | fuel tank level | `fuel <pct>` | percent |
| `0x33` (ice, hybrid) | barometric pressure | `baro <kPa>` | kPa |
| `0x42` | control module voltage | `volt <V>` | volts |
| `0x46` | ambient air temperature | `ambient <degC>` | degrees C |
| `0xA6` | odometer | `odo <km>` | km |
| `0x5B` (ev, hybrid) | hybrid battery pack remaining life | (follows `soc`) | percent |
| `0x5C` (ice, hybrid) | engine oil temperature | `oilt <degC>` | degrees C |
| `0x420` (broadcast, 100 ms) | telltale bitmask, 32-bit little-endian | `tt <mask>` | hex or decimal |

Battery ECU knobs (`ev`, `hybrid`): `soc <pct>`, `soh <pct>`, `packv <V>`,
`packi <A>` (negative = charging), `chg <0-3>`, `range <km>`, `cons <Wh/km>`,
`gear <P|R|N|D|L>`, `pwr <0-3>`, `mrpm <rpm>`, `power <kW>` (negative =
regeneration). Driver-assist record (DID `0x0104`): `eco <0-100>`,
`limit <km/h>` (0 = none known), `risk <0-3>`, `lane <mask>`, `gap <m>`.
Contract 1.2 records (layouts in `car-can-proxy/docs/emulator-ev-profile.md`):
cruise `cruise <0-3>`, `setspd <km/h>`, `gapset <0-4>`, `mode <0-7>`; tires
`tp <bar>` (all four) or `tp0`..`tp3`, `ttire <degC>` or `ttire0`..`ttire3`;
trip `tripkm`, `tripmin`, `tripavg <km/h>`, `tripfuel <L/100km>`; charge
session `chgkw`, `chgtarget <pct>`, `chgmin`, `plug <0-2>`; occupancy
`belts`, `seats`, `doors`, `windows` (bit masks, hex accepted).
`car` reads the current car type.

A knob without a value reads the current setting; `reset` puts every knob
back to the defaults below without restarting (the CAN sockets stay up, so
a proxy reading the emulator sees no link loss). The `speed`, `rpm`,
`temp`, `flow`, `intake` and `load` knobs keep their historical raw
semantics; the newer knobs take physical units.

Defaults: `speed 88`, `rpm 12` (768 rpm), `temp 35` (-5 degC), `flow 1344`,
`intake 100`, `load 0`, `fuel 75`, `volt 12.6`, `ambient 23`, `odo 10568.7`,
`iat 30`, `throttle 0`, `baro 101`, `oilt 95`,
`tt 0`; battery ECU `soc 80`, `soh 97`, `packv 388`, `packi 55`, `chg 0`,
`range 290`, `cons 165`, `gear D`, `pwr 3`, `mrpm 6600`, `power 21.3`;
driver assist `eco 78`, `limit 50`, `risk 0`, `lane 3`, `gap 42`;
contract 1.2 `cruise 0`, `setspd 0`, `gapset 0`, `mode 0`, `tp 2.3 2.3 2.2 2.2`,
`ttire 35 35 33 33`, `tripkm 12.3`, `tripmin 18`, `tripavg 41`, `tripfuel 6.5`,
`chgkw 0`, `chgtarget 80`, `chgmin 0`, `plug 0`, `belts 0x03`, `seats 0x03`,
`doors 0`, `windows 0`.

Telltale bits 0-11: engine, oil, battery, brake, left, right, high beam, door,
seatbelt, ABS, traction, TPMS. Bits 12-19 are the EV/hybrid lamps defined by
the `car-can-proxy` contract.

# Drive cycle

`--drive-cycle=cycles/demo.cycle` loops a scripted drive: speed, rpm and
coolant interpolate through 12 phases over 44 s, lamps switch at the phase
boundaries, fuel drains over the lap, and (ev/hybrid) motor power, state of
charge, consumption, range, gear and the driver-assist record follow the
same formulas as qt-cluster-demo's built-in demo, so the proxy path looks
like `--demo`. The file format is documented in `cycles/demo.cycle`; write
another file for a different drive. `cycle stop` / `cycle start` on the
control port pause and resume it (knobs win while paused; `cycle` reads
the state). Without the flag the car sits at fixed values as before.

# As a service (Pi, systemd)

```bash
cmake -S . -B build && cmake --build build
sudo systemctl enable --now /home/pi/car-can-emulator/systemd/car-can-emulator.service
journalctl -u car-can-emulator -f
```

The unit runs `--node=vcan1 --car=ev` by default (the Pi image from
misc-tools uses `--car=hybrid --drive-cycle=...`); copy
`systemd/car-can-emulator.env.example` to `systemd/car-can-emulator.env` to
change it. It is ordered after `can-proxy-links.service` from
`car-can-proxy`, which creates `vcan1`. The OpenWrt init script
(`WrtCarCanEmulatorStartupScr`) is unchanged for `can0` boards.

# Used with car-can-proxy

`car-can-proxy` (https://github.com/hackboxguy/car-can-proxy) reads this
emulator through its `obd2-ice`, `emu-ev` and `emu-hybrid` plugins and
publishes a vehicle-independent contract for instrument-cluster apps; its
integration tests run this emulator on `vcan1` in all three car types.

# Source layout

`src/main.cpp` arguments and threads; `src/State.*` the emulated car's
values and served-PID set; `src/ObdEcu.cpp` the J1979 responder;
`src/BmsEcu.cpp` the UDS/ISO-TP battery ECU; `src/Telltales.cpp` the
`0x420` broadcast; `src/Control.cpp` the port-8080 knobs.

