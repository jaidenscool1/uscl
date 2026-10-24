cmake_minimum_required(VERSION 3.21)
project(uscl-shell LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Multimedia Network)
find_package(SDL3 REQUIRED CONFIG)
find_package(Vulkan QUIET)   # headers only: the GPU check for Automatic resolution scale
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
    src/updater.cpp src/updater.h
    third_party/tweetnacl/tweetnacl.c
)

# the JStationX XMB artwork (tools/xmb_assets.py)
file(GLOB_RECURSE XMB_ASSETS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS xmb/*.png)

qt_add_qml_module(uscl
    URI Uscl
    QML_FILES
        qml/Main.qml
        qml/Background.qml
        qml/Xmb.qml
        qml/Page.qml
        qml/OptionsPanel.qml
        qml/Dialog.qml
        qml/Hints.qml
        qml/Notice.qml
        qml/ClockBox.qml
        qml/Boot.qml
        qml/Setup.qml
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
        # compiled from tint.frag, so builds don't need Qt Shader Tools:
        # qsb --glsl "100es,120,150" --hlsl 50 --msl 12 -o shaders/tint.frag.qsb shaders/tint.frag
        shaders/tint.frag.qsb
        input/sdl_player.yml
        input/null_player.yml
        ${XMB_ASSETS}
)

# the release tag sets the version (CI: -DUSCL_VERSION=${tag}); updates come from the public releases repo
set(USCL_VERSION "0.3.0" CACHE STRING "USCL version")
set(USCL_RELEASES "jaidenscool1/uscl" CACHE STRING "GitHub repo USCL updates come from")
# Ed25519 public key that every update must be signed with (the private key is a CI secret, see packaging/README.md)
set(USCL_UPDATE_KEY "96108d612f3d53ba337cdaa6e383ac73f2c05ab0a4402e2e4ca1a9525771892a" CACHE STRING "update signing public key (hex)")
target_compile_definitions(uscl PRIVATE USCL_VERSION="${USCL_VERSION}" USCL_RELEASES="${USCL_RELEASES}" USCL_UPDATE_KEY="${USCL_UPDATE_KEY}")
if(Vulkan_FOUND)
    target_include_directories(uscl PRIVATE ${Vulkan_INCLUDE_DIRS})
endif()
target_link_libraries(uscl PRIVATE Qt6::Quick Qt6::Multimedia Qt6::Network SDL3::SDL3)

set_target_properties(uscl PROPERTIES WIN32_EXECUTABLE ON)
install(TARGETS uscl RUNTIME DESTINATION bin)
