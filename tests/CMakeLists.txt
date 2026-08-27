function(snack_add_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} PRIVATE Snack::Core Qt6::Test)
    snack_set_project_warnings(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 20)
endfunction()

snack_add_test(snack_domain_tests domain/TestDomainTypes.cpp)
snack_add_test(snack_settings_tests app/TestAppSettings.cpp)
snack_add_test(snack_conversation_bootstrap_tests app/TestConversationBootstrap.cpp)
snack_add_test(snack_session_manager_tests app/TestSessionManager.cpp)
snack_add_test(snack_single_instance_tests app/TestSingleInstance.cpp)
snack_add_test(snack_theme_tests ui/TestThemeDefinition.cpp)
snack_add_test(snack_storage_tests storage/TestStorage.cpp)
snack_add_test(snack_session_tests session/TestSessionController.cpp)
snack_add_test(snack_session_runtime_tests session/TestSessionRuntimeRegistry.cpp)
snack_add_test(snack_agent_runtime_tests agent/TestAgentRuntime.cpp)
snack_add_test(snack_codex_tests agent/TestCodexAppServer.cpp)
target_compile_definitions(
    snack_codex_tests
    PRIVATE SNACK_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_executable(snack_ui_tests ui/TestMainWindow.cpp)
target_link_libraries(snack_ui_tests PRIVATE Snack::Ui Qt6::Test)
snack_set_project_warnings(snack_ui_tests)
add_test(NAME snack_ui_tests COMMAND snack_ui_tests)
set_tests_properties(
    snack_ui_tests
    PROPERTIES
        TIMEOUT 20
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

if(WIN32 AND SNACK_DEPLOY_QT_AFTER_BUILD)
    set(_snack_expect_llvm_mingw_runtime OFF)
    if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(_snack_expect_llvm_mingw_runtime ON)
    endif()

    add_test(
        NAME snack_windows_deployment_tests
        COMMAND
            "${CMAKE_COMMAND}"
            "-DAPP_PATH=$<TARGET_FILE:snack>"
            "-DEXPECT_LLVM_MINGW_RUNTIME=${_snack_expect_llvm_mingw_runtime}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyWindowsDeployment.cmake"
    )
    set_tests_properties(snack_windows_deployment_tests PROPERTIES TIMEOUT 20)
endif()
