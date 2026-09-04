// Telltale bitmask on 0x420 every 100 ms, little-endian 32-bit, the way a
// body controller would broadcast it. Bit order follows the car-can-proxy
// contract (bits 0-11 legacy lamps, 12-19 EV/hybrid lamps).
#include "State.h"
#include "Threads.h"

#include <cstring>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>

void telltale_broadcaster(std::string node)
{
    int sockfd;
    struct sockaddr_can addr;
    struct ifreq ifr;
    if ((sockfd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) { perror("CAN socket creation failed (telltales)"); return; }
    strcpy(ifr.ifr_name, node.c_str());
    ioctl(sockfd, SIOCGIFINDEX, &ifr);
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("CAN socket bind failed (telltales)"); close(sockfd); return; }
    while (g_running)
    {
        struct can_frame frame;
        memset(&frame, 0, sizeof(frame));
        uint32_t tt;
        {
            std::lock_guard<std::mutex> lock(g_state.mutex);
            tt = g_state.telltales;
        }
        frame.can_id = 0x420;
        frame.can_dlc = 8;
        frame.data[0] = tt & 0xFF;
        frame.data[1] = (tt >> 8) & 0xFF;
        frame.data[2] = (tt >> 16) & 0xFF;
        frame.data[3] = (tt >> 24) & 0xFF;
        if (write(sockfd, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame))
            perror("Write (telltales)");
        usleep(100000);
    }
    close(sockfd);
}
