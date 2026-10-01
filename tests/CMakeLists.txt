# Один исполняемый файл на набор тестов; все тесты идут без экрана (offscreen).
function(pinch_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE pinch_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set(env "QT_QPA_PLATFORM=offscreen")
    if(PINCH_SANITIZE)
        list(APPEND env "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp:print_suppressions=0")
    endif()
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${env}")
endfunction()

pinch_add_test(test_geometry)
pinch_add_test(test_capture)
pinch_add_test(test_capturebackend)
pinch_add_test(test_instancelock)
pinch_add_test(test_document)
pinch_add_test(test_renderer)
pinch_add_test(test_output)
pinch_add_test(test_settings)
pinch_add_test(test_toolbar)
pinch_add_test(test_keys)
pinch_add_test(test_overlay)
pinch_add_test(test_appicon)
pinch_add_test(test_i18n)
pinch_add_test(test_rawimage)

# Клиент KWin ScreenShot2 против поддельного KWin на приватной шине (dbus-daemon должен быть в PATH).
add_executable(fake_kwin fakes/fake_kwin.cpp)
target_link_libraries(fake_kwin PRIVATE Qt6::Gui Qt6::DBus pinch_options)
pinch_add_test(test_kwincapture)
add_dependencies(test_kwincapture fake_kwin)
target_include_directories(test_kwincapture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_kwincapture PRIVATE FAKE_KWIN_PATH="$<TARGET_FILE:fake_kwin>")
if(PINCH_WAYLAND)
    pinch_add_test(test_screencopy_live)

    # Клиент screencopy против поддельного композитора. Нужен wayland-server (тот же пакет, что и wayland-client:
    # Ubuntu — libwayland-dev, Arch — wayland); без него тест не собирается. В pinch и pinch_core не входит.
    pkg_check_modules(WAYLAND_SERVER QUIET IMPORTED_TARGET wayland-server)
    if(WAYLAND_SERVER_FOUND)
        set(fake_protocols_dir "${CMAKE_CURRENT_BINARY_DIR}/protocols")
        set(fake_screencopy_h "${fake_protocols_dir}/wlr-screencopy-unstable-v1-server-protocol.h")
        set(fake_screencopy_c "${fake_protocols_dir}/wlr-screencopy-unstable-v1-protocol.c")
        add_custom_command(
            OUTPUT "${fake_screencopy_h}" "${fake_screencopy_c}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${fake_protocols_dir}"
            COMMAND ${WAYLAND_SCANNER} server-header "${PINCH_SCREENCOPY_XML}" "${fake_screencopy_h}"
            COMMAND ${WAYLAND_SCANNER} private-code "${PINCH_SCREENCOPY_XML}" "${fake_screencopy_c}"
            DEPENDS "${PINCH_SCREENCOPY_XML}"
            COMMENT "wayland-scanner: wlr-screencopy-unstable-v1 (server, for tests)"
            VERBATIM)
        add_executable(fake_compositor fakes/fake_compositor.cpp "${fake_screencopy_h}" "${fake_screencopy_c}")
        set_target_properties(fake_compositor PROPERTIES AUTOMOC OFF)
        target_include_directories(fake_compositor PRIVATE "${fake_protocols_dir}")
        target_link_libraries(fake_compositor PRIVATE PkgConfig::WAYLAND_SERVER pinch_options)

        pinch_add_test(test_screencopy_fake)
        add_dependencies(test_screencopy_fake fake_compositor)
        target_compile_definitions(test_screencopy_fake PRIVATE FAKE_COMPOSITOR_PATH="$<TARGET_FILE:fake_compositor>")
    endif()
endif()

# Проверка согласованности ключей локализации и таблиц (без сборки).
add_test(NAME check_i18n
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_i18n.cmake)
