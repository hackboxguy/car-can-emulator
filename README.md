# car-can-emulator
CAN bus emulator for testing OBD2 devices

# how to build and test
1. Plugin Canable usb-to-can dongle to your linux pc
2. ```sudo ip link set can0 type can bitrate 500000```
3. ```sudo ifconfig can0 up```
4. ```cmake -H. -BOutput```
5. ```cmake --build Output -- all```
6. ```./Output/car-can-emulator --node=can0 --debugprint=true```
7. From a second terminal, run ``` echo -n speed | nc 127.0.0.1 8080``` to read the current speed
8. From a second terminal, run ``` echo -n "speed 90" | nc 127.0.0.1 8080``` to set the current speed

