cmake_minimum_required(VERSION 3.10)
set(COMPONENT_NAME car-can-emulator)
#set(COMPONENT_STARTUP_SCRIPT S59TouchBlinkServer)
project(${COMPONENT_NAME})

set(CMAKE_CXX_STANDARD 11)

#find_package(PkgConfig REQUIRED)
#pkg_check_modules(LIBEVDEV REQUIRED libevdev)

#include_directories(${LIBEVDEV_INCLUDE_DIRS})
#link_directories(${LIBEVDEV_LIBRARY_DIRS})

add_executable(${PROJECT_NAME} car-can-emulator.cpp)
target_link_libraries(${PROJECT_NAME} pthread) #${LIBEVDEV_LIBRARIES})
install(TARGETS ${COMPONENT_NAME} DESTINATION usr/sbin)
#install(PROGRAMS ${COMPONENT_STARTUP_SCRIPT} DESTINATION etc/init.d) #copy server's startup script to init.d

#for cross compiling
#cmake -H. -BOutput -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain-arm-backup.cmake -DCMAKE_INSTALL_PREFIX=/home/user/tmp/
#cmake --build Output -- all
#cmake --build Output -- install

#for local compiling
#cmake -H. -BOutput
#cmake --build Output -- all

#for cleaning make generated output
#rm -rf Output
