cmake_minimum_required(VERSION 3.16)
project(pinch VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(GNUInstallDirs)
include(CheckPIESupported)
check_pie_supported()

option(PINCH_WERROR "Считать предупреждения компилятора ошибками" ON)
option(PINCH_SANITIZE "Собрать с AddressSanitizer и UndefinedBehaviorSanitizer" OFF)
option(PINCH_WAYLAND "Поддержка Wayland (screencopy)" ON)
# Идентификатор приложения (обратная запись домена): имя .desktop, app_id окон Wayland, регистрация в портале.
set(PINCH_APP_ID "io.github.ufna.pinch")

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets DBus Test LinguistTools)

# Общие флаги для всех наших целей: предупреждения (как ошибки, если PINCH_WERROR) и hardening.
# Предупреждения — только для C++: C-код в проекте лишь сгенерированный (wayland-scanner), и его
# предупреждения (-Wpedantic) не должны ломать сборку. Hardening — для всех языков: компоновщик
# объединяет пометки CET (.note.gnu.property) по И, и один C-объект без -fcf-protection снял бы IBT/SHSTK.
set(PINCH_CXX "$<COMPILE_LANGUAGE:CXX>")
add_library(pinch_options INTERFACE)
target_compile_options(pinch_options INTERFACE
    "$<${PINCH_CXX}:-Wall;-Wextra;-Wpedantic>"
    -fstack-protector-strong -fstack-clash-protection -fcf-protection)
if(PINCH_WERROR)
    target_compile_options(pinch_options INTERFACE "$<${PINCH_CXX}:-Werror>")
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
    src/capturebackend.h src/capturebackend.cpp
    src/instancelock.h src/instancelock.cpp
    src/output.h src/output.cpp
    src/settings.h src/settings.cpp
    src/toolbar.h src/toolbar.cpp
    src/keys.h src/keys.cpp
    src/overlaycontroller.h src/overlaycontroller.cpp
    src/screenview.h src/screenview.cpp
    src/overlaysession.h src/overlaysession.cpp
    src/appicon.h src/appicon.cpp
    src/i18n.h src/i18n.cpp
    src/rawimage.h src/rawimage.cpp
    src/screencopy.h src/screencopy.cpp
    src/kwincapture.h src/kwincapture.cpp
    src/portalcapture.h src/portalcapture.cpp
    src/dumpable.h src/dumpable.cpp
)
# Иконки окна встроены в бинарь (для диалогов и заголовков); установка hicolor — ниже.
qt_add_resources(pinch_core "icons" PREFIX "/icons" BASE "data/icons/hicolor" FILES
    data/icons/hicolor/32x32/apps/pinch.png
    data/icons/hicolor/64x64/apps/pinch.png
    data/icons/hicolor/128x128/apps/pinch.png
    data/icons/hicolor/256x256/apps/pinch.png)
# Таблицы переводов (id-based .ts) компилируются в .qm и встраиваются в ресурсы: :/i18n/pinch_<lang>.qm.
qt_add_translations(pinch_core
    TS_FILES translations/pinch_en.ts translations/pinch_ru.ts
    RESOURCE_PREFIX "/i18n"
    LRELEASE_OPTIONS -idbased)
target_include_directories(pinch_core PUBLIC src)
target_compile_definitions(pinch_core PUBLIC PINCH_APP_ID="${PINCH_APP_ID}")
target_link_libraries(pinch_core PUBLIC Qt6::Widgets Qt6::DBus pinch_options)

# Wayland: собственное соединение wayland-client и протокол wlr-screencopy (код генерирует wayland-scanner).
if(PINCH_WAYLAND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
    find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
    set(PINCH_PROTOCOLS_DIR "${CMAKE_CURRENT_BINARY_DIR}/protocols")
    set(PINCH_SCREENCOPY_XML "${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-screencopy-unstable-v1.xml")
    set(PINCH_SCREENCOPY_H "${PINCH_PROTOCOLS_DIR}/wlr-screencopy-unstable-v1-client-protocol.h")
    set(PINCH_SCREENCOPY_C "${PINCH_PROTOCOLS_DIR}/wlr-screencopy-unstable-v1-protocol.c")
    add_custom_command(
        OUTPUT "${PINCH_SCREENCOPY_H}" "${PINCH_SCREENCOPY_C}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PINCH_PROTOCOLS_DIR}"
        COMMAND ${WAYLAND_SCANNER} client-header "${PINCH_SCREENCOPY_XML}" "${PINCH_SCREENCOPY_H}"
        COMMAND ${WAYLAND_SCANNER} private-code "${PINCH_SCREENCOPY_XML}" "${PINCH_SCREENCOPY_C}"
        DEPENDS "${PINCH_SCREENCOPY_XML}"
        COMMENT "wayland-scanner: wlr-screencopy-unstable-v1"
        VERBATIM)
    set_source_files_properties("${PINCH_SCREENCOPY_H}" "${PINCH_SCREENCOPY_C}" PROPERTIES SKIP_AUTOGEN ON)
    target_sources(pinch_core PRIVATE "${PINCH_SCREENCOPY_H}" "${PINCH_SCREENCOPY_C}")
    target_include_directories(pinch_core PRIVATE "${PINCH_PROTOCOLS_DIR}")
    target_compile_definitions(pinch_core PUBLIC PINCH_HAVE_WAYLAND=1)
    target_link_libraries(pinch_core PUBLIC PkgConfig::WAYLAND_CLIENT)
endif()

add_executable(pinch src/main.cpp)
target_link_libraries(pinch PRIVATE pinch_core)
target_compile_definitions(pinch PRIVATE PINCH_VERSION="${PROJECT_VERSION}")
# Все пути установки — через GNUInstallDirs: бинарь и Exec в .desktop берут один и тот же CMAKE_INSTALL_BINDIR.
install(TARGETS pinch RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY data/icons/hicolor DESTINATION ${CMAKE_INSTALL_DATADIR}/icons)
# Exec — абсолютный путь: по нему KWin сопоставляет процесс с .desktop (X-KDE-DBUS-Restricted-Interfaces).
# build/<id>.desktop — для проверки (desktop-file-validate) с префиксом времени конфигурации;
# при установке файл генерируется заново с настоящим префиксом (`cmake --install --prefix …`), затем ставится как обычный.
set(PINCH_DESKTOP_IN "${CMAKE_CURRENT_SOURCE_DIR}/data/${PINCH_APP_ID}.desktop.in")
set(PINCH_DESKTOP_INSTALL "${CMAKE_CURRENT_BINARY_DIR}/install/${PINCH_APP_ID}.desktop")
configure_file(${PINCH_DESKTOP_IN} ${CMAKE_CURRENT_BINARY_DIR}/${PINCH_APP_ID}.desktop @ONLY)
install(CODE "
    set(CMAKE_INSTALL_FULL_BINDIR \"${CMAKE_INSTALL_BINDIR}\")
    if(NOT IS_ABSOLUTE \"\${CMAKE_INSTALL_FULL_BINDIR}\")
        set(CMAKE_INSTALL_FULL_BINDIR \"\${CMAKE_INSTALL_PREFIX}/\${CMAKE_INSTALL_FULL_BINDIR}\")
    endif()
    configure_file(\"${PINCH_DESKTOP_IN}\" \"${PINCH_DESKTOP_INSTALL}\" @ONLY)
")
install(FILES ${PINCH_DESKTOP_INSTALL} DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

enable_testing()
add_subdirectory(tests)
