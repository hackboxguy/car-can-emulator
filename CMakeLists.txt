cmake_minimum_required(VERSION 3.10)
project(car-can-emulator)

set(CMAKE_CXX_STANDARD 17)
include(GNUInstallDirs)

# Platform option: systemd, openwrt, or local (default)
set(PLATFORM "local" CACHE STRING "Target platform: systemd, openwrt, or local")

add_executable(${PROJECT_NAME} src/car-can-emulator.cpp)
target_link_libraries(${PROJECT_NAME} pthread)

# Tests (no external dependencies, host-only)
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_executable(test-car-can-emulator tests/test-car-can-emulator.cpp)
    set_target_properties(test-car-can-emulator PROPERTIES CXX_STANDARD 17)
    add_test(NAME unit-tests COMMAND test-car-can-emulator)
endif()

install(FILES config/car-can-emulator.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR})

if(PLATFORM STREQUAL "systemd")
    install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_SBINDIR})
    install(FILES deploy/systemd/car-can-emulator.service DESTINATION lib/systemd/system)
elseif(PLATFORM STREQUAL "openwrt")
    install(TARGETS ${PROJECT_NAME} DESTINATION usr/sbin)
    install(PROGRAMS deploy/openwrt/car-can-emulator.init DESTINATION etc/init.d)
else()
    install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# Build instructions:
#   Local:     cmake -H. -BOutput && cmake --build Output
#   systemd:   cmake -H. -BOutput -DPLATFORM=systemd && cmake --build Output
#   OpenWrt:   cmake -H. -BOutput -DPLATFORM=openwrt -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake && cmake --build Output
#   Install:   cmake --build Output -- install
#   Clean:     rm -rf Output
