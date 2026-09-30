# Один исполняемый файл на набор тестов; все тесты идут без экрана (offscreen).
function(hs_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE hs_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set(env "QT_QPA_PLATFORM=offscreen")
    if(HS_SANITIZE)
        list(APPEND env "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp:print_suppressions=0")
    endif()
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${env}")
endfunction()

hs_add_test(test_geometry)
hs_add_test(test_capture)
