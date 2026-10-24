cmake_minimum_required(VERSION 3.21)
project(uscl-shell LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Multimedia Network)
find_package(SDL3 REQUIRED CONFIG)
qt_standard_project_setup(REQUIRES 6.5)
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(uscl
    src/main.cpp
    src/games.cpp src/games.h
    src/gamepad.cpp src/gamepad.h
    src/launcher.cpp src/launcher.h
    src/system.cpp src/system.h
    src/online.cpp src/online.h
)

qt_add_qml_module(uscl
    URI Uscl
    QML_FILES
        qml/Main.qml
        qml/Background.qml
        qml/Xmb.qml
        qml/XmbItem.qml
        qml/SidePanel.qml
        qml/StatusPill.qml
        qml/Icon.qml
        qml/Setup.qml
        qml/Toast.qml
        qml/TextPrompt.qml
    RESOURCES
        fonts/MaterialDesignIcons.ttf
        fonts/NotoSans-Regular.ttf
        fonts/NotoSans-Light.ttf
        fonts/Roboto-Thin.ttf
        sounds/xmb/back.wav
        sounds/xmb/cancel.wav
        sounds/xmb/category.wav
        sounds/xmb/cursor.wav
        sounds/xmb/error.wav
        sounds/xmb/notify.wav
        sounds/xmb/select.wav
        sounds/xmb/startup.wav
        # compiled from wave.frag, so builds don't need Qt Shader Tools:
        # qsb --glsl "100es,120,150" --hlsl 50 --msl 12 -o shaders/wave.frag.qsb shaders/wave.frag
        shaders/wave.frag.qsb
)

target_link_libraries(uscl PRIVATE Qt6::Quick Qt6::Multimedia Qt6::Network SDL3::SDL3)

set_target_properties(uscl PROPERTIES WIN32_EXECUTABLE ON)
install(TARGETS uscl RUNTIME DESTINATION bin)
