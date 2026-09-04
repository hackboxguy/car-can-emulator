// Battery / drive / driver-assist ECU for ev and hybrid: UDS ReadDataByIdentifier (0x22)
// over ISO-TP, tester 0x7E4 -> ECU 0x7EC, using the kernel's CAN_ISOTP
// socket so every answer is a genuine multi-frame transfer. The DID set is
// the "reference EV profile" documented in car-can-proxy
// (docs/emulator-ev-profile.md); it is modelled on common BMS shapes, not on
// any one manufacturer.
#include "State.h"
#include "Threads.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <linux/can.h>
#include <linux/can/isotp.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

static void put16(std::vector<uint8_t> &v, int value) { v.push_back((value >> 8) & 0xFF); v.push_back(value & 0xFF); }
static void put32(std::vector<uint8_t> &v, uint32_t value) { put16(v, (value >> 16) & 0xFFFF); put16(v, value & 0xFFFF); }

static bool buildDid(uint16_t did, std::vector<uint8_t> &out)
{
    std::lock_guard<std::mutex> lock(g_state.mutex);
    const EmuState &s = g_state;
    out = { 0x62, (uint8_t)(did >> 8), (uint8_t)(did & 0xFF) };
    switch (did)
    {
    case 0x0101:   // pack status
        put16(out, (int)(s.packVoltage * 10.0 + 0.5));
        put16(out, (int)(s.packCurrent * 10.0 + (s.packCurrent >= 0 ? 0.5 : -0.5)) & 0xFFFF);
        out.push_back((uint8_t)(s.soc * 2.0 + 0.5));
        out.push_back((uint8_t)(s.soh * 2.0 + 0.5));
        out.push_back((uint8_t)s.charging);
        out.push_back(0);
        return true;
    case 0x0102:   // range, consumption, odometer
        put16(out, s.rangeKm);
        put16(out, s.consumption & 0xFFFF);
        put32(out, s.odo);
        return true;
    case 0x0103:   // drive
        out.push_back((uint8_t)s.gear);
        out.push_back((uint8_t)s.powerState);
        put16(out, s.motorRpm);
        put16(out, (int)(s.motorPowerKw * 10.0 + (s.motorPowerKw >= 0 ? 0.5 : -0.5)) & 0xFFFF);
        put16(out, 0);
        return true;
    case 0x0104:   // driver assist
        out.push_back((uint8_t)s.ecoScore);
        out.push_back((uint8_t)s.speedLimit);
        out.push_back((uint8_t)s.collisionRisk);
        out.push_back((uint8_t)s.laneState);
        put16(out, (int)(s.leadGapM * 10.0 + 0.5));
        put16(out, 0);
        return true;
    default:
        return false;
    }
}

void bms_ecu(std::string node)
{
    int fd = socket(PF_CAN, SOCK_DGRAM, CAN_ISOTP);
    if (fd < 0)
    {
        perror("ISO-TP socket (battery ECU disabled; is the can-isotp module loaded?)");
        return;
    }
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, node.c_str(), IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) { perror(node.c_str()); close(fd); return; }
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof addr);
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    addr.can_addr.tp.tx_id = 0x7EC;
    addr.can_addr.tp.rx_id = 0x7E4;
    if (bind(fd, (struct sockaddr*)&addr, sizeof addr) < 0) { perror("ISO-TP bind"); close(fd); return; }
    std::cout << "Battery ECU (UDS over ISO-TP) started on 0x7E4/0x7EC\n";

    while (g_running)
    {
        struct pollfd pfd = { fd, POLLIN, 0 };
        int rc = poll(&pfd, 1, 500);
        if (rc < 0 && errno != EINTR) { perror("poll"); break; }
        if (rc <= 0) continue;
        uint8_t req[64];
        ssize_t n = read(fd, req, sizeof req);
        if (n <= 0) continue;
        std::vector<uint8_t> resp;
        if (n >= 3 && req[0] == 0x22)
        {
            const uint16_t did = (req[1] << 8) | req[2];
            if (!buildDid(did, resp))
                resp = { 0x7F, 0x22, 0x31 };     // requestOutOfRange
        }
        else
        {
            resp = { 0x7F, (uint8_t)(n > 0 ? req[0] : 0), 0x11 };   // serviceNotSupported
        }
        if (write(fd, resp.data(), resp.size()) != (ssize_t)resp.size())
            perror("ISO-TP write");
    }
    close(fd);
    std::cout << "Battery ECU stopped.\n";
}
