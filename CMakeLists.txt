cmake_minimum_required(VERSION 3.10)
project(car-can-emulator)

set(CMAKE_CXX_STANDARD 11)

add_executable(${PROJECT_NAME} src/car-can-emulator.cpp)
target_link_libraries(${PROJECT_NAME} pthread)
install(TARGETS ${PROJECT_NAME} DESTINATION usr/sbin)
install(PROGRAMS deploy/openwrt/car-can-emulator.init DESTINATION etc/init.d)

#for cross compiling
#cmake -H. -BOutput -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain-arm-backup.cmake -DCMAKE_INSTALL_PREFIX=/home/user/tmp/
#cmake --build Output -- all
#cmake --build Output -- install

#for local compiling
#cmake -H. -BOutput
#cmake --build Output -- all

#for cleaning make generated output
#rm -rf Output
