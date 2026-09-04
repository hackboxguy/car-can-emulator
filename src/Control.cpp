// Control port: "knob" reads, "knob value" writes, one command per TCP
// connection on port 8080 (echo -n "speed 90" | nc 127.0.0.1 8080).
#include "State.h"
#include "Threads.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::string handle(const std::string &cmdIn, const std::string &arg)
{
    std::string cmd = cmdIn;
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
    const bool set = !arg.empty();
    char out[64];
    out[0] = 0;
    std::lock_guard<std::mutex> lock(g_state.mutex);
    EmuState &s = g_state;
    auto clampd = [](double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); };
    auto clampi = [](long v, long lo, long hi) { return v < lo ? lo : (v > hi ? hi : v); };

    // legacy raw knobs
    if (cmd == "speed")        { if (set) s.speed = atoi(arg.c_str()); else sprintf(out, "%d\n", s.speed); }
    else if (cmd == "rpm")     { if (set) s.rpm = atoi(arg.c_str()); else sprintf(out, "%d\n", s.rpm); }
    else if (cmd == "temp")    { if (set) s.temp = atoi(arg.c_str()); else sprintf(out, "%d\n", s.temp); }
    else if (cmd == "flow")    { if (set) s.flow = atoi(arg.c_str()); else sprintf(out, "%d\n", s.flow); }
    else if (cmd == "intake")  { if (set) s.intake = atoi(arg.c_str()); else sprintf(out, "%d\n", s.intake); }
    else if (cmd == "load")    { if (set) s.load = atoi(arg.c_str()); else sprintf(out, "%d\n", s.load); }
    // physical-unit knobs
    else if (cmd == "fuel")    { if (set) s.fuel = (unsigned char)(clampi(atol(arg.c_str()), 0, 100) * 255 / 100); else sprintf(out, "%d\n", s.fuel * 100 / 255); }
    else if (cmd == "volt")    { if (set) s.volt = (unsigned short)(atof(arg.c_str()) * 1000.0 + 0.5); else sprintf(out, "%.3f\n", s.volt / 1000.0); }
    else if (cmd == "ambient") { if (set) s.ambient = (unsigned char)(clampi(atol(arg.c_str()), -40, 215) + 40); else sprintf(out, "%d\n", (int)s.ambient - 40); }
    else if (cmd == "odo")     { if (set) s.odo = (unsigned int)(atof(arg.c_str()) * 10.0 + 0.5); else sprintf(out, "%.1f\n", s.odo / 10.0); }
    else if (cmd == "tt")      { if (set) s.telltales = (uint32_t)strtoul(arg.c_str(), NULL, 0); else sprintf(out, "0x%08X\n", s.telltales); }
    // battery / drive ECU
    else if (cmd == "soc")     { if (set) s.soc = clampd(atof(arg.c_str()), 0, 100); else sprintf(out, "%.1f\n", s.soc); }
    else if (cmd == "soh")     { if (set) s.soh = clampd(atof(arg.c_str()), 0, 100); else sprintf(out, "%.1f\n", s.soh); }
    else if (cmd == "packv")   { if (set) s.packVoltage = clampd(atof(arg.c_str()), 0, 6000); else sprintf(out, "%.1f\n", s.packVoltage); }
    else if (cmd == "packi")   { if (set) s.packCurrent = clampd(atof(arg.c_str()), -3000, 3000); else sprintf(out, "%.1f\n", s.packCurrent); }
    else if (cmd == "chg")     { if (set) s.charging = (int)clampi(atol(arg.c_str()), 0, 3); else sprintf(out, "%d\n", s.charging); }
    else if (cmd == "range")   { if (set) s.rangeKm = (int)clampi(atol(arg.c_str()), 0, 65000); else sprintf(out, "%d\n", s.rangeKm); }
    else if (cmd == "cons")    { if (set) s.consumption = (int)clampi(atol(arg.c_str()), -30000, 30000); else sprintf(out, "%d\n", s.consumption); }
    else if (cmd == "gear")    {
        if (set) { const char g = toupper(arg[0]); s.gear = g=='P'?0:g=='R'?1:g=='N'?2:g=='D'?3:g=='L'?4:(int)clampi(atol(arg.c_str()),0,4); }
        else sprintf(out, "%c\n", "PRNDL"[s.gear]);
    }
    else if (cmd == "pwr")     { if (set) s.powerState = (int)clampi(atol(arg.c_str()), 0, 3); else sprintf(out, "%d\n", s.powerState); }
    else if (cmd == "mrpm")    { if (set) s.motorRpm = (int)clampi(atol(arg.c_str()), 0, 65000); else sprintf(out, "%d\n", s.motorRpm); }
    else if (cmd == "power")   { if (set) s.motorPowerKw = clampd(atof(arg.c_str()), -3000, 3000); else sprintf(out, "%.1f\n", s.motorPowerKw); }
    else if (cmd == "car")     { sprintf(out, "%s\n", EmuState::carName(s.car)); }
    else                       { sprintf(out, "unknown command\n"); }
    return out;
}

} // namespace

void socket_listener()
{
    int sockfd;
    struct sockaddr_in server_addr;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) { perror("Socket creation failed"); return; }
    int reuse = 1;   // so a restart is not refused by the previous run's TIME_WAIT sockets
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) { perror("Socket bind failed"); close(sockfd); return; }
    if (listen(sockfd, 3) < 0) { perror("Socket listen failed"); close(sockfd); return; }
    std::cout << "Socket listener started on port 8080.\n";

    while (g_running)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);
        struct timeval timeout = { 1, 0 };
        int activity = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0 && errno != EINTR) { perror("Select error"); break; }
        if (!FD_ISSET(sockfd, &read_fds))
            continue;
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int new_socket = accept(sockfd, (struct sockaddr*)&client_addr, &addr_len);
        if (new_socket < 0) { if (g_running) perror("Socket accept failed"); break; }
        char buffer[1024] = {0};
        ssize_t n = read(new_socket, buffer, sizeof(buffer) - 1);
        if (n > 0)
        {
            std::string cmd, arg;
            std::stringstream msg(std::string(buffer, (size_t)n));
            msg >> cmd >> arg;
            const std::string reply = handle(cmd, arg);
            if (!reply.empty())
                if (write(new_socket, reply.c_str(), reply.size()) < 0) perror("write");
        }
        close(new_socket);
    }
    close(sockfd);
    std::cout << "Socket listener stopped.\n";
}
