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
#include <fstream>
#include <algorithm>

using namespace std;
// Global running flag
std::atomic<bool> running(true);

unsigned short obd_speed=0x0058,obd_temp=35,obd_rpm=12,obd_flow=0x0540;//default-flow 5.4l/100km
unsigned char obd_intake=0,obd_load=0;
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
            if(frame.can_id == 0x7DF )
            {
                req_field=frame.data[2];
                frame.can_id=0x7E8;
                frame.can_dlc=8;
                frame.data[0]=0x06;
                frame.data[1]=0x41;
                frame.data[2]=req_field;
                switch(req_field)
                {
                    case 0x04:frame.data[0]=0x03;frame.data[3]=obd_load;frame.data[4]=0x00;frame.data[5]=0x00;frame.data[6]=0x00;frame.data[7]=0x00;break;//load
                    case 0x0B:frame.data[0]=0x03;frame.data[3]=obd_intake;frame.data[4]=0x00;frame.data[5]=0x00;frame.data[6]=0x00;frame.data[7]=0x00;break;//intake
                    case 0x10:frame.data[0]=0x04;frame.data[3]=(obd_flow>>8);frame.data[4]=obd_flow&0x00FF;frame.data[5]=0x00;frame.data[6]=0x00;frame.data[7]=0x00;break;//air-flow rate
                    case 0x05:frame.data[0]=0x03;frame.data[3]=obd_temp&0x00FF;frame.data[4]=(obd_temp>>8);frame.data[5]=0x00;frame.data[6]=0x00;frame.data[7]=0x00;break;//engine coolant temp
                    case 0x0D:frame.data[0]=0x03;frame.data[3]=obd_speed&0x00FF;frame.data[4]=(obd_speed>>8);frame.data[5]=0x00;frame.data[6]=0x00;frame.data[7]=0x00;break;//vehicle speed
                    case 0x0C:frame.data[0]=0x04;frame.data[3]=obd_rpm&0x00FF;frame.data[4]=(obd_rpm>>8);frame.data[5]=0x00;frame.data[6]=0x00;frame.data[7]=0x00;break;//engine rpm
                    case 0x40:frame.data[0]=0x06;frame.data[3]=0xFF;frame.data[4]=0xFF;frame.data[5]=0xFF;frame.data[6]=0xFE;frame.data[7]=0x00;break;//supported pid's
                    default  :frame.data[0]=0x06;frame.data[3]=0xFF;frame.data[4]=0xFF;frame.data[5]=0xFF;frame.data[6]=0xFF;frame.data[7]=0xFF;break;
                }
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

    // Wait for threads to complete
    socket_thread.join();
    canbus_thread.join();

    std::cout << "All threads have exited. Program terminated.\n";
    return 0;
}
/*****************************************************************************/
