cmake_minimum_required(VERSION 3.21)
project(CorinthPTZ VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt 6.5+ recommended on Windows (FFmpeg media backend plays RTSP).
# 6.4 is the minimum that compiles.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Multimedia)

# ---- SDL2: joystick input (gamepads, flight sticks, PTZ keyboards, generic HID) --------
# An installed SDL2 (SDL2_DIR / CMAKE_PREFIX_PATH) is used if there is one; otherwise SDL2 is
# downloaded and built statically, so nothing has to be installed by hand.
option(CORINTH_WITH_SDL2 "Joystick support through SDL2" ON)
option(CORINTH_FETCH_SDL2 "Download and build SDL2 statically when none is installed" ON)
set(CORINTH_SDL2_TARGET "")
if(CORINTH_WITH_SDL2)
    find_package(SDL2 CONFIG QUIET)
    if(SDL2_FOUND AND TARGET SDL2::SDL2)
        set(CORINTH_SDL2_TARGET SDL2::SDL2)
        message(STATUS "Corinth: using the installed SDL2")
    elseif(CORINTH_FETCH_SDL2)
        include(FetchContent)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
        set(SDL2_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL2
            URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.zip
            URL_HASH SHA256=ec855bcd815b4b63d0c958c42c2923311c656227d6e0c1ae1e721406d346444b
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(SDL2)
        if(TARGET SDL2::SDL2-static)
            set(CORINTH_SDL2_TARGET SDL2::SDL2-static)
        elseif(TARGET SDL2-static)
            set(CORINTH_SDL2_TARGET SDL2-static)
        endif()
        message(STATUS "Corinth: SDL2 built statically from source")
    endif()
endif()
if(NOT CORINTH_SDL2_TARGET)
    message(STATUS "Corinth: no SDL2 - Xbox-compatible gamepads only (XInput on Windows)")
endif()

# ---- FFmpeg: the low-latency video decoder (src/LowLatencyPlayer) -------------------
# FFMPEG_DIR = a folder with include/, lib/ and (on Windows) bin/ of a *shared* FFmpeg 6-8 build.
# Left empty on Windows, a pinned LGPL shared build is downloaded and hash-checked. Without
# FFmpeg the app still builds and plays video through Qt's MediaPlayer, with more delay.
set(FFMPEG_DIR "" CACHE PATH "FFmpeg development files (include, lib, bin); empty = download on Windows")
option(CORINTH_WITH_FFMPEG "Build the low-latency FFmpeg video player" ON)
option(CORINTH_FETCH_FFMPEG "Download a shared FFmpeg build on Windows when FFMPEG_DIR is not set" ON)
set(CORINTH_FFMPEG_OK OFF)
set(CORINTH_FFMPEG_LIBS "")
if(CORINTH_WITH_FFMPEG)
    if(NOT FFMPEG_DIR AND WIN32 AND CORINTH_FETCH_FFMPEG)
        include(FetchContent)
        FetchContent_Declare(ffmpeg_dev
            URL https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-07-31-14-10/ffmpeg-n7.1.5-12-g1fdbca85aa-win64-lgpl-shared-7.1.zip
            URL_HASH SHA256=0f376f96fb38554ccefb1b2ae9c7c6a7b351f0e60a372b38262c320e8392c5d0
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(ffmpeg_dev)
        set(FFMPEG_DIR "${ffmpeg_dev_SOURCE_DIR}")
    endif()
    find_path(CORINTH_FFMPEG_INCLUDE libavformat/avformat.h HINTS "${FFMPEG_DIR}/include")
    set(CORINTH_FFMPEG_ALL_FOUND ON)
    foreach(_lib avformat avcodec avutil swscale)
        find_library(CORINTH_FFMPEG_LIB_${_lib} NAMES ${_lib} HINTS "${FFMPEG_DIR}/lib")
        if(CORINTH_FFMPEG_LIB_${_lib})
            list(APPEND CORINTH_FFMPEG_LIBS "${CORINTH_FFMPEG_LIB_${_lib}}")
        else()
            set(CORINTH_FFMPEG_ALL_FOUND OFF)
        endif()
    endforeach()
    if(CORINTH_FFMPEG_INCLUDE AND CORINTH_FFMPEG_ALL_FOUND)
        set(CORINTH_FFMPEG_OK ON)
        message(STATUS "Corinth: FFmpeg low-latency player enabled (${CORINTH_FFMPEG_INCLUDE})")
    else()
        message(STATUS "Corinth: FFmpeg not found - video plays through Qt MediaPlayer (more delay). Set FFMPEG_DIR to enable the low-latency player.")
    endif()
endif()

qt_add_executable(CorinthPTZ
    src/main.cpp
    src/AppController.h        src/AppController.cpp
    src/AuditLog.h             src/AuditLog.cpp
    src/PtzController.h        src/PtzController.cpp
    src/LinkMonitor.h          src/LinkMonitor.cpp
    src/CaptureService.h       src/CaptureService.cpp
    src/Discovery.h            src/Discovery.cpp
    src/JoystickManager.h      src/JoystickManager.cpp
    src/HealthMonitor.h        src/HealthMonitor.cpp
    src/LowLatencyPlayer.h     src/LowLatencyPlayer.cpp
    src/ControlCoordinator.h   src/ControlCoordinator.cpp
    src/NetUtil.h              src/NetUtil.cpp
    src/CredentialStore.h      src/CredentialStore.cpp
    src/drivers/CameraProfile.h src/drivers/CameraProfile.cpp
    src/drivers/CameraDriver.h src/drivers/CameraDriver.cpp
    src/drivers/IsapiDriver.h  src/drivers/IsapiDriver.cpp
    src/drivers/OnvifDriver.h  src/drivers/OnvifDriver.cpp
    src/drivers/DemoDriver.h   src/drivers/DemoDriver.cpp
)

target_include_directories(CorinthPTZ PRIVATE src)
target_compile_definitions(CorinthPTZ PRIVATE APP_VERSION_STRING="${PROJECT_VERSION}")

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(CorinthPTZ
    URI Corinth
    VERSION 1.0
    RESOURCE_PREFIX /
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/components/Card.qml
        qml/components/OverlayCard.qml
        qml/components/Lbl.qml
        qml/components/Mono.qml
        qml/components/Btn.qml
        qml/components/Seg.qml
        qml/components/SwitchPill.qml
        qml/components/ToggleRow.qml
        qml/components/SliderRow.qml
        qml/components/DropField.qml
        qml/components/TextRow.qml
        qml/components/Dot.qml
        qml/components/VirtualJoystick.qml
        qml/components/DirectionPad.qml
        qml/components/Toast.qml
        qml/pages/LiveView.qml
        qml/pages/SettingsPage.qml
        qml/pages/InputPage.qml
        qml/pages/CamerasPanel.qml
        qml/pages/ControllerDiagram.qml
        qml/pages/EndpointTester.qml
        qml/js/Schema.js
)

# Built-in camera profile (endpoints as data); users override it with JSON files.
qt_add_resources(CorinthPTZ "profiles"
    PREFIX "/profiles"
    BASE "profiles"
    FILES profiles/hikvision-isapi.json)

target_link_libraries(CorinthPTZ PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Multimedia)

if(CORINTH_SDL2_TARGET)
    target_compile_definitions(CorinthPTZ PRIVATE CORINTH_HAS_SDL2)
    target_link_libraries(CorinthPTZ PRIVATE ${CORINTH_SDL2_TARGET})
endif()

if(CORINTH_FFMPEG_OK)
    target_compile_definitions(CorinthPTZ PRIVATE CORINTH_HAS_FFMPEG)
    target_include_directories(CorinthPTZ PRIVATE "${CORINTH_FFMPEG_INCLUDE}")
    target_link_libraries(CorinthPTZ PRIVATE ${CORINTH_FFMPEG_LIBS})
endif()

if(MSVC)
    # Source files contain UTF-8 symbols (·, →, ×); MSVC needs to be told.
    target_compile_options(CorinthPTZ PRIVATE /utf-8)
endif()

if(WIN32)
    target_link_libraries(CorinthPTZ PRIVATE xinput advapi32)   # advapi32: Windows Credential Manager
    set_target_properties(CorinthPTZ PROPERTIES WIN32_EXECUTABLE ON)

    # Copy the Qt DLLs, QML modules and multimedia plugins next to the .exe after
    # every build so it runs straight from VS Code or Explorer.
    option(CORINTH_DEPLOY_ON_BUILD "Run windeployqt after each build" ON)
    find_program(CORINTH_WINDEPLOYQT windeployqt
        HINTS "${QT6_INSTALL_PREFIX}/bin" "${Qt6_DIR}/../../../bin")
    if(CORINTH_DEPLOY_ON_BUILD AND CORINTH_WINDEPLOYQT)
        add_custom_command(TARGET CorinthPTZ POST_BUILD
            COMMAND "${CORINTH_WINDEPLOYQT}" --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                    --no-translations --no-compiler-runtime "$<TARGET_FILE:CorinthPTZ>"
            COMMENT "Deploying Qt runtime next to CorinthPTZ.exe")
    endif()
    # windeployqt is told to skip the compiler runtime, but a MinGW build cannot
    # start without these three DLLs unless the compiler is on PATH.
    if(MINGW)
        get_filename_component(CORINTH_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
        add_custom_command(TARGET CorinthPTZ POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CORINTH_MINGW_BIN}/libgcc_s_seh-1.dll"
                    "${CORINTH_MINGW_BIN}/libstdc++-6.dll"
                    "${CORINTH_MINGW_BIN}/libwinpthread-1.dll"
                    "$<TARGET_FILE_DIR:CorinthPTZ>"
            COMMENT "Copying MinGW runtime DLLs")
    endif()
    # FFmpeg's DLLs go next to the exe last, over the ones windeployqt copied for Qt Multimedia
    # (same FFmpeg 7.1 series, so Qt's own player keeps working with them).
    if(CORINTH_FFMPEG_OK AND EXISTS "${FFMPEG_DIR}/bin")
        file(GLOB CORINTH_FFMPEG_DLLS "${FFMPEG_DIR}/bin/av*.dll" "${FFMPEG_DIR}/bin/sw*.dll")
        foreach(_dll ${CORINTH_FFMPEG_DLLS})
            add_custom_command(TARGET CorinthPTZ POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:CorinthPTZ>"
                COMMENT "Copying FFmpeg runtime")
        endforeach()
    endif()
    # Recording runs ffmpeg as a program; the one from the same FFmpeg package goes next to the exe.
    if(CORINTH_FFMPEG_OK AND EXISTS "${FFMPEG_DIR}/bin/ffmpeg.exe")
        add_custom_command(TARGET CorinthPTZ POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_DIR}/bin/ffmpeg.exe" "$<TARGET_FILE_DIR:CorinthPTZ>"
            COMMENT "Copying ffmpeg.exe (used for recording)")
    endif()
    if(TARGET SDL2::SDL2)
        get_target_property(CORINTH_SDL2_TYPE SDL2::SDL2 TYPE)
        if(CORINTH_SDL2_TYPE STREQUAL "SHARED_LIBRARY")
            add_custom_command(TARGET CorinthPTZ POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:CorinthPTZ>"
                COMMENT "Copying SDL2.dll")
        endif()
    endif()
endif()

if(APPLE)
    set_target_properties(CorinthPTZ PROPERTIES MACOSX_BUNDLE ON)
endif()

# ---- unit tests: ctest --test-dir build --output-on-failure ----------------------
option(CORINTH_BUILD_TESTS "Build the unit tests" ON)
if(CORINTH_BUILD_TESTS)
    enable_testing()

    qt_add_executable(ctl_test
        tests/ctl_test.cpp
        src/ControlCoordinator.h src/ControlCoordinator.cpp
        src/NetUtil.h src/NetUtil.cpp)
    target_include_directories(ctl_test PRIVATE src)
    target_link_libraries(ctl_test PRIVATE Qt6::Core Qt6::Network)
    add_test(NAME control_coordinator COMMAND ctl_test)

    if(CORINTH_SDL2_TARGET)
        qt_add_executable(joystick_test
            tests/joystick_test.cpp
            src/JoystickManager.h src/JoystickManager.cpp)
        target_include_directories(joystick_test PRIVATE src)
        target_compile_definitions(joystick_test PRIVATE CORINTH_HAS_SDL2)
        target_link_libraries(joystick_test PRIVATE Qt6::Core ${CORINTH_SDL2_TARGET})
        add_test(NAME joystick_input COMMAND joystick_test)
    endif()

    if(CORINTH_FFMPEG_OK)
        qt_add_executable(video_test
            tests/video_test.cpp
            src/LowLatencyPlayer.h src/LowLatencyPlayer.cpp)
        target_include_directories(video_test PRIVATE src "${CORINTH_FFMPEG_INCLUDE}")
        target_compile_definitions(video_test PRIVATE CORINTH_HAS_FFMPEG)
        target_link_libraries(video_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Multimedia ${CORINTH_FFMPEG_LIBS})
        if(WIN32 AND EXISTS "${FFMPEG_DIR}/bin")
            file(GLOB CORINTH_FFMPEG_TEST_DLLS "${FFMPEG_DIR}/bin/av*.dll" "${FFMPEG_DIR}/bin/sw*.dll")
            foreach(_dll ${CORINTH_FFMPEG_TEST_DLLS})
                add_custom_command(TARGET video_test POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:video_test>")
            endforeach()
        endif()
        add_test(NAME low_latency_video COMMAND video_test)
        set_tests_properties(low_latency_video PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=${QT6_INSTALL_PREFIX}/plugins/platforms")
    endif()

    qt_add_executable(credential_test
        tests/credential_test.cpp
        src/CredentialStore.h src/CredentialStore.cpp)
    target_include_directories(credential_test PRIVATE src)
    target_link_libraries(credential_test PRIVATE Qt6::Core)
    if(WIN32)
        target_link_libraries(credential_test PRIVATE advapi32)
    endif()
    add_test(NAME credential_store COMMAND credential_test)

    qt_add_executable(discovery_test
        tests/discovery_test.cpp
        src/Discovery.h src/Discovery.cpp
        src/NetUtil.h src/NetUtil.cpp)
    target_include_directories(discovery_test PRIVATE src)
    target_link_libraries(discovery_test PRIVATE Qt6::Core Qt6::Network)
    add_test(NAME camera_discovery COMMAND discovery_test)

    qt_add_executable(driver_test
        tests/driver_test.cpp
        src/drivers/CameraProfile.h src/drivers/CameraProfile.cpp
        src/drivers/CameraDriver.h src/drivers/CameraDriver.cpp
        src/drivers/IsapiDriver.h src/drivers/IsapiDriver.cpp
        src/drivers/OnvifDriver.h src/drivers/OnvifDriver.cpp
        src/drivers/DemoDriver.h src/drivers/DemoDriver.cpp)
    target_include_directories(driver_test PRIVATE src src/drivers)
    qt_add_resources(driver_test "profiles"
        PREFIX "/profiles"
        BASE "profiles"
        FILES profiles/hikvision-isapi.json)
    target_link_libraries(driver_test PRIVATE Qt6::Core Qt6::Network)
    add_test(NAME camera_drivers COMMAND driver_test)
endif()
