#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A scripted, looping drive cycle that writes the emulated car's values
// every tick, so the vehicle the proxy reads moves like the cluster's own
// demo. Loaded from a text file (cycles/demo.cycle documents the format).
struct DrivePhase {
    int durMs;
    double speed0, speed1, rpm0, rpm1, coolant0, coolant1;
    uint32_t setLamps, clearLamps;
};

struct DriveCycle {
    int tickMs = 50;
    double fuelStart = 75, fuelEnd = 60, socStart = 80, socEnd = 66, odometerStart = 10568;
    std::vector<DrivePhase> phases;
    int totalMs() const;

    // Parse a cycle file; returns false with `error` set on a bad line.
    static bool load(const std::string &path, DriveCycle &out, std::string &error);
    static bool lampMask(const std::string &list, uint32_t &mask);
};

void drive_cycle_thread(DriveCycle cycle);
extern bool g_cycleRunning;   // control port: "cycle start|stop"
