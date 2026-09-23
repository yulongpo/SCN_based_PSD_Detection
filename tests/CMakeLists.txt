# No framework download, GPU, device or real data file is needed for these tests.
# The test-only factory in ScnTests.cpp prevents pulling the production backend
# object from the static library. Never link that factory into product targets.
add_executable(scn_tests ScnTests.cpp)
target_link_libraries(scn_tests PRIVATE scn_algorithm scn_source scn_application scn_runtime)
foreach(group algorithm channel pipeline source hash frequency)
    add_test(NAME scn_${group} COMMAND scn_tests ${group})
    set_tests_properties(scn_${group} PROPERTIES TIMEOUT 30)
endforeach()

# Real QThread/QTimer session integration, still with a test-only backend/no GPU.
add_executable(scn_session_tests SessionTests.cpp)
target_link_libraries(scn_session_tests PRIVATE scn_application)
add_test(NAME scn_session COMMAND scn_session_tests)
set_tests_properties(scn_session PROPERTIES TIMEOUT 30)
if(WIN32)
    add_custom_command(TARGET scn_session_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::Core> $<TARGET_FILE_DIR:scn_session_tests>
        COMMENT "Deploying Qt Core for user-run session tests" VERBATIM)
    get_property(SCN_SOURCE_RUNTIME_DLLS GLOBAL PROPERTY SCN_SOURCE_RUNTIME_DLLS)
    foreach(SCN_SOURCE_RUNTIME_DLL IN LISTS SCN_SOURCE_RUNTIME_DLLS)
        if(EXISTS "${SCN_SOURCE_RUNTIME_DLL}")
            add_custom_command(TARGET scn_session_tests POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${SCN_SOURCE_RUNTIME_DLL}"
                    "$<TARGET_FILE_DIR:scn_session_tests>"
                COMMENT "Deploying source runtime for session tests: ${SCN_SOURCE_RUNTIME_DLL}"
                VERBATIM)
        endif()
    endforeach()
endif()

add_executable(scn_policy_tests PolicyTests.cpp)
target_link_libraries(scn_policy_tests PRIVATE scn_application)
add_test(NAME scn_policy COMMAND scn_policy_tests)
set_tests_properties(scn_policy PROPERTIES TIMEOUT 30)

add_executable(scn_policy_storage_tests PolicyStorageTests.cpp)
target_link_libraries(scn_policy_storage_tests PRIVATE scn_application Qt6::Core Qt6::Sql)
add_test(NAME scn_policy_storage COMMAND scn_policy_storage_tests)
set_tests_properties(scn_policy_storage PROPERTIES TIMEOUT 30)
if(WIN32)
    add_custom_command(TARGET scn_policy_storage_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::Core> $<TARGET_FILE_DIR:scn_policy_storage_tests>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::Sql> $<TARGET_FILE_DIR:scn_policy_storage_tests>
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:scn_policy_storage_tests>/sqldrivers
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT6_INSTALL_PREFIX}/plugins/sqldrivers/qsqlited.dll"
            "$<TARGET_FILE_DIR:scn_policy_storage_tests>/sqldrivers/qsqlited.dll"
        COMMENT "Deploying Qt SQL runtime for policy storage tests" VERBATIM)
endif()
