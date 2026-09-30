cmake_minimum_required(VERSION 3.16)
project(pinch VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(CheckPIESupported)
check_pie_supported()

option(PINCH_WERROR "Считать предупреждения компилятора ошибками" ON)
option(PINCH_SANITIZE "Собрать с AddressSanitizer и UndefinedBehaviorSanitizer" OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Test)

# Общие флаги для всех наших целей: предупреждения (как ошибки, если PINCH_WERROR) и hardening.
add_library(pinch_options INTERFACE)
target_compile_options(pinch_options INTERFACE
    -Wall -Wextra -Wpedantic
    -fstack-protector-strong -fstack-clash-protection -fcf-protection)
if(PINCH_WERROR)
    target_compile_options(pinch_options INTERFACE -Werror)
endif()
target_link_options(pinch_options INTERFACE
    -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
if(PINCH_SANITIZE)
    target_compile_options(pinch_options INTERFACE
        -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -U_FORTIFY_SOURCE)
    target_link_options(pinch_options INTERFACE -fsanitize=address,undefined)
else()
    # _FORTIFY_SOURCE требует оптимизации; Ubuntu уже задаёт его сама, поэтому сначала снимаем.
    target_compile_options(pinch_options INTERFACE
        "$<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=3>")
endif()

add_library(pinch_core STATIC
    src/annotation.h
    src/document.h src/document.cpp
    src/geometry.h src/geometry.cpp
    src/renderer.h src/renderer.cpp
    src/capture.h src/capture.cpp
    src/instancelock.h src/instancelock.cpp
    src/output.h src/output.cpp
    src/settings.h src/settings.cpp
    src/toolbar.h src/toolbar.cpp
    src/keys.h src/keys.cpp
    src/overlay.h src/overlay.cpp
    src/appicon.h src/appicon.cpp
)
# Иконки окна встроены в бинарь (для диалогов и заголовков); установка hicolor — ниже.
qt_add_resources(pinch_core "icons" PREFIX "/icons" BASE "data/icons/hicolor" FILES
    data/icons/hicolor/32x32/apps/pinch.png
    data/icons/hicolor/64x64/apps/pinch.png
    data/icons/hicolor/128x128/apps/pinch.png
    data/icons/hicolor/256x256/apps/pinch.png)
target_include_directories(pinch_core PUBLIC src)
target_link_libraries(pinch_core PUBLIC Qt6::Widgets pinch_options)

add_executable(pinch src/main.cpp)
target_link_libraries(pinch PRIVATE pinch_core)
target_compile_definitions(pinch PRIVATE PINCH_VERSION="${PROJECT_VERSION}")
install(TARGETS pinch DESTINATION bin)
install(DIRECTORY data/icons/hicolor DESTINATION share/icons)
install(FILES data/pinch.desktop DESTINATION share/applications)

enable_testing()
add_subdirectory(tests)
