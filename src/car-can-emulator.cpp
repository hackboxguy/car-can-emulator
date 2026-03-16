//can emulated server with ability to change params via netcat command
//echo -n "temp" | nc 127.0.0.1 8080 (reads current engine temperature)
//echo -n "temp 40" | nc 127.0.0.1 8080
//echo -n "flow 1600" | nc 127.0.0.1 8080
//echo -n "speed 120" | nc 127.0.0.1 8080
//echo -n "rpm 4" | nc 127.0.0.1 8080

#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <string>
#include <sstream>
#include <algorithm>
#include <climits>
#include <fstream>
#include <map>
#include <chrono>
#include "config-utils.h"
#include "obd-encode.h"
// Global running flag and error tracking
std::atomic<bool> running(true);
std::atomic<bool> exit_failure(false); // set by threads on fatal errors
std::atomic<bool> sim_paused(false);

std::atomic<int> obd_speed{88}, obd_temp{35}, obd_rpm{12}, obd_flow{0x0540};
std::atomic<int> obd_intake{0}, obd_load{0};
std::atomic<int> obd_fuel{75}, obd_battery{12600}; // fuel=75%, battery=12600mV (12.6V)

struct OBDParam {
    const char *name;
    std::atomic<int> *var;
    long min_val;
    long max_val;
};

static OBDParam obd_params[] = {
    {"speed",  &obd_speed,   0,   255},
    {"rpm",    &obd_rpm,     0, 16383},
    {"temp",   &obd_temp,  -40,   215},
    {"flow",   &obd_flow,    0, 65535},
    {"intake", &obd_intake,  0,   255},
    {"load",   &obd_load,    0,   100},
    {"fuel",   &obd_fuel,    0,   100},
    {"battery",&obd_battery,  0, 65535},
};

// Telltale indicator state: 2-byte bitfield broadcast on CAN ID 0x420
// Byte 0: engine(0), oil(1), battery(2), brake(3), left(4), right(5), highbeam(6), door(7)
// Byte 1: seatbelt(0), abs(1), traction(2), tpms(3), bits 4-7 reserved
std::atomic<unsigned short> telltale_state{0x0000};

struct TelltaleInfo {
    const char *name;
    int bit;
};

static TelltaleInfo telltale_bits[] = {
    {"engine",   0}, {"oil",      1}, {"battery",  2}, {"brake",    3},
    {"left",     4}, {"right",    5}, {"highbeam",  6}, {"door",     7},
    {"seatbelt", 8}, {"abs",      9}, {"traction", 10}, {"tpms",    11},
};
/*****************************************************************************/
// Signal handler for graceful shutdown (async-signal-safe: only sets atomic flag)
void handle_signal(int) {
    running = false;
}
/*****************************************************************************/
// Function to listen on a Linux socket
void socket_listener(bool bind_all, int port)
{
    int sockfd;
    struct sockaddr_in server_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit_failure = true; running = false;
        return;
    }
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = bind_all ? INADDR_ANY : htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Socket bind failed");
        close(sockfd);
        exit_failure = true; running = false;
        return;
    }

    if (listen(sockfd, 3) < 0) {
        perror("Socket listen failed");
        close(sockfd);
        exit_failure = true; running = false;
        return;
    }

    std::cout << "Socket listener started on port " << port << ".\n";
    while (running)
    {
        int new_socket;
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0 && errno != EINTR)
        {
            perror("Select error");
            exit_failure = true; running = false;
            break;
        }

        if (FD_ISSET(sockfd, &read_fds))
        {
            if ((new_socket = accept(sockfd, (struct sockaddr*)&client_addr, &addr_len)) < 0)
            {
                if (!running)
                    break; // signal-driven shutdown
                // Transient errors (e.g. ECONNABORTED): log and retry
                if (errno == ECONNABORTED || errno == EINTR)
                {
                    perror("Socket accept (transient, retrying)");
                    continue;
                }
                // Fatal accept error: shut down
                perror("Socket accept failed");
                exit_failure = true;
                running = false;
                break;
            }

            char buffer[1024] = {0};
            ssize_t nbytes = read(new_socket, buffer, sizeof(buffer) - 1);
            if (nbytes <= 0)
            {
                close(new_socket);
                continue;
            }
            std::string cmd,cmdArg;
            std::string buf (buffer);
            std::stringstream msgstream(buf);
            msgstream >> cmd;
            msgstream >> cmdArg;
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
            
            long val = 0;
            if (cmd == "pause")
            {
                sim_paused.store(true);
            }
            else if (cmd == "resume")
            {
                sim_paused.store(false);
            }
            else if (cmd == "telltale")
            {
                if (cmdArg.empty())
                {
                    // Read current telltale state as hex
                    snprintf(buffer, sizeof(buffer), "0x%04X\n", telltale_state.load());
                    write(new_socket, buffer, strlen(buffer));
                }
                else
                {
                    std::string action;
                    msgstream >> action;
                    std::transform(cmdArg.begin(), cmdArg.end(), cmdArg.begin(), ::tolower);
                    std::transform(action.begin(), action.end(), action.begin(), ::tolower);

                    if (cmdArg == "all" && action == "off")
                    {
                        telltale_state.store(0);
                    }
                    else
                    {
                        for (auto &t : telltale_bits)
                        {
                            if (cmdArg == t.name)
                            {
                                unsigned short mask = 1u << t.bit;
                                if (action == "on")
                                    telltale_state.fetch_or(mask);
                                else if (action == "off")
                                    telltale_state.fetch_and(~mask);
                                break;
                            }
                        }
                    }
                }
            }
            else if (cmd == "list")
            {
                std::string response;
                for (auto &p : obd_params)
                    response += std::string(p.name) + "=" + std::to_string(p.var->load()) + "\n";
                write(new_socket, response.c_str(), response.size());
            }
            else
            {
                for (auto &p : obd_params)
                {
                    if (cmd == p.name)
                    {
                        if (cmdArg.empty())
                        {
                            snprintf(buffer, sizeof(buffer), "%d\n", p.var->load());
                            write(new_socket, buffer, strlen(buffer));
                        }
                        else if (parse_int(cmdArg, val, p.min_val, p.max_val))
                            p.var->store(val);
                        break;
                    }
                }
            }
	    close(new_socket);
        }
    }
    close(sockfd);
    std::cout << "Socket listener stopped.\n";
}
/*****************************************************************************/
// Function to listen on a CAN bus
void canbus_listener(bool debugprint,std::string node)
{
    int sockfd;
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_frame frame;
    unsigned char req_field=0x00;
    if ((sockfd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0)
    {
        perror("CAN socket creation failed");
        exit_failure = true; running = false;
        return;
    }

    strncpy(ifr.ifr_name, node.c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(sockfd, SIOCGIFINDEX, &ifr) < 0)
    {
        perror("CAN interface not found");
        close(sockfd);
        exit_failure = true; running = false;
        return;
    }

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("CAN socket bind failed");
        close(sockfd);
        exit_failure = true; running = false;
        return;
    }

    std::cout << "CAN bus listener started on interface:"<<node<<std::endl;

    while (running)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0 && errno != EINTR)
        {
            perror("Select error");
            exit_failure = true; running = false;
            break;
        }

        if (FD_ISSET(sockfd, &read_fds))
        {
            int nbytes = read(sockfd, &frame, sizeof(struct can_frame));
            if (nbytes < 0)
            {
                if (running) {
                    perror("CAN read failed");
                    exit_failure = true;
                }
                running = false;
                break;
            }

            //print incoming request data 
            if(debugprint)
            {
                for (int i = 0; i < frame.can_dlc; i++)
                    printf("%02X ",frame.data[i]);
                printf("\n");
            }
            if(frame.can_id == 0x7DF || frame.can_id == 0x7E0)
            {
                // Validate minimum DLC for OBD request (length + mode + PID = 3 bytes)
                if (frame.can_dlc < 3)
                    continue;
                // Only respond to Mode 01 (current data) requests
                if (frame.data[1] != 0x01)
                    continue;
                req_field=frame.data[2];

                // Look up the current value for this PID
                int obd_value = 0;
                switch(req_field)
                {
                    case 0x04: obd_value = obd_load.load(); break;
                    case 0x05: obd_value = obd_temp.load(); break;
                    case 0x0B: obd_value = obd_intake.load(); break;
                    case 0x0C: obd_value = obd_rpm.load(); break;
                    case 0x0D: obd_value = obd_speed.load(); break;
                    case 0x10: obd_value = obd_flow.load(); break;
                    case 0x2F: obd_value = obd_fuel.load(); break;
                    case 0x42: obd_value = obd_battery.load(); break;
                    default: break; // supported PID queries don't need a value
                }

                OBDResponse resp = encode_obd_response(req_field, obd_value);
                frame.can_id = 0x7E8;
                frame.can_dlc = 8;
                std::memcpy(frame.data, resp.data, 8);
                if (resp.respond && write(sockfd, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame))
                    perror("Write");
                //print response 
                if(debugprint)
                {
                    for (int i = 0; i < frame.can_dlc; i++) 
                        printf("%02X ",frame.data[i]);
                    printf("\n"); 
                }
            }
        }
    }
    close(sockfd);
    std::cout << "CAN bus listener stopped.\n";
}
/*****************************************************************************/
// Telltale broadcaster: sends CAN ID 0x420 every 200ms
// Turn signals auto-blink at ~1.5Hz when enabled
void telltale_broadcaster(std::string node)
{
    int sockfd;
    struct sockaddr_can addr;
    struct ifreq ifr;

    if ((sockfd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0)
    {
        perror("Telltale CAN socket creation failed");
        exit_failure = true; running = false;
        return;
    }

    strncpy(ifr.ifr_name, node.c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(sockfd, SIOCGIFINDEX, &ifr) < 0)
    {
        perror("Telltale CAN interface not found");
        close(sockfd);
        exit_failure = true; running = false;
        return;
    }

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("Telltale CAN socket bind failed");
        close(sockfd);
        exit_failure = true; running = false;
        return;
    }

    std::cout << "Telltale broadcaster started on interface:" << node << std::endl;

    int blink_counter = 0;
    const int BLINK_TOGGLE_COUNT = 3; // toggle every 3×200ms = 600ms ≈ 1.67Hz

    while (running)
    {
        unsigned short state = telltale_state.load();

        // Auto-blink turn signals: toggle bits 4 (left) and 5 (right) at ~1.5Hz
        blink_counter++;
        bool blink_on = (blink_counter / BLINK_TOGGLE_COUNT) % 2 == 0;
        unsigned short wire_state;
        if (blink_on)
            wire_state = state; // show turn signals as-is
        else
            wire_state = state & ~0x0030u; // suppress turn signals during off phase

        struct can_frame frame;
        frame.can_id = 0x420;
        frame.can_dlc = 2;
        frame.data[0] = wire_state & 0xFF;
        frame.data[1] = (wire_state >> 8) & 0xFF;

        if (write(sockfd, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame))
            perror("Telltale write");

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    close(sockfd);
    std::cout << "Telltale broadcaster stopped.\n";
}
/*****************************************************************************/
// Drive simulation: cycles through a realistic drive profile
struct SimPhase {
    const char *name;
    int duration_ms;    // phase duration
    int speed_start, speed_end;
    int rpm_start, rpm_end;
    int temp_start, temp_end;
    int fuel_start, fuel_end;
    unsigned short telltale_on;   // telltales to set at phase start
    unsigned short telltale_off;  // telltales to clear at phase start
};

static SimPhase drive_profile[] = {
    {"cold start",   3000,   0,  0,  800, 800,  20, 40, 75,75, 0x0100,0x0000}, // seatbelt on
    {"warmup idle",  5000,   0,  0,  800, 800,  40, 70, 75,75, 0x0000,0x0100}, // seatbelt off
    {"accel 1-2",    3000,   0, 30,  800,3500,  70, 75, 75,74, 0x0010,0x0000}, // left turn on
    {"accel 2-3",    3000,  30, 60, 2000,3500,  75, 80, 74,73, 0x0000,0x0010}, // left turn off
    {"accel 3-4",    4000,  60,100, 2000,3500,  80, 85, 73,71, 0x0000,0x0000},
    {"cruise",      15000, 100,100, 2200,2200,  85, 90, 71,68, 0x0040,0x0000}, // highbeam on
    {"accel 4-5",    4000, 100,140, 2200,4000,  90, 90, 68,65, 0x0000,0x0040}, // highbeam off
    {"high cruise", 10000, 140,140, 3000,3000,  90, 90, 65,60, 0x0000,0x0000},
    {"decelerate",   5000, 140, 60, 3000,1500,  90, 88, 60,60, 0x0008,0x0000}, // brake on
    {"coast",        4000,  60, 30, 1500,1000,  88, 86, 60,60, 0x0020,0x0000}, // right turn on
    {"stop",         4000,  30,  0, 1000, 800,  86, 85, 60,60, 0x0000,0x0020}, // right turn off
    {"idle at stop", 5000,   0,  0,  800, 800,  85, 83, 60,60, 0x0000,0x0008}, // brake off
};

void drive_simulator(float speed_mult)
{
    std::cout << "Drive simulator started (speed=" << speed_mult << "x).\n";

    const int UPDATE_MS = 50;
    const int NUM_PHASES = sizeof(drive_profile) / sizeof(drive_profile[0]);

    while (running)
    {
        for (int phase = 0; phase < NUM_PHASES && running; phase++)
        {
            auto &p = drive_profile[phase];
            int duration = (int)(p.duration_ms / speed_mult);
            int steps = duration / UPDATE_MS;
            if (steps < 1) steps = 1;

            // Apply telltale changes at phase start
            if (p.telltale_on)
                telltale_state.fetch_or(p.telltale_on);
            if (p.telltale_off)
                telltale_state.fetch_and(~p.telltale_off);

            for (int step = 0; step < steps && running; step++)
            {
                if (sim_paused.load())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(UPDATE_MS));
                    step--; // don't advance while paused
                    continue;
                }

                float t = (float)step / steps;
                int spd = p.speed_start + (int)((p.speed_end - p.speed_start) * t);
                int rpm = p.rpm_start + (int)((p.rpm_end - p.rpm_start) * t);
                // Add slight RPM jitter for realism
                rpm += (step % 5 - 2) * 10; // ±20 RPM jitter
                if (rpm < 0) rpm = 0;
                int tmp = p.temp_start + (int)((p.temp_end - p.temp_start) * t);
                int fuel = p.fuel_start + (int)((p.fuel_end - p.fuel_start) * t);

                obd_speed.store(spd);
                obd_rpm.store(rpm);
                obd_temp.store(tmp);
                obd_fuel.store(fuel);

                std::this_thread::sleep_for(std::chrono::milliseconds(UPDATE_MS));
            }
        }
        // Reset fuel for next loop, temp carries over
        obd_fuel.store(75);
    }

    std::cout << "Drive simulator stopped.\n";
}
/*****************************************************************************/
void printHelp(std::string program)
{
        std::cout << "Usage: "<<program<<" [options]\n"
                << "Options:\n"
                << "  --node=<canx>       Specify the can0/can1 node(or --node <canx>)\n"
                << "  --debugprint=<flag> Specify the true/false debug print (or --debugprint <flag>)\n"
                << "  --port=<N>          TCP port for control interface (default: 8080)\n"
                << "  --bind-all          Bind TCP socket to all interfaces (default: localhost only)\n"
                << "  --simulate          Enable drive simulation mode\n"
                << "  --simulate-speed=<N> Simulation speed multiplier (default: 1.0)\n"
                << "  --help              Display this help message\n"
                << "\nConfig file search order:\n"
                << "  ./car-can-emulator.conf\n"
                << "  ./config/car-can-emulator.conf\n"
                << "  /etc/car-can-emulator.conf\n"
                << "Command-line arguments override config file values.\n";
}
/*****************************************************************************/
int main(int argc, char* argv[])
{
    std::string myname = argv[0];
    std::string node = "Unknown";
    std::string debugprint = "Unknown";
    bool debugflag=false;
    bool bind_all=false;
    bool simulate=false;
    float sim_speed=1.0f;
    int port=8080;

    // If --help is passed, print the help message
    if (argc == 2 && std::string(argv[1]) == "--help")
    {
        printHelp(myname);
        return 0;
    }

    // Load defaults from config file (command-line args override)
    for (const auto &path : {std::string("./car-can-emulator.conf"),
                             std::string("./config/car-can-emulator.conf"),
                             std::string("/etc/car-can-emulator.conf")})
    {
        auto cfg = read_config(path);
        if (!cfg.empty())
        {
            if (cfg.count("CAN_NODE") && node == "Unknown")
                node = cfg["CAN_NODE"];
            if (cfg.count("TCP_PORT"))
                port = safe_stoi(cfg["TCP_PORT"], 8080, 1, 65535);
            if (cfg.count("DEBUG_PRINT") && debugprint == "Unknown")
                debugprint = cfg["DEBUG_PRINT"];
            if (cfg.count("BIND_ALL")) {
                std::string v = cfg["BIND_ALL"];
                std::transform(v.begin(), v.end(), v.begin(), ::tolower);
                if (v == "true") bind_all = true;
            }
            if (cfg.count("SIMULATE")) {
                std::string v = cfg["SIMULATE"];
                std::transform(v.begin(), v.end(), v.begin(), ::tolower);
                if (v == "true") simulate = true;
            }
            if (cfg.count("SIMULATE_SPEED"))
                sim_speed = safe_stof(cfg["SIMULATE_SPEED"], 1.0f);
            break; // use first config file found
        }
    }

    // Iterate over command-line arguments
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        // Check for --node= format
        if (arg.rfind("--node=", 0) == 0)
            node = arg.substr(7);  // Extract the value after '='

        // Check for --debugprint= format
        else if (arg.rfind("--debugprint=", 0) == 0)
            debugprint = arg.substr(13);  // Extract the value after '='

        // Check for --node followed by value
        else if (arg == "--node" && i + 1 < argc)
            node = argv[++i];  // Get the next argument as the node

        // Check for --debugprint followed by value
        else if (arg == "--debugprint" && i + 1 < argc)
            debugprint = argv[++i];  // Get the next argument as the debugprint

        // Check for --port= format
        else if (arg.rfind("--port=", 0) == 0)
            port = safe_stoi(arg.substr(7), 8080, 1, 65535);

        // Check for --port followed by value
        else if (arg == "--port" && i + 1 < argc)
            port = safe_stoi(argv[++i], 8080, 1, 65535);

        // Check for --simulate flag
        else if (arg == "--simulate")
            simulate = true;

        // Check for --simulate-speed= format
        else if (arg.rfind("--simulate-speed=", 0) == 0)
            sim_speed = safe_stof(arg.substr(17), 1.0f);

        // Check for --bind-all flag
        else if (arg == "--bind-all")
            bind_all = true;
    }

    if (node == "Unknown")
    {
        std::cerr << "Error: --node argument is required (e.g. --node=can0)\n";
        printHelp(myname);
        return 1;
    }

    for(auto& c : debugprint)
        c = tolower(c);
    if(debugprint=="true")
        debugflag=true;

    // Set up signal handlers (SIGINT for interactive, SIGTERM for service managers)
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    // Create threads
    std::thread socket_thread(socket_listener, bind_all, port);
    std::thread canbus_thread(canbus_listener, debugflag, node);
    std::thread telltale_thread(telltale_broadcaster, node);
    std::thread sim_thread;
    if (simulate)
        sim_thread = std::thread(drive_simulator, sim_speed);

    // Wait for threads to complete
    socket_thread.join();
    canbus_thread.join();
    telltale_thread.join();
    if (sim_thread.joinable())
        sim_thread.join();

    std::cout << "All threads have exited. Program terminated.\n";
    return exit_failure.load() ? 1 : 0;
}
/*****************************************************************************/
