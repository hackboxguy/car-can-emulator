// The cycle mirrors qt-cluster-demo's DemoSimulator: speed, rpm and coolant
// interpolate per phase; lamps switch at boundaries; fuel drains over a lap;
// the electric side (power, SoC, consumption, range, gear) and the driver
// assist record (posted limit, lead gap, collision risk) follow the same
// formulas the demo uses, so the two look alike on screen.
#include "DriveCycle.h"
#include "State.h"
#include "Threads.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

bool g_cycleRunning = true;

int DriveCycle::totalMs() const
{
    int t = 0;
    for (const auto &p : phases) t += p.durMs;
    return t;
}

bool DriveCycle::lampMask(const std::string &list, uint32_t &mask)
{
    static const char *names[] = { "engine", "oil", "battery", "brake", "left", "right", "highbeam", "door",
                                   "seatbelt", "abs", "traction", "tpms", "ev-ready", "charging", "limited-power",
                                   "low-traction-battery", "park-brake", "low-fuel", "fog", "hv-fault" };
    mask = 0;
    if (list == "-" || list.empty()) return true;
    std::stringstream ss(list);
    std::string item;
    while (std::getline(ss, item, ',')) {
        bool found = false;
        for (int i = 0; i < 20; i++)
            if (item == names[i]) { mask |= 1u << i; found = true; break; }
        if (!found) return false;
    }
    return true;
}

bool DriveCycle::load(const std::string &path, DriveCycle &out, std::string &error)
{
    std::ifstream in(path);
    if (!in) { error = "cannot open " + path; return false; }
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        lineNo++;
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::stringstream ss(line);
        std::string key;
        if (!(ss >> key)) continue;
        auto bad = [&](const std::string &why) { error = path + ":" + std::to_string(lineNo) + ": " + why; return false; };
        if (key == "tick_ms") { if (!(ss >> out.tickMs) || out.tickMs < 10 || out.tickMs > 1000) return bad("tick_ms 10..1000"); }
        else if (key == "fuel_start") { if (!(ss >> out.fuelStart)) return bad("fuel_start"); }
        else if (key == "fuel_end") { if (!(ss >> out.fuelEnd)) return bad("fuel_end"); }
        else if (key == "soc_end") { if (!(ss >> out.socEnd)) return bad("soc_end"); }
        else if (key == "soc_start") { if (!(ss >> out.socStart)) return bad("soc_start"); }
        else if (key == "odometer_start") { if (!(ss >> out.odometerStart)) return bad("odometer_start"); }
        else if (key == "phase") {
            DrivePhase p;
            std::string setL, clrL;
            if (!(ss >> p.durMs >> p.speed0 >> p.speed1 >> p.rpm0 >> p.rpm1 >> p.coolant0 >> p.coolant1 >> setL >> clrL))
                return bad("phase needs: ms speed0 speed1 rpm0 rpm1 coolant0 coolant1 set clear");
            if (p.durMs <= 0) return bad("phase duration must be positive");
            if (!lampMask(setL, p.setLamps) || !lampMask(clrL, p.clearLamps)) return bad("unknown lamp name");
            out.phases.push_back(p);
        }
        else return bad("unknown key " + key);
    }
    if (out.phases.empty()) { error = path + ": no phases"; return false; }
    return true;
}

namespace {

unsigned short rpmRaw(double rpm)
{
    // ObdEcu sends A = low byte of the raw field, B = high byte; the wire
    // value is (A*256+B)/4. So raw = (hi(4*rpm)) | (lo(4*rpm) << 8).
    const int q = static_cast<int>(rpm * 4.0 + 0.5);
    return static_cast<unsigned short>(((q >> 8) & 0xFF) | ((q & 0xFF) << 8));
}

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

void drive_cycle_thread(DriveCycle cycle)
{
    using clock = std::chrono::steady_clock;
    const int total = cycle.totalMs();
    std::cout << "Drive cycle: " << cycle.phases.size() << " phases, " << total / 1000.0 << " s per lap, tick "
              << cycle.tickMs << " ms\n";

    int phase = 0;
    int phaseElapsed = 0;
    int lapElapsed = 0;
    double powerFiltered = 0.0, efficiencyFiltered = 142.0;
    double soc = cycle.socStart, odometer = cycle.odometerStart;
    double leadGap = 45.0;
    double tripKm = 0.0;
    long tripMs = 0;
    double tireHeat = 0.0;
    int limitKph = 50;
    int limitTimer = 0;
    uint32_t lamps = 0;
    bool entering = true;
    const double dtHours = cycle.tickMs / 3.6e6;
    const double alpha = 0.06 * cycle.tickMs / 50.0;

    auto next = clock::now();
    while (g_running) {
        next += std::chrono::milliseconds(cycle.tickMs);
        if (!g_cycleRunning) { std::this_thread::sleep_until(next); continue; }

        const DrivePhase &p = cycle.phases[phase];
        if (entering) {
            lamps |= p.setLamps;
            lamps &= ~p.clearLamps;
            entering = false;
        }
        const double t = clampd(static_cast<double>(phaseElapsed) / p.durMs, 0.0, 1.0);
        const double speed = p.speed0 + (p.speed1 - p.speed0) * t;
        const double rpm = std::max(0.0, p.rpm0 + (p.rpm1 - p.rpm0) * t + (std::rand() % 41 - 20));
        const double coolant = p.coolant0 + (p.coolant1 - p.coolant0) * t;
        const double accel = (p.speed1 - p.speed0) * 1000.0 / p.durMs;          // km/h per s, the phase's own slope
        const double fuel = cycle.fuelStart - (cycle.fuelStart - cycle.fuelEnd) * lapElapsed / total;
        const double batt = 12.6 + (std::rand() % 100 - 50) * 0.002;

        // Electric side, DemoSimulator::emitEvSignals.
        double target = accel * 3.2 + (accel >= 0.0 ? speed * 0.20 : 0.0);
        target = clampd(target, -100.0, 100.0);
        powerFiltered += (target - powerFiltered) * alpha;
        const double flow = clampd(std::round(powerFiltered), -100.0, 100.0);
        const double powerKw = flow / 100.0 * 90.0;
        // Charge follows the lap the way fuel does, socStart down to socEnd
        // and back at the top of the next one. It used to integrate power into
        // a 60 kWh pack instead, which drains about 1.25%/min and never
        // recovers: an hour into a run the pack sat clamped at 0 and the range
        // readout with it, which is not a state the demo is meant to show.
        soc = cycle.socStart - (cycle.socStart - cycle.socEnd) * lapElapsed / total;
        odometer += speed * dtHours;
        const double effTarget = 142.0 + (speed > 120.0 ? (speed - 120.0) * 0.5 : 0.0);
        efficiencyFiltered += (effTarget - efficiencyFiltered) * alpha;
        const int range = static_cast<int>((soc / 100.0) * 60000.0 / 142.0);

        // Driver assist, HarmanCluster's stand-ins turned into vehicle data.
        const double coach = std::fabs(flow) <= 25.0 ? 0.0 : std::copysign(std::min(1.0, (std::fabs(flow) - 25.0) / 75.0), flow);
        limitTimer += cycle.tickMs;
        if (limitTimer >= 2000 || lapElapsed == 0) {
            limitTimer = 0;
            static const int bands[] = { 30, 50, 70, 80, 100, 120, 130 };
            limitKph = 130;
            for (int b : bands) if (speed <= b) { limitKph = b; break; }
        }
        {
            const double gapTarget = 12.0 + speed * 0.32;
            const double relax = (gapTarget - leadGap) * 0.02 * cycle.tickMs / 100.0;
            leadGap = clampd(leadGap + relax - coach * 0.30 * cycle.tickMs / 100.0, 6.0, 70.0);
        }
        const bool braking = (lamps & (1u << 3)) != 0;
        int risk = 0;
        if (braking && speed > 40.0) risk = 3;
        else if (leadGap < 6.0 + speed * 0.22) risk = 1;

        // v1.2: engine detail follows the electric side's power figure; the
        // cruise, tires, trip and occupancy records follow the phase.
        tripKm += speed * dtHours;
        tripMs += cycle.tickMs;
        tireHeat += ((speed > 5.0 ? 1.0 : 0.0) - tireHeat) * cycle.tickMs / 30000.0;
        const bool steady = p.speed0 == p.speed1 && speed > 60.0;
        const int throttlePct = flow > 0 ? static_cast<int>(flow) : 0;
        const int tripMin = static_cast<int>(tripMs / 60000);

        {
            std::lock_guard<std::mutex> lock(g_state.mutex);
            EmuState &s = g_state;
            s.speed = static_cast<unsigned short>(speed + 0.5);
            s.rpm = rpmRaw(rpm);
            s.temp = static_cast<unsigned short>(coolant + 40.0 + 0.5);
            s.fuel = static_cast<unsigned char>(fuel * 255.0 / 100.0 + 0.5);
            s.volt = static_cast<unsigned short>(batt * 1000.0 + 0.5);
            s.odo = static_cast<unsigned int>(odometer * 10.0 + 0.5);
            s.telltales = lamps;
            s.motorPowerKw = powerKw;
            s.packCurrent = powerKw * 1000.0 / s.packVoltage;
            s.soc = soc;
            s.consumption = static_cast<int>(efficiencyFiltered + 0.5);
            s.rangeKm = range;
            s.gear = speed > 0.5 ? 3 : 0;
            s.powerState = 3;
            s.motorRpm = static_cast<int>(speed * 75.0);
            s.ecoScore = static_cast<int>(100.0 - std::fabs(flow));
            s.speedLimit = limitKph;
            s.leadGapM = leadGap;
            s.collisionRisk = risk;
            s.laneState = speed > 30.0 ? 3 : 0;
            // v1.2
            s.throttle = throttlePct;
            s.intake = static_cast<unsigned char>(35 + throttlePct * 1.1);          // MAP kPa: vacuum at idle, boost at full power
            s.oilTemp = static_cast<int>(coolant + 8.0);
            s.iat = 30;
            s.cruiseState = steady ? 2 : (speed > 30.0 ? 1 : 0);
            s.cruiseSetKmh = steady ? speed : 0.0;
            s.cruiseGap = steady ? 3 : 0;
            for (int i = 0; i < 4; i++) {
                s.tirePressure[i] = (i < 2 ? 2.3 : 2.2) + 0.15 * tireHeat;
                s.tireTemp[i] = static_cast<int>(23.0 + 45.0 * tireHeat + (i < 2 ? 3 : 0));
            }
            s.tripKm = tripKm;
            s.tripMin = tripMin;
            s.tripAvgKmh = tripMs > 1000 ? static_cast<int>(tripKm / (tripMs / 3.6e6) + 0.5) : 0;
            s.belts = (lamps & (1u << 8)) ? 0x01u : 0x03u;                          // seatbelt lamp: passenger unbuckled
            s.doors = (lamps & (1u << 7)) ? 0x01u : 0x00u;                          // door lamp: front left ajar
            s.windows = speed < 45.0 ? 0x01u : 0x00u;
        }

        phaseElapsed += cycle.tickMs;
        lapElapsed += cycle.tickMs;
        if (phaseElapsed >= p.durMs) {
            phaseElapsed = 0;
            phase = (phase + 1) % static_cast<int>(cycle.phases.size());
            entering = true;
            if (phase == 0) lapElapsed = 0;
        }
        std::this_thread::sleep_until(next);
    }
}
