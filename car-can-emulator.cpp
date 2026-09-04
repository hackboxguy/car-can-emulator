//can emulated server with ability to change params via netcat command
//echo -n "temp" | nc 127.0.0.1 8080 (reads current engine temperature)
//echo -n "temp 40" | nc 127.0.0.1 8080
//echo -n "flow 1600" | nc 127.0.0.1 8080
//echo -n "speed 120" | nc 127.0.0.1 8080
//echo -n "rpm 4" | nc 127.0.0.1 8080
//echo -n "fuel 75" | nc 127.0.0.1 8080      (percent)
//echo -n "volt 12.6" | nc 127.0.0.1 8080    (control module voltage, V)
//echo -n "ambient 23" | nc 127.0.0.1 8080   (ambient air, degrees C)
//echo -n "odo 10568.7" | nc 127.0.0.1 8080  (odometer, km)
//echo -n "tt 0x110" | nc 127.0.0.1 8080     (telltale bitmask broadcast on 0x420)
//
// The emulator answers only the PIDs it actually serves and advertises exactly
// those in the supported-PID bitmaps (0x00, 0x20, ...), like a real ECU.

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
#include <fstream>
#include <algorithm>

using namespace std;
// Global running flag
std::atomic<bool> running(true);

unsigned short obd_speed=0x0058,obd_temp=35,obd_rpm=12,obd_flow=0x0540;//default-flow 5.4l/100km
unsigned char obd_intake=0,obd_load=0;
unsigned char obd_fuel=191;          //PID 0x2F raw A, 191 = 75 %
unsigned short obd_volt=12600;       //PID 0x42 in mV
unsigned char obd_ambient=63;        //PID 0x46 raw A, 63 = 23 degC
unsigned int obd_odo=105687;         //PID 0xA6 in 0.1 km, 10568.7 km
std::atomic<unsigned int> telltales(0);   //broadcast on 0x420, 100 ms, little-endian

// Supported-PID bitmaps: bit 7 of byte A is PID base+1 ... bit 0 of byte D is
// PID base+0x20, which also says "next block exists".
static const unsigned int supported_00 = (1u<<28)|(1u<<27)|(1u<<21)|(1u<<20)|(1u<<19)|(1u<<16)|1u; // 04 05 0B 0C 0D 10, +0x20
static const unsigned int supported_20 = (1u<<17)|1u;                       // 2F, +0x40
static const unsigned int supported_40 = (1u<<30)|(1u<<26)|1u;              // 42 46, +0x60
static const unsigned int supported_60 = 1u;                                // +0x80
static const unsigned int supported_80 = 1u;                                // +0xA0
static const unsigned int supported_A0 = (1u<<26);                          // A6
static const unsigned int supported_C0 = 0u;
/*****************************************************************************/
// Signal handler to handle SIGINT (Ctrl+C) for graceful shutdown
void handle_signal(int signal) {
    if (signal == SIGINT) {
        std::cout << "\nSIGINT received. Shutting down gracefully...\n";
        running = false;
    }
}
/*****************************************************************************/
// Function to listen on a Linux socket
void socket_listener() 
{
    int sockfd;
    struct sockaddr_in server_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        return;
    }
    int reuse = 1;   //so a restart is not refused by the previous run's TIME_WAIT sockets
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Socket bind failed");
        close(sockfd);
        return;
    }

    if (listen(sockfd, 3) < 0) {
        perror("Socket listen failed");
        close(sockfd);
        return;
    }

    std::cout << "Socket listener started on port 8080.\n";
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
            break;
        }

        if (FD_ISSET(sockfd, &read_fds)) 
        {
            if ((new_socket = accept(sockfd, (struct sockaddr*)&client_addr, &addr_len)) < 0) 
            {
                if (running) 
                    perror("Socket accept failed");
                break;
            }

            //std::cout << "Accepted connection on socket.\n";
            // Handle the client connection (simplified for demonstration)
            char buffer[1024] = {0};
            read(new_socket, buffer, 1024);
            //std::cout << "Received: " << buffer << "\n";
            std::string cmd,cmdArg;
            std::string buf (buffer);
            stringstream msgstream(buf);
            msgstream >> cmd;
            msgstream >> cmdArg;
            transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
            
            //speed/rpm/temp/flow
            if(cmd == "speed")
            {
                if(cmdArg.length()<=0)
                {
                    sprintf(buffer,"%d\n",obd_speed);
                    write(new_socket,buffer,strlen(buffer));
                }
                else
                    obd_speed=atoi(cmdArg.c_str());	
            }
            else if(cmd == "rpm")
            {    
                if(cmdArg.length()<=0)
                {
                    sprintf(buffer,"%d\n",obd_rpm);
                    write(new_socket,buffer,strlen(buffer));
                }
                else 
                    obd_rpm=atoi(cmdArg.c_str());	
            }
            else if(cmd == "temp")
            {
                if(cmdArg.length()<=0)
                {
                    sprintf(buffer,"%d\n",obd_temp);
                    write(new_socket,buffer,strlen(buffer));
                }
                else 
                    obd_temp=atoi(cmdArg.c_str());	
            }
            else if(cmd == "flow")
            {
                if(cmdArg.length()<=0)
                {
                    sprintf(buffer,"%d\n",obd_flow);
                    write(new_socket,buffer,strlen(buffer));
                }
                else 
                    obd_flow=atoi(cmdArg.c_str());	
            }
            else if(cmd == "intake")
            {
                if(cmdArg.length()<=0)
                {
                    sprintf(buffer,"%d\n",obd_intake);
                    write(new_socket,buffer,strlen(buffer));
                }
                else
                    obd_intake=atoi(cmdArg.c_str());
            }
            else if(cmd == "load")
            {
                if(cmdArg.length()<=0)
                {
                    sprintf(buffer,"%d\n",obd_load);
                    write(new_socket,buffer,strlen(buffer));
                }
                else
                    obd_load=atoi(cmdArg.c_str());
            }
            else if(cmd == "fuel")   //percent
            {
                if(cmdArg.length()<=0)
                {
                    sprintf(buffer,"%d\n",obd_fuel*100/255);
                    write(new_socket,buffer,strlen(buffer));
                }
                else
                {
                    int pct=atoi(cmdArg.c_str()); if(pct<0)pct=0; if(pct>100)pct=100;
                    obd_fuel=(unsigned char)(pct*255/100);
                }
            }
            else if(cmd == "volt")   //volts
            {
                if(cmdArg.length()<=0)
                {
                    sprintf(buffer,"%.3f\n",obd_volt/1000.0);
                    write(new_socket,buffer,strlen(buffer));
                }
                else
                    obd_volt=(unsigned short)(atof(cmdArg.c_str())*1000.0+0.5);
            }
            else if(cmd == "ambient")   //degrees C
            {
                if(cmdArg.length()<=0)
                {
                    sprintf(buffer,"%d\n",(int)obd_ambient-40);
                    write(new_socket,buffer,strlen(buffer));
                }
                else
                {
                    int c=atoi(cmdArg.c_str()); if(c<-40)c=-40; if(c>215)c=215;
                    obd_ambient=(unsigned char)(c+40);
                }
            }
            else if(cmd == "odo")   //km
            {
                if(cmdArg.length()<=0)
                {
                    sprintf(buffer,"%.1f\n",obd_odo/10.0);
                    write(new_socket,buffer,strlen(buffer));
                }
                else
                    obd_odo=(unsigned int)(atof(cmdArg.c_str())*10.0+0.5);
            }
            else if(cmd == "tt")   //telltale bitmask, hex or decimal
            {
                if(cmdArg.length()<=0)
                {
                    sprintf(buffer,"0x%08X\n",telltales.load());
                    write(new_socket,buffer,strlen(buffer));
                }
                else
                    telltales=(unsigned int)strtoul(cmdArg.c_str(),NULL,0);
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
        return;
    }

    //strcpy(ifr.ifr_name, "can0");//TODO: make canbus configurable via cmdline arg
    strcpy(ifr.ifr_name, node.c_str());//TODO: make canbus configurable via cmdline arg
    ioctl(sockfd, SIOCGIFINDEX, &ifr);

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) 
    {
        perror("CAN socket bind failed");
        close(sockfd);
        return;
    }

    std::cout << "CAN bus listener started on interface:"<<node<<endl;

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
            break;
        }

        if (FD_ISSET(sockfd, &read_fds)) 
        {
            int nbytes = read(sockfd, &frame, sizeof(struct can_frame));
            if (nbytes < 0) 
            {
                if (running) 
                    perror("CAN read failed");
                break;
            }

            //print incoming request data 
            if(debugprint)
            {
                for (int i = 0; i < frame.can_dlc; i++)
                    printf("%02X ",frame.data[i]);
                printf("\n");
            }
            if(frame.can_id == 0x7DF && frame.can_dlc>=3 && frame.data[1]==0x01)
            {
                bool answer=true;
                req_field=frame.data[2];
                frame.can_id=0x7E8;
                frame.can_dlc=8;
                frame.data[0]=0x06;
                frame.data[1]=0x41;
                frame.data[2]=req_field;
                unsigned int bitmap=0;
                switch(req_field)
                {
                    case 0x04:frame.data[0]=0x03;frame.data[3]=obd_load;frame.data[4]=0x00;frame.data[5]=0x00;frame.data[6]=0x00;frame.data[7]=0x00;break;//load
                    case 0x0B:frame.data[0]=0x03;frame.data[3]=obd_intake;frame.data[4]=0x00;frame.data[5]=0x00;frame.data[6]=0x00;frame.data[7]=0x00;break;//intake
                    case 0x10:frame.data[0]=0x04;frame.data[3]=(obd_flow>>8);frame.data[4]=obd_flow&0x00FF;frame.data[5]=0x00;frame.data[6]=0x00;frame.data[7]=0x00;break;//air-flow rate
                    case 0x05:frame.data[0]=0x03;frame.data[3]=obd_temp&0x00FF;frame.data[4]=(obd_temp>>8);frame.data[5]=0x00;frame.data[6]=0x00;frame.data[7]=0x00;break;//engine coolant temp
                    case 0x0D:frame.data[0]=0x03;frame.data[3]=obd_speed&0x00FF;frame.data[4]=(obd_speed>>8);frame.data[5]=0x00;frame.data[6]=0x00;frame.data[7]=0x00;break;//vehicle speed
                    case 0x0C:frame.data[0]=0x04;frame.data[3]=obd_rpm&0x00FF;frame.data[4]=(obd_rpm>>8);frame.data[5]=0x00;frame.data[6]=0x00;frame.data[7]=0x00;break;//engine rpm
                    case 0x2F:frame.data[0]=0x03;frame.data[3]=obd_fuel;frame.data[4]=0x00;frame.data[5]=0x00;frame.data[6]=0x00;frame.data[7]=0x00;break;//fuel level
                    case 0x42:frame.data[0]=0x04;frame.data[3]=(obd_volt>>8);frame.data[4]=obd_volt&0x00FF;frame.data[5]=0x00;frame.data[6]=0x00;frame.data[7]=0x00;break;//control module voltage
                    case 0x46:frame.data[0]=0x03;frame.data[3]=obd_ambient;frame.data[4]=0x00;frame.data[5]=0x00;frame.data[6]=0x00;frame.data[7]=0x00;break;//ambient air temp
                    case 0xA6:frame.data[0]=0x06;frame.data[3]=(obd_odo>>24)&0xFF;frame.data[4]=(obd_odo>>16)&0xFF;frame.data[5]=(obd_odo>>8)&0xFF;frame.data[6]=obd_odo&0xFF;frame.data[7]=0x00;break;//odometer
                    case 0x00:bitmap=supported_00;break;
                    case 0x20:bitmap=supported_20;break;
                    case 0x40:bitmap=supported_40;break;
                    case 0x60:bitmap=supported_60;break;
                    case 0x80:bitmap=supported_80;break;
                    case 0xA0:bitmap=supported_A0;break;
                    case 0xC0:bitmap=supported_C0;break;
                    default  :answer=false;break;   //a real ECU does not answer a PID it lacks
                }
                if((req_field&0x1F)==0)   //supported-PID bitmap reply
                {
                    frame.data[0]=0x06;
                    frame.data[3]=(bitmap>>24)&0xFF;frame.data[4]=(bitmap>>16)&0xFF;
                    frame.data[5]=(bitmap>>8)&0xFF;frame.data[6]=bitmap&0xFF;frame.data[7]=0x00;
                }
                if(!answer)
                    continue;
                if (write(sockfd, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame)) 
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
// Broadcast the telltale bitmask on 0x420 every 100 ms, little-endian, the way
// a body controller would. Bits 0-11 match the cluster's lamp order; see the
// car-can-proxy contract for the full 32-bit assignment.
void telltale_broadcaster(std::string node)
{
    int sockfd;
    struct sockaddr_can addr;
    struct ifreq ifr;
    if ((sockfd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0)
    {
        perror("CAN socket creation failed (telltales)");
        return;
    }
    strcpy(ifr.ifr_name, node.c_str());
    ioctl(sockfd, SIOCGIFINDEX, &ifr);
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("CAN socket bind failed (telltales)");
        close(sockfd);
        return;
    }
    while (running)
    {
        struct can_frame frame;
        memset(&frame, 0, sizeof(frame));
        unsigned int tt = telltales.load();
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
/*****************************************************************************/
void printHelp(std::string program)
{
        std::cout << "Usage: "<<program<<" [options]\n"
                << "Options:\n"
                << "  --node=<canx>       Specify the can0/can1 node(or --node <canx>)\n"
                << "  --debugprint=<flag> Specify the true/false debug print (or --debugprint <flag>)\n"
                << "  --help              Display this help message\n";
}
/*****************************************************************************/
int main(int argc, char* argv[])
{
    std::string myname = "car-simulator";
    std::string node = "Unknown";
    std::string debugprint = "Unknown";
    bool debugflag=false;

    // If no arguments or --help is passed, print the help message
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help"))
    {
        printHelp(myname);
        return 0;
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
    }

    for(auto& c : debugprint)
        c = tolower(c);
    if(debugprint=="true")
        debugflag=true;
    
    // Set up the signal handler
    std::signal(SIGINT, handle_signal);

    // Create threads
    std::thread socket_thread(socket_listener);
    std::thread canbus_thread(canbus_listener,debugflag,node);
    std::thread telltale_thread(telltale_broadcaster,node);

    // Wait for threads to complete
    socket_thread.join();
    canbus_thread.join();
    telltale_thread.join();

    std::cout << "All threads have exited. Program terminated.\n";
    return 0;
}
/*****************************************************************************/
