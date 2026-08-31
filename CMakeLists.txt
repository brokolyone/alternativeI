cmake_minimum_required(VERSION 3.21)
project(AltTools VERSION 0.6.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Generates src/gui/AppVersion.h (kAppVersion) from the PROJECT_VERSION set
# above, so the About dialog can't drift out of sync with the version CPack
# and the release tag actually use.
configure_file(
    src/gui/AppVersion.h.in
    ${CMAKE_BINARY_DIR}/generated/gui/AppVersion.h
    @ONLY
)

# diskutil_core: platform-agnostic raw disk backup/restore logic (MBR/GPT
# parsing, SHA-256, the backup/restore/verify copy loops). Shared between
# the diskutil CLI and the GUI's Disk tab so both go through the exact
# same safety-critical code path rather than maintaining two copies of
# logic that can destroy data if it drifts.
add_library(diskutil_core STATIC
    src/diskutil/BlockDevice.h
    src/diskutil/BlockDevice.cpp
    src/diskutil/Partitioning.h
    src/diskutil/Partitioning.cpp
    src/diskutil/Sha256.h
    src/diskutil/Sha256.cpp
    src/diskutil/DiskOperations.h
    src/diskutil/DiskOperations.cpp
)
target_include_directories(diskutil_core PUBLIC src)

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
    src/gui/i18n.h
    src/gui/i18n.cpp
    src/gui/AboutDialog.h
    src/gui/AboutDialog.cpp
    src/gui/SettingsDialog.h
    src/gui/SettingsDialog.cpp
    src/gui/AffinityDialog.h
    src/gui/AffinityDialog.cpp
    src/gui/Theme.h
    src/gui/Theme.cpp
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
    src/gui/DiskWorker.h
    src/gui/DiskWorker.cpp
    src/gui/DiskDeviceList.h
    src/gui/DiskDeviceList.cpp
    src/gui/RestoreConfirmDialog.h
    src/gui/RestoreConfirmDialog.cpp
    src/gui/DiskToolsView.h
    src/gui/DiskToolsView.cpp
    src/gui/NetworkView.h
    src/gui/NetworkView.cpp
    src/gui/SystemInfoView.h
    src/gui/SystemInfoView.cpp
    src/gui/StartupPrograms.h
    src/gui/StartupPrograms.cpp
    src/gui/StartupProgramsView.h
    src/gui/StartupProgramsView.cpp
)

add_executable(alttools
    src/main.cpp
    ${CORE_SOURCES}
    ${GUI_SOURCES}
)

target_include_directories(alttools PRIVATE src ${CMAKE_BINARY_DIR}/generated/gui)
target_link_libraries(alttools PRIVATE Qt6::Widgets diskutil_core)

if(WIN32)
    target_link_libraries(alttools PRIVATE ntdll advapi32 psapi iphlpapi ws2_32 pdh)
    target_compile_definitions(alttools PRIVATE UNICODE _UNICODE)

    # Bundle the Qt runtime (DLLs + platform/style plugins) so the NSIS
    # installer built below has something self-contained to package - a
    # bare alttools.exe won't run on a machine without a
    # matching system Qt6 install.
    #
    # Deployed into its own directory (via windeployqt's --dir) rather
    # than alongside the exe in the main build tree: an earlier version
    # of this rule installed straight from $<TARGET_FILE_DIR:...>, which
    # for the single-config Ninja generator used in CI *is* the build
    # root - install(DIRECTORY ...) from there recursively copied the
    # entire build tree, including CPack's own staging directory that it
    # creates inside that same build root while packaging, into the
    # package. `cmake --install` hung for 6+ minutes copying that
    # self-referential/ever-growing tree before a CI timeout caught it.
    set(QT_DEPLOY_DIR "${CMAKE_BINARY_DIR}/qt-deploy")
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt)
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET alttools POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${QT_DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:alttools>" "${QT_DEPLOY_DIR}/"
            COMMAND ${WINDEPLOYQT_EXECUTABLE} --no-compiler-runtime --no-translations
                    "${QT_DEPLOY_DIR}/$<TARGET_FILE_NAME:alttools>"
            COMMENT "Deploying Qt6 runtime into ${QT_DEPLOY_DIR}"
        )
    else()
        message(WARNING "windeployqt not found - the packaged .exe won't include the Qt runtime")
    endif()
endif()

# diskutil: standalone CLI for raw MBR/GPT/disk backup-restore, on top of
# diskutil_core. Kept free of the Qt dependency so it builds (and can be
# shipped/audited) on its own - see README's disk module warning before
# running it against a real device.
add_executable(diskutil src/diskutil/main.cpp)
target_link_libraries(diskutil PRIVATE diskutil_core)

if(WIN32)
    target_compile_definitions(diskutil PRIVATE UNICODE _UNICODE)
endif()

# --- Install rules ----------------------------------------------------
# Deliberately flat: both packagers below (CPack DEB and CPack NSIS) work
# fine with everything under a single prefix rather than a full FHS tree,
# and it keeps the Windows side simple (NSIS just needs "the exe and its
# DLLs in one directory").

if(WIN32)
    install(TARGETS alttools diskutil RUNTIME DESTINATION .)
    if(WINDEPLOYQT_EXECUTABLE)
        # Picks up whatever windeployqt dropped into its own deploy
        # directory (Qt DLLs, platforms/, styles/, etc.) without
        # re-listing it all by hand. That directory contains a *copy* of
        # the exe (windeployqt needs one alongside the DLLs to scan), so
        # it's excluded here - install(TARGETS) above already placed the
        # real one.
        install(DIRECTORY "${QT_DEPLOY_DIR}/" DESTINATION .
                PATTERN "*.exe" EXCLUDE)
    endif()
else()
    install(TARGETS alttools diskutil RUNTIME DESTINATION bin)
    install(FILES packaging/linux/alttools.desktop
            DESTINATION share/applications)
    install(FILES packaging/linux/copyright
            DESTINATION share/doc/alttools)

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/changelog.Debian.gz
        COMMAND sh -c "gzip -9 -n -c '${CMAKE_SOURCE_DIR}/packaging/linux/changelog' > '${CMAKE_BINARY_DIR}/changelog.Debian.gz'"
        DEPENDS ${CMAKE_SOURCE_DIR}/packaging/linux/changelog
        VERBATIM
    )
    add_custom_target(deb_changelog ALL DEPENDS ${CMAKE_BINARY_DIR}/changelog.Debian.gz)
    install(FILES ${CMAKE_BINARY_DIR}/changelog.Debian.gz
            DESTINATION share/doc/alttools)
endif()

# --- CPack: `.deb` on Linux, NSIS `.exe` installer on Windows ----------

set(CPACK_PACKAGE_NAME "alttools")
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
    # DEB is the installed package; TGZ is the same staged tree (still
    # nested under usr/ - see CPACK_PACKAGING_INSTALL_PREFIX below) handed
    # out as a portable tarball for anyone who'd rather extract-and-run
    # than involve dpkg.
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON) # auto-computes libqt6widgets6 etc. via dpkg-shlibdeps
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT") # proper name_version_arch.deb naming
    # "-1" packaging revision (upstream version, Debian revision 1) marks
    # this as a non-native package to lintian - a bare version reads as
    # "the upstream project IS this Debian package" and demands a full
    # debian/changelog this project doesn't have.
    set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
elseif(WIN32)
    # NSIS is the installer; ZIP is the same flat install tree the
    # installer wraps, archived directly instead - "extract and run", no
    # installer/uninstaller/PATH changes, for a portable option alongside it.
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_PACKAGE_NAME "AltTools")
    set(CPACK_NSIS_DISPLAY_NAME "AltTools")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH ON) # offers to add diskutil.exe to PATH
    set(CPACK_PACKAGE_EXECUTABLES "alttools" "AltTools")
endif()

# Gives the portable archive generators (ZIP/TGZ) a "-portable" filename
# distinct from the installer/.deb, which otherwise share the same
# CPACK_PACKAGE_FILE_NAME base - see packaging/CPackProjectConfig.cmake.
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/packaging/CPackProjectConfig.cmake")

include(CPack)
