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
    src/core/IProcessProvider.h
    src/core/ProcessProviderFactory.cpp
)

if(WIN32)
    list(APPEND CORE_SOURCES
        src/core/windows/ProcessProviderWin.h
        src/core/windows/ProcessProviderWin.cpp
    )
elseif(UNIX)
    list(APPEND CORE_SOURCES
        src/core/linux/ProcessProviderLinux.h
        src/core/linux/ProcessProviderLinux.cpp
    )
endif()

set(GUI_SOURCES
    src/gui/MainWindow.h
    src/gui/MainWindow.cpp
    src/gui/ProcessTableModel.h
    src/gui/ProcessTableModel.cpp
)

add_executable(alternative_hacker
    src/main.cpp
    ${CORE_SOURCES}
    ${GUI_SOURCES}
)

target_include_directories(alternative_hacker PRIVATE src)
target_link_libraries(alternative_hacker PRIVATE Qt6::Widgets)

if(WIN32)
    target_link_libraries(alternative_hacker PRIVATE ntdll advapi32 psapi)
    target_compile_definitions(alternative_hacker PRIVATE UNICODE _UNICODE)
endif()
