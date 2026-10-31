cmake_minimum_required(VERSION 3.21)
project(CorinthPTZ VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt 6.5+ recommended on Windows (FFmpeg media backend plays RTSP).
# 6.4 is the minimum that compiles.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Multimedia)

option(CORINTH_WITH_SDL2 "Use SDL2 for joystick input when it is installed (adds flight sticks and generic joysticks)" ON)

qt_add_executable(CorinthPTZ
    src/main.cpp
    src/AppController.h        src/AppController.cpp
    src/AuditLog.h             src/AuditLog.cpp
    src/PtzController.h        src/PtzController.cpp
    src/LinkMonitor.h          src/LinkMonitor.cpp
    src/CaptureService.h       src/CaptureService.cpp
    src/Discovery.h            src/Discovery.cpp
    src/JoystickManager.h      src/JoystickManager.cpp
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
        qml/js/Schema.js
)

target_link_libraries(CorinthPTZ PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Multimedia)

if(CORINTH_WITH_SDL2)
    find_package(SDL2 CONFIG QUIET)
    if(SDL2_FOUND)
        message(STATUS "Corinth: SDL2 found - flight sticks and generic joysticks enabled")
        target_compile_definitions(CorinthPTZ PRIVATE CORINTH_HAS_SDL2)
        if(TARGET SDL2::SDL2)
            target_link_libraries(CorinthPTZ PRIVATE SDL2::SDL2)
        else()
            target_link_libraries(CorinthPTZ PRIVATE ${SDL2_LIBRARIES})
            target_include_directories(CorinthPTZ PRIVATE ${SDL2_INCLUDE_DIRS})
        endif()
    else()
        message(STATUS "Corinth: SDL2 not found - using XInput (Windows) for Xbox-type gamepads only")
    endif()
endif()

if(MSVC)
    # Source files contain UTF-8 symbols (·, →, ×); MSVC needs to be told.
    target_compile_options(CorinthPTZ PRIVATE /utf-8)
endif()

if(WIN32)
    target_link_libraries(CorinthPTZ PRIVATE xinput)
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
    if(SDL2_FOUND AND TARGET SDL2::SDL2)
        add_custom_command(TARGET CorinthPTZ POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:CorinthPTZ>"
            COMMENT "Copying SDL2.dll")
    endif()
endif()

if(APPLE)
    set_target_properties(CorinthPTZ PROPERTIES MACOSX_BUNDLE ON)
endif()
