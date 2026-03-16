# car-can-emulator

CAN bus OBD2 emulator for testing automotive devices such as head-up displays, instrument clusters, and car TCUs — without sitting in an actual car.

Emulates standard SAE J1979 OBD2 PIDs over CAN bus, with a TCP control interface for setting values at runtime. Includes telltale indicator signals and a drive simulation mode.

## Connection Diagram

![connection-diagram](/images/connection-diagram.png "connection-diagram")

## Features

- **OBD2 PID emulation**: Speed, RPM, temperature, engine load, intake pressure, MAF flow, fuel level, battery voltage
- **SAE J1979 compliant**: Correct encoding (RPM×4 BE, temp+40, etc.), responds to both 0x7DF and 0x7E0
- **Supported PIDs advertisement**: Properly reports supported PIDs via 0x00, 0x20, 0x40
- **Telltale indicators**: Dashboard warning lights on CAN ID 0x420 (engine, oil, brake, turn signals, etc.)
- **Drive simulation**: Automatic drive cycle with `--simulate` flag
- **TCP control interface**: Set/read any value via netcat
- **Multi-platform**: Runs on Raspberry Pi OS, Buildroot, OpenWrt, or any Linux with CAN support

## Quick Start (Local Development)

### With CAN Hardware

1. Plug in a USB-to-CAN adapter (e.g., CANable)
2. Configure the CAN interface:
   ```bash
   sudo ip link set can0 type can bitrate 500000
   sudo ip link set up can0
   ```
3. Build and run:
   ```bash
   cmake -H. -BOutput
   cmake --build Output
   ./Output/car-can-emulator --node=can0 --debugprint=true
   ```

### With Virtual CAN (No Hardware Needed)

```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

cmake -H. -BOutput
cmake --build Output
./Output/car-can-emulator --node=vcan0 --debugprint=true
```

Monitor traffic with: `candump vcan0`

## TCP Control Interface

Default port: 8080. Use netcat to send commands:

### OBD2 Parameters

| Command | Description | Range |
|---|---|---|
| `echo -n "speed" \| nc 127.0.0.1 8080` | Read current speed | |
| `echo -n "speed 120" \| nc 127.0.0.1 8080` | Set speed to 120 km/h | 0-255 |
| `echo -n "rpm 3000" \| nc 127.0.0.1 8080` | Set RPM to 3000 | 0-16383 |
| `echo -n "temp 90" \| nc 127.0.0.1 8080` | Set coolant temp to 90°C | -40 to 215 |
| `echo -n "load 50" \| nc 127.0.0.1 8080` | Set engine load to 50% | 0-100 |
| `echo -n "fuel 75" \| nc 127.0.0.1 8080` | Set fuel level to 75% | 0-100 |
| `echo -n "battery 12600" \| nc 127.0.0.1 8080` | Set battery to 12.6V | 0-65535 (mV) |
| `echo -n "intake 101" \| nc 127.0.0.1 8080` | Set intake pressure (kPa) | 0-255 |
| `echo -n "flow 1600" \| nc 127.0.0.1 8080` | Set MAF air flow | 0-65535 |
| `echo -n "list" \| nc 127.0.0.1 8080` | Show all current values | |

### Telltale Indicators

| Command | Description |
|---|---|
| `echo -n "telltale engine on" \| nc 127.0.0.1 8080` | Turn on check engine light |
| `echo -n "telltale brake off" \| nc 127.0.0.1 8080` | Turn off brake warning |
| `echo -n "telltale all off" \| nc 127.0.0.1 8080` | Clear all indicators |
| `echo -n "telltale" \| nc 127.0.0.1 8080` | Read current state (hex) |

Available indicators: `engine`, `oil`, `battery`, `brake`, `left`, `right`, `highbeam`, `door`, `seatbelt`, `abs`, `traction`, `tpms`

### Simulation Control

| Command | Description |
|---|---|
| `echo -n "pause" \| nc 127.0.0.1 8080` | Pause drive simulation |
| `echo -n "resume" \| nc 127.0.0.1 8080` | Resume drive simulation |

## Command-Line Options

```
--node=<canx>           CAN interface (e.g., can0, vcan0)
--port=<N>              TCP port for control interface (default: 8080)
--debugprint=<flag>     Print CAN frames to stdout (true/false)
--bind-all              Bind TCP to all interfaces (default: localhost only)
--simulate              Enable drive simulation mode
--simulate-speed=<N>    Simulation speed multiplier (default: 1.0, range: 0.01-100)
--help                  Display help message
```

## Configuration File

The emulator reads a config file on startup (command-line args override):

**Search order**: `./car-can-emulator.conf` → `./config/car-can-emulator.conf` → `/etc/car-can-emulator.conf`

```ini
CAN_NODE=can0
TCP_PORT=8080
DEBUG_PRINT=false
BIND_ALL=false
SIMULATE=false
SIMULATE_SPEED=1.0
```

## Platform Deployment

### Raspberry Pi OS Lite / systemd

```bash
cmake -H. -BOutput -DPLATFORM=systemd
cmake --build Output
sudo cmake --install Output
sudo systemctl enable car-can-emulator
sudo systemctl start car-can-emulator
```

The systemd unit file handles CAN interface setup/teardown automatically.
Edit `/etc/default/car-can-emulator` to override `CAN_NODE` or `CAN_BITRATE`, or edit `/etc/car-can-emulator.conf` for application settings.

### Buildroot

Add as an external package:

1. Copy `deploy/buildroot/` contents to your Buildroot `package/car-can-emulator/` directory
2. Add `source "package/car-can-emulator/Config.in"` to your package `Config.in`
3. Enable in `make menuconfig` → Target packages → car-can-emulator
4. Optionally enable the systemd service sub-option
5. `make`

### OpenWrt

Cross-compile with the OpenWrt toolchain:

```bash
cmake -H. -BOutput -DPLATFORM=openwrt -DCMAKE_TOOLCHAIN_FILE=/path/to/openwrt-toolchain.cmake
cmake --build Output
```

The init script reads settings from `/etc/car-can-emulator.conf`.

## Build Commands

```bash
# Local:     cmake -H. -BOutput && cmake --build Output
# systemd:   cmake -H. -BOutput -DPLATFORM=systemd && cmake --build Output
# OpenWrt:   cmake -H. -BOutput -DPLATFORM=openwrt -DCMAKE_TOOLCHAIN_FILE=... && cmake --build Output
# Install:   sudo cmake --install Output
# Clean:     rm -rf Output
```
