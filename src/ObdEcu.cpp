// OBD-II (J1979) ECU: answers mode-01 functional requests on 0x7DF from
// 0x7E8, for exactly the PIDs the current car type serves.
#include "State.h"
#include "Threads.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

void canbus_listener(bool debugprint, std::string node)
{
    int sockfd;
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_frame frame;
    if ((sockfd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0)
    {
        perror("CAN socket creation failed");
        return;
    }
    strcpy(ifr.ifr_name, node.c_str());
    ioctl(sockfd, SIOCGIFINDEX, &ifr);
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("CAN socket bind failed");
        close(sockfd);
        return;
    }
    std::cout << "CAN bus listener started on interface:" << node << std::endl;

    while (g_running)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);
        struct timeval timeout = { 1, 0 };
        int activity = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0 && errno != EINTR)
        {
            perror("Select error");
            break;
        }
        if (!FD_ISSET(sockfd, &read_fds))
            continue;

        int nbytes = read(sockfd, &frame, sizeof(struct can_frame));
        if (nbytes < 0)
        {
            if (g_running)
                perror("CAN read failed");
            break;
        }
        if (debugprint)
        {
            printf("%03X ", frame.can_id & CAN_SFF_MASK);
            for (int i = 0; i < frame.can_dlc; i++)
                printf("%02X ", frame.data[i]);
            printf("\n");
        }
        if (frame.can_id != 0x7DF || frame.can_dlc < 3 || frame.data[1] != 0x01)
            continue;

        const unsigned char pid = frame.data[2];
        bool answer = true;
        frame.can_id = 0x7E8;
        frame.can_dlc = 8;
        memset(frame.data, 0, 8);
        frame.data[1] = 0x41;
        frame.data[2] = pid;

        std::lock_guard<std::mutex> lock(g_state.mutex);
        const EmuState &s = g_state;
        if ((pid & 0x1F) == 0)   // supported-PID bitmap
        {
            const uint32_t bitmap = s.supportedBitmap(pid);
            frame.data[0] = 0x06;
            frame.data[3] = (bitmap >> 24) & 0xFF; frame.data[4] = (bitmap >> 16) & 0xFF;
            frame.data[5] = (bitmap >> 8) & 0xFF;  frame.data[6] = bitmap & 0xFF;
            // A bitmap request past the last served block gets no answer,
            // the same as a real ECU; the chain bit already said so.
            if (bitmap == 0)
                answer = false;
        }
        else if (!s.servedPids.count(pid))
        {
            answer = false;   // a real ECU does not answer a PID it lacks
        }
        else switch (pid)
        {
            case 0x04: frame.data[0]=0x03; frame.data[3]=s.load; break;
            case 0x05: frame.data[0]=0x03; frame.data[3]=s.temp & 0xFF; frame.data[4]=(s.temp >> 8); break;
            case 0x0B: frame.data[0]=0x03; frame.data[3]=s.intake; break;
            case 0x0C: frame.data[0]=0x04; frame.data[3]=s.rpm & 0xFF; frame.data[4]=(s.rpm >> 8); break;
            case 0x0D: frame.data[0]=0x03; frame.data[3]=s.speed & 0xFF; frame.data[4]=(s.speed >> 8); break;
            case 0x10: frame.data[0]=0x04; frame.data[3]=(s.flow >> 8); frame.data[4]=s.flow & 0xFF; break;
            case 0x2F: frame.data[0]=0x03; frame.data[3]=s.fuel; break;
            case 0x42: frame.data[0]=0x04; frame.data[3]=(s.volt >> 8); frame.data[4]=s.volt & 0xFF; break;
            case 0x46: frame.data[0]=0x03; frame.data[3]=s.ambient; break;
            case 0x5B: frame.data[0]=0x03; frame.data[3]=(unsigned char)(s.soc * 255.0 / 100.0 + 0.5); break;
            case 0xA6: frame.data[0]=0x06; frame.data[3]=(s.odo >> 24) & 0xFF; frame.data[4]=(s.odo >> 16) & 0xFF;
                       frame.data[5]=(s.odo >> 8) & 0xFF; frame.data[6]=s.odo & 0xFF; break;
            default:   answer = false; break;
        }
        if (!answer)
            continue;
        if (write(sockfd, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame))
            perror("Write");
        if (debugprint)
        {
            printf("  -> 7E8 ");
            for (int i = 0; i < frame.can_dlc; i++)
                printf("%02X ", frame.data[i]);
            printf("\n");
        }
    }
    close(sockfd);
    std::cout << "CAN bus listener stopped.\n";
}
