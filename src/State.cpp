#include "State.h"

EmuState g_state;

void EmuState::selectCar(CarType c)
{
    car = c;
    servedPids.clear();
    switch (c) {
    case CarType::Ice:
        servedPids = { 0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x10, 0x2F, 0x42, 0x46, 0xA6 };
        break;
    case CarType::Ev:
        // No engine, no fuel: an EV's OBD port is thin. Everything else is
        // on the battery ECU over UDS.
        servedPids = { 0x0D, 0x42, 0x46, 0x5B, 0xA6 };
        break;
    case CarType::Hybrid:
        servedPids = { 0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x10, 0x2F, 0x42, 0x46, 0x5B, 0xA6 };
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
