// Shared emulator state: what the emulated car currently reports. Written by
// the control port, read by the ECU threads. One mutex, short critical
// sections; no thread holds it across a socket call.
#pragma once
#include <cstdint>
#include <mutex>
#include <set>
#include <string>

enum class CarType { Ice, Ev, Hybrid };

struct EmuState {
    std::mutex mutex;
    CarType car = CarType::Ice;

    // Legacy OBD-II knobs keep their raw-byte semantics (see README).
    unsigned short speed = 0x0058;    // PID 0x0D, km/h
    unsigned short temp = 35;         // PID 0x05 raw A (A - 40 degC)
    unsigned short rpm = 12;          // PID 0x0C raw A (rpm = A * 64)
    unsigned short flow = 0x0540;     // PID 0x10 raw
    unsigned char intake = 0;         // PID 0x0B raw A
    unsigned char load = 0;           // PID 0x04 raw A

    // Newer knobs take physical units.
    unsigned char fuel = 191;         // PID 0x2F raw A, 191 = 75 %
    unsigned short volt = 12600;      // PID 0x42 mV
    unsigned char ambient = 63;       // PID 0x46 raw A, 63 = 23 degC
    unsigned int odo = 105687;        // PID 0xA6 0.1 km
    uint32_t telltales = 0;           // 0x420 broadcast

    // Battery / drive ECU (ev, hybrid), physical units.
    double packVoltage = 388.0;       // V
    double packCurrent = 55.0;        // A, negative = charging
    double soc = 80.0;                // %
    double soh = 97.0;                // %
    int charging = 0;                 // 0 none, 1 AC, 2 DC, 3 complete
    int rangeKm = 290;
    int consumption = 165;            // Wh/km
    int gear = 3;                     // 0 P 1 R 2 N 3 D 4 L
    int powerState = 3;               // 0 off 1 acc 2 on 3 ready
    int motorRpm = 6600;
    double motorPowerKw = 21.3;       // negative = regeneration

    // PIDs the OBD ECU serves for the current car type; bitmaps derive from it.
    std::set<uint8_t> servedPids;
    void selectCar(CarType c);
    uint32_t supportedBitmap(uint8_t base) const;   // caller holds mutex or is single-threaded at startup
    bool hasBms() const { return car != CarType::Ice; }
    static const char *carName(CarType c);
    static bool parseCar(const std::string &s, CarType *out);
};

extern EmuState g_state;
