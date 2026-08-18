cmake_minimum_required(VERSION 3.21)
project(AlternativeHacker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(CORE_SOURCES
    src/core/ProcessInfo.h
    src/core/ProcessDetails.h
    src/core/IProcessProvider.h
    src/core/ProcessProviderFactory.cpp
    src/core/SystemStats.h
    src/core/ISystemMonitor.h
    src/core/SystemMonitorFactory.cpp
    src/core/ServiceInfo.h
    src/core/IServiceManager.h
    src/core/ServiceManagerFactory.cpp
)

if(WIN32)
    list(APPEND CORE_SOURCES
        src/core/windows/ProcessProviderWin.h
        src/core/windows/ProcessProviderWin.cpp
        src/core/windows/SystemMonitorWin.h
        src/core/windows/SystemMonitorWin.cpp
        src/core/windows/ServiceManagerWin.h
        src/core/windows/ServiceManagerWin.cpp
    )
elseif(UNIX)
    list(APPEND CORE_SOURCES
        src/core/linux/ProcessProviderLinux.h
        src/core/linux/ProcessProviderLinux.cpp
        src/core/linux/SystemMonitorLinux.h
        src/core/linux/SystemMonitorLinux.cpp
        src/core/linux/ServiceManagerLinux.h
        src/core/linux/ServiceManagerLinux.cpp
    )
endif()

set(GUI_SOURCES
    src/gui/MainWindow.h
    src/gui/MainWindow.cpp
    src/gui/ProcessTreeModel.h
    src/gui/ProcessTreeModel.cpp
    src/gui/DetailTableModels.h
    src/gui/DetailTableModels.cpp
    src/gui/ProcessDetailsDialog.h
    src/gui/ProcessDetailsDialog.cpp
    src/gui/SparklineWidget.h
    src/gui/SparklineWidget.cpp
    src/gui/PerformanceView.h
    src/gui/PerformanceView.cpp
    src/gui/ServicesTableModel.h
    src/gui/ServicesTableModel.cpp
    src/gui/ServicesView.h
    src/gui/ServicesView.cpp
)

add_executable(alternative_hacker
    src/main.cpp
    ${CORE_SOURCES}
    ${GUI_SOURCES}
)

target_include_directories(alternative_hacker PRIVATE src)
target_link_libraries(alternative_hacker PRIVATE Qt6::Widgets)

if(WIN32)
    target_link_libraries(alternative_hacker PRIVATE ntdll advapi32 psapi iphlpapi ws2_32 pdh)
    target_compile_definitions(alternative_hacker PRIVATE UNICODE _UNICODE)
endif()

# diskutil: standalone CLI for raw MBR/GPT/disk backup-restore. Kept free
# of the Qt dependency so it builds (and can be shipped/audited) on its
# own - see README's disk module warning before running it against a real
# device.
add_executable(diskutil
    src/diskutil/main.cpp
    src/diskutil/BlockDevice.h
    src/diskutil/BlockDevice.cpp
    src/diskutil/Partitioning.h
    src/diskutil/Partitioning.cpp
    src/diskutil/Sha256.h
    src/diskutil/Sha256.cpp
)
target_include_directories(diskutil PRIVATE src)

if(WIN32)
    target_compile_definitions(diskutil PRIVATE UNICODE _UNICODE)
endif()
