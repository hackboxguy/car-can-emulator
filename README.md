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
6. ```./Output/car-can-emulator --node=can0 --debugprint=true```
7. From a second terminal, run ``` echo -n speed | nc 127.0.0.1 8080``` to read the current speed
8. From a second terminal, run ``` echo -n "speed 90" | nc 127.0.0.1 8080``` to set the current speed

Without hardware, a virtual bus works the same way: `sudo ip link add dev vcan1 type vcan && sudo ip link set up vcan1`, then `--node=vcan1`.

# What it emulates

An OBD-II ECU answering functional requests on `0x7DF` from `0x7E8`, plus a
body-controller style telltale broadcast. It advertises exactly the PIDs it
serves in the supported-PID bitmaps (`0x00`, `0x20`, ... `0xC0`) and, like a
real ECU, does not answer a PID it lacks.

| PID | Signal | netcat knob | Unit |
|---|---|---|---|
| `0x04` | engine load | `load <A>` | raw byte |
| `0x05` | coolant temperature | `temp <A>` | raw byte (A - 40 = degC) |
| `0x0B` | intake air temperature | `intake <A>` | raw byte |
| `0x0C` | engine RPM | `rpm <A>` | raw byte A (rpm = A * 64) |
| `0x0D` | vehicle speed | `speed <kmh>` | km/h |
| `0x10` | MAF air flow | `flow <raw>` | raw 16-bit |
| `0x2F` | fuel tank level | `fuel <pct>` | percent |
| `0x42` | control module voltage | `volt <V>` | volts |
| `0x46` | ambient air temperature | `ambient <degC>` | degrees C |
| `0xA6` | odometer | `odo <km>` | km |
| `0x420` (broadcast, 100 ms) | telltale bitmask, 32-bit little-endian | `tt <mask>` | hex or decimal |

A knob without a value reads the current setting. The `speed`, `rpm`, `temp`,
`flow`, `intake` and `load` knobs keep their historical raw semantics; the
newer knobs take physical units.

Telltale bits 0-11: engine, oil, battery, brake, left, right, high beam, door,
seatbelt, ABS, traction, TPMS. Bits 12-19 are the EV/hybrid lamps defined by
the `car-can-proxy` contract.

# Used with car-can-proxy

`car-can-proxy` (https://github.com/hackboxguy/car-can-proxy) reads this
emulator through its `obd2-ice` plugin and publishes a vehicle-independent
contract for instrument-cluster apps; its integration tests run this emulator
on `vcan1`. EV and hybrid modes for the emulator are planned there.

