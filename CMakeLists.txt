cmake_minimum_required(VERSION 3.16)

project(deepcool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_BUILD_TYPE Release)

# Export compile commands for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Option to build GUI or CLI only
option(BUILD_GUI "Build the GUI application (requires Qt Widgets)" OFF)
option(BUILD_CLI "Build the CLI application (Qt Core only)" ON)

# Find Qt6 Core (always required)
find_package(Qt6 REQUIRED COMPONENTS Core)

# Find Qt6 Widgets only if building GUI
if(BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
endif()

# Linux specific: find udev and libusb libraries
if(UNIX AND NOT APPLE)
    find_library(UDEV_LIBRARY udev)
    if(UDEV_LIBRARY)
        message(STATUS "Found udev library: ${UDEV_LIBRARY}")
    else()
        message(WARNING "udev library not found - device detection may not work properly")
    endif()

    # Find libusb-1.0 (required for vendor-specific USB devices)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
    message(STATUS "Found libusb-1.0: ${LIBUSB_LIBRARIES}")
endif()

# Common device source files
set(DEVICE_SOURCES
    device.h
    device.cpp
    deepcooldevice.cpp
    deepcooldevice.h
)

# ============================================
# CLI Application (headless, Qt Core only)
# ============================================
if(BUILD_CLI)
    set(CLI_SOURCES
        main_cli.cpp
        ${DEVICE_SOURCES}
    )

    add_executable(deepcool-cli ${CLI_SOURCES})

    target_link_libraries(deepcool-cli
        Qt6::Core
    )

    if(UNIX AND NOT APPLE)
        if(UDEV_LIBRARY)
            target_link_libraries(deepcool-cli ${UDEV_LIBRARY})
        endif()
        target_include_directories(deepcool-cli PRIVATE ${LIBUSB_INCLUDE_DIRS})
        target_link_libraries(deepcool-cli ${LIBUSB_LIBRARIES})
    endif()

    set_target_properties(deepcool-cli PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    install(TARGETS deepcool-cli
        RUNTIME DESTINATION bin
    )

    message(STATUS "CLI application: ENABLED")
endif()

# ============================================
# GUI Application (requires display)
# ============================================
if(BUILD_GUI)
    set(GUI_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        ${DEVICE_SOURCES}
    )

    add_executable(deepcool-gui ${GUI_SOURCES})

    target_link_libraries(deepcool-gui
        Qt6::Core
        Qt6::Widgets
    )

    if(UNIX AND NOT APPLE)
        if(UDEV_LIBRARY)
            target_link_libraries(deepcool-gui ${UDEV_LIBRARY})
        endif()
        target_include_directories(deepcool-gui PRIVATE ${LIBUSB_INCLUDE_DIRS})
        target_link_libraries(deepcool-gui ${LIBUSB_LIBRARIES})
    endif()

    set_target_properties(deepcool-gui PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    install(TARGETS deepcool-gui
        RUNTIME DESTINATION bin
    )

    message(STATUS "GUI application: ENABLED")
else()
    message(STATUS "GUI application: DISABLED (use -DBUILD_GUI=ON to enable)")
endif()
