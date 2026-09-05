#include "State.h"

EmuState g_state;

void EmuState::resetKnobs()
{
    EmuState fresh;
    fresh.selectCar(car);
    speed = fresh.speed; temp = fresh.temp; rpm = fresh.rpm; flow = fresh.flow;
    intake = fresh.intake; load = fresh.load;
    iat = fresh.iat; throttle = fresh.throttle; baro = fresh.baro; oilTemp = fresh.oilTemp;
    fuel = fresh.fuel; volt = fresh.volt; ambient = fresh.ambient; odo = fresh.odo;
    telltales = fresh.telltales;
    packVoltage = fresh.packVoltage; packCurrent = fresh.packCurrent;
    soc = fresh.soc; soh = fresh.soh; charging = fresh.charging;
    rangeKm = fresh.rangeKm; consumption = fresh.consumption;
    gear = fresh.gear; powerState = fresh.powerState;
    motorRpm = fresh.motorRpm; motorPowerKw = fresh.motorPowerKw;
    ecoScore = fresh.ecoScore; speedLimit = fresh.speedLimit;
    collisionRisk = fresh.collisionRisk; laneState = fresh.laneState; leadGapM = fresh.leadGapM;
    cruiseState = fresh.cruiseState; cruiseSetKmh = fresh.cruiseSetKmh; cruiseGap = fresh.cruiseGap; driveMode = fresh.driveMode;
    for (int i = 0; i < 4; i++) { tirePressure[i] = fresh.tirePressure[i]; tireTemp[i] = fresh.tireTemp[i]; }
    tripKm = fresh.tripKm; tripMin = fresh.tripMin; tripAvgKmh = fresh.tripAvgKmh; tripFuelL100 = fresh.tripFuelL100;
    chargeKw = fresh.chargeKw; chargeTargetPct = fresh.chargeTargetPct; chargeMin = fresh.chargeMin; plug = fresh.plug;
    belts = fresh.belts; seats = fresh.seats; doors = fresh.doors; windows = fresh.windows;
}

void EmuState::selectCar(CarType c)
{
    car = c;
    servedPids.clear();
    switch (c) {
    case CarType::Ice:
        servedPids = { 0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x0F, 0x10, 0x11, 0x2F, 0x33, 0x42, 0x46, 0x5C, 0xA6 };
        break;
    case CarType::Ev:
        // No engine, no fuel: an EV's OBD port is thin. Everything else is
        // on the battery ECU over UDS.
        servedPids = { 0x0D, 0x42, 0x46, 0x5B, 0xA6 };
        break;
    case CarType::Hybrid:
        servedPids = { 0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x0F, 0x10, 0x11, 0x2F, 0x33, 0x42, 0x46, 0x5B, 0x5C, 0xA6 };
        break;
    }
}

// Bit 7 of byte A is PID base+1 ... bit 0 of byte D is PID base+0x20, which
// also says "next block exists". Blocks past the last served PID are not
// advertised at all.
uint32_t EmuState::supportedBitmap(uint8_t base) const
{
    uint32_t word = 0;
    uint8_t highest = 0;
    for (uint8_t pid : servedPids)
        if (pid > highest) highest = pid;
    for (int n = 1; n <= 31; n++) {
        const int pid = base + n;
        if (pid <= 0xFF && servedPids.count(static_cast<uint8_t>(pid)))
            word |= 1u << (32 - n);
    }
    if (base + 0x20 <= 0xE0 && highest > base + 0x20)
        word |= 1u;
    return word;
}

const char *EmuState::carName(CarType c)
{
    switch (c) {
    case CarType::Ice: return "ice";
    case CarType::Ev: return "ev";
    case CarType::Hybrid: return "hybrid";
    }
    return "?";
}

bool EmuState::parseCar(const std::string &s, CarType *out)
{
    if (s == "ice") { *out = CarType::Ice; return true; }
    if (s == "ev") { *out = CarType::Ev; return true; }
    if (s == "hybrid") { *out = CarType::Hybrid; return true; }
    return false;
}
