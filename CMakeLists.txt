cmake_minimum_required(VERSION 3.21)
project(AlternativeHacker VERSION 0.1.0 LANGUAGES CXX)

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

    # Bundle the Qt runtime (DLLs + platform/style plugins) next to the exe
    # so the NSIS installer built below has something self-contained to
    # package - a bare alternative_hacker.exe won't run on a machine
    # without a matching system Qt6 install.
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt)
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET alternative_hacker POST_BUILD
            COMMAND ${WINDEPLOYQT_EXECUTABLE} --no-compiler-runtime --no-translations
                    "$<TARGET_FILE:alternative_hacker>"
            COMMENT "Deploying Qt6 runtime next to alternative_hacker.exe"
        )
    else()
        message(WARNING "windeployqt not found - the packaged .exe won't include the Qt runtime")
    endif()
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

# --- Install rules ----------------------------------------------------
# Deliberately flat: both packagers below (CPack DEB and CPack NSIS) work
# fine with everything under a single prefix rather than a full FHS tree,
# and it keeps the Windows side simple (NSIS just needs "the exe and its
# DLLs in one directory").

if(WIN32)
    install(TARGETS alternative_hacker diskutil RUNTIME DESTINATION .)
    if(WINDEPLOYQT_EXECUTABLE)
        # Picks up whatever windeployqt dropped next to the exe (Qt DLLs,
        # platforms/, styles/, etc.) without re-listing it all by hand.
        # The exe/pdb/lib/exp files are excluded since install(TARGETS)
        # above already placed the executables themselves.
        install(DIRECTORY "$<TARGET_FILE_DIR:alternative_hacker>/" DESTINATION .
                PATTERN "*.exe" EXCLUDE
                PATTERN "*.pdb" EXCLUDE
                PATTERN "*.lib" EXCLUDE
                PATTERN "*.exp" EXCLUDE
                PATTERN "CMakeFiles" EXCLUDE)
    endif()
else()
    install(TARGETS alternative_hacker diskutil RUNTIME DESTINATION bin)
    install(FILES packaging/linux/alternative-hacker.desktop
            DESTINATION share/applications)
    install(FILES packaging/linux/copyright
            DESTINATION share/doc/alternative-hacker)

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/changelog.Debian.gz
        COMMAND sh -c "gzip -9 -n -c '${CMAKE_SOURCE_DIR}/packaging/linux/changelog' > '${CMAKE_BINARY_DIR}/changelog.Debian.gz'"
        DEPENDS ${CMAKE_SOURCE_DIR}/packaging/linux/changelog
        VERBATIM
    )
    add_custom_target(deb_changelog ALL DEPENDS ${CMAKE_BINARY_DIR}/changelog.Debian.gz)
    install(FILES ${CMAKE_BINARY_DIR}/changelog.Debian.gz
            DESTINATION share/doc/alternative-hacker)
endif()

# --- CPack: `.deb` on Linux, NSIS `.exe` installer on Windows ----------

set(CPACK_PACKAGE_NAME "alternative-hacker")
set(CPACK_PACKAGE_VENDOR "alternativeI")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Process manager and disk backup/restore tool")
# CPack DEB prepends CPACK_PACKAGE_DESCRIPTION_SUMMARY as the synopsis
# line automatically, so this must be the extended description only -
# repeating the summary as its own first line here trips lintian's
# description-synopsis-is-duplicated check.
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
"Cross-platform analog of Process Hacker / System Informer: a process
 tree with per-process threads/modules/memory/handles/network detail,
 system performance graphs, a service manager, and diskutil, a
 standalone CLI for SHA-256-verified MBR/GPT/disk backup and restore.")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_CONTACT "AlternativeI Project <unset@example.invalid>")
set(CPACK_STRIP_FILES TRUE)

if(UNIX AND NOT WIN32)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON) # auto-computes libqt6widgets6 etc. via dpkg-shlibdeps
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT") # proper name_version_arch.deb naming
    # "-1" packaging revision (upstream 0.1.0, Debian revision 1) marks
    # this as a non-native package to lintian - a bare "0.1.0" reads as
    # "the upstream project IS this Debian package" and demands a full
    # debian/changelog this project doesn't have.
    set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_PACKAGE_NAME "Alternative Hacker")
    set(CPACK_NSIS_DISPLAY_NAME "Alternative Hacker")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH ON) # offers to add diskutil.exe to PATH
    set(CPACK_PACKAGE_EXECUTABLES "alternative_hacker" "Alternative Hacker")
endif()

include(CPack)
