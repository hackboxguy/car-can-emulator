// car-can-emulator: a bench vehicle on a CAN interface.
//
//   --node=<if>            CAN interface (can0, vcan1, ...)
//   --car=ice|ev|hybrid    which kind of car to be (default ice)
//   --drive-cycle=<file>   loop a scripted drive cycle (cycles/demo.cycle);
//                          without it the car sits at fixed values
//   --debugprint=true      print request/response frames
//
// Every car type runs the OBD-II ECU (0x7DF/0x7E8) and the 0x420 telltale
// broadcast; ev and hybrid add a battery/drive ECU answering UDS 0x22 over
// ISO-TP on 0x7E4/0x7EC. Values change at runtime through the control port
// (see Control.cpp and README.md).
#include "DriveCycle.h"
#include "State.h"
#include "Threads.h"

#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

std::atomic<bool> g_running(true);

static void handle_signal(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
        g_running = false;
}

static void printHelp(const std::string &program)
{
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  --node=<canx>       CAN interface, e.g. can0 or vcan1 (or --node <canx>)\n"
              << "  --car=<type>        ice (default), ev or hybrid\n"
              << "  --drive-cycle=<f>   loop a scripted drive cycle from file (see cycles/demo.cycle)\n"
              << "  --debugprint=<flag> true/false, print CAN traffic\n"
              << "  --help              Display this help message\n";
}

int main(int argc, char *argv[])
{
    std::string node, debugprint, car = "ice", cyclePath;
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help")) { printHelp("car-can-emulator"); return 0; }
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg.rfind("--node=", 0) == 0) node = arg.substr(7);
        else if (arg.rfind("--debugprint=", 0) == 0) debugprint = arg.substr(13);
        else if (arg.rfind("--car=", 0) == 0) car = arg.substr(6);
        else if (arg.rfind("--drive-cycle=", 0) == 0) cyclePath = arg.substr(14);
        else if (arg == "--node" && i + 1 < argc) node = argv[++i];
        else if (arg == "--debugprint" && i + 1 < argc) debugprint = argv[++i];
        else if (arg == "--car" && i + 1 < argc) car = argv[++i];
        else { std::cerr << "unknown argument " << arg << "\n"; printHelp("car-can-emulator"); return 2; }
    }
    if (node.empty()) { std::cerr << "--node is required\n"; return 2; }
    for (auto &c : debugprint) c = tolower(c);
    const bool debugflag = (debugprint == "true");

    CarType carType;
    if (!EmuState::parseCar(car, &carType)) { std::cerr << "--car must be ice, ev or hybrid\n"; return 2; }
    g_state.selectCar(carType);
    std::cout << "Emulating a " << EmuState::carName(carType) << " on " << node << "\n";

    DriveCycle cycle;
    if (!cyclePath.empty())
    {
        std::string err;
        if (!DriveCycle::load(cyclePath, cycle, err)) { std::cerr << "drive cycle: " << err << "\n"; return 2; }
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::vector<std::thread> threads;
    threads.emplace_back(socket_listener);
    threads.emplace_back(canbus_listener, debugflag, node);
    threads.emplace_back(telltale_broadcaster, node);
    if (g_state.hasBms())
        threads.emplace_back(bms_ecu, node);
    if (!cyclePath.empty())
        threads.emplace_back(drive_cycle_thread, cycle);
    for (auto &t : threads)
        t.join();
    std::cout << "All threads have exited. Program terminated.\n";
    return 0;
}
