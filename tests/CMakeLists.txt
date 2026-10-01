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

# Проверка согласованности ключей локализации и таблиц (без сборки).
add_test(NAME check_i18n
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_i18n.cmake)
