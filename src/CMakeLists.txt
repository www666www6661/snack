set(SNACK_CORE_SOURCES
    agent/AgentRuntime.cpp
    agent/AgentRuntime.h
    agent/claude/ClaudeAdapter.cpp
    agent/claude/ClaudeAdapter.h
    agent/claude/ClaudeCliDiscovery.cpp
    agent/claude/ClaudeCliDiscovery.h
    agent/claude/ClaudeStreamClient.cpp
    agent/claude/ClaudeStreamClient.h
    agent/claude/ClaudeStreamProtocol.cpp
    agent/claude/ClaudeStreamProtocol.h
    agent/codex/CodexAdapter.cpp
    agent/codex/CodexAdapter.h
    agent/codex/CodexAccountLifecycle.cpp
    agent/codex/CodexAccountLifecycle.h
    agent/codex/CodexApprovalLifecycle.cpp
    agent/codex/CodexApprovalLifecycle.h
    agent/codex/CodexUserInputLifecycle.cpp
    agent/codex/CodexUserInputLifecycle.h
    agent/codex/CodexAppServerClient.cpp
    agent/codex/CodexAppServerClient.h
    agent/codex/CodexCliDiscovery.cpp
    agent/codex/CodexCliDiscovery.h
    agent/codex/CodexModelCatalog.cpp
    agent/codex/CodexModelCatalog.h
    agent/codex/CodexProtocol.cpp
    agent/codex/CodexProtocol.h
    agent/codex/CodexThreadLifecycle.cpp
    agent/codex/CodexThreadLifecycle.h
    agent/codex/CodexTurnLifecycle.cpp
    agent/codex/CodexTurnLifecycle.h
    agent/FakeAgentAdapter.cpp
    agent/FakeAgentAdapter.h
    agent/IAgentAdapter.h
    agent/process/IProcessTransport.h
    agent/process/QProcessTransport.cpp
    agent/process/QProcessTransport.h
    app/AppSettings.cpp
    app/AppSettings.h
    app/ConversationBootstrap.cpp
    app/ConversationBootstrap.h
    app/ConversationExporter.cpp
    app/ConversationExporter.h
    app/Logging.cpp
    app/Logging.h
    app/SessionManager.cpp
    app/SessionManager.h
    app/SingleInstanceGuard.cpp
    app/SingleInstanceGuard.h
    app/WorkspaceFileIndex.cpp
    app/WorkspaceFileIndex.h
    app/WorkspacePathPolicy.cpp
    app/WorkspacePathPolicy.h
    app/WorkspaceWatcher.cpp
    app/WorkspaceWatcher.h
    app/WorkspaceSymbolIndex.cpp
    app/WorkspaceSymbolIndex.h
    workspace/WorkspaceSnapshot.cpp
    workspace/WorkspaceSnapshot.h
    workspace/IsolationCapability.cpp
    workspace/IsolationCapability.h
    workspace/WorkspaceDiff.cpp
    workspace/WorkspaceDiff.h
    workspace/WorkspaceChangeReview.cpp
    workspace/WorkspaceChangeReview.h
    workspace/WorkspaceWriteLease.cpp
    workspace/WorkspaceWriteLease.h
    terminal/ITerminalProcess.h
    terminal/TerminalSession.cpp
    terminal/TerminalSession.h
    terminal/NativeTerminalProcess.h
    terminal/TerminalOutputDecoder.cpp
    terminal/TerminalOutputDecoder.h
    terminal/TerminalTextBuffer.cpp
    terminal/TerminalTextBuffer.h
    app/WorkspaceFilePreview.cpp
    app/WorkspaceFilePreview.h
    app/WorkspaceExternalOpener.cpp
    app/WorkspaceExternalOpener.h
    domain/DomainTypes.cpp
    domain/DomainTypes.h
    domain/PromptTemplateEngine.cpp
    domain/PromptTemplateEngine.h
    session/SessionController.cpp
    session/SessionController.h
    session/SessionRuntimeRegistry.cpp
    session/SessionRuntimeRegistry.h
    storage/ContentStore.cpp
    storage/ContentStore.h
    storage/EventRepository.h
    storage/EventStore.cpp
    storage/EventStore.h
    ui/ThemeDefinition.cpp
    ui/ThemeDefinition.h
)

if(WIN32)
    list(APPEND SNACK_CORE_SOURCES terminal/NativeTerminalProcess_win.cpp)
else()
    list(APPEND SNACK_CORE_SOURCES terminal/NativeTerminalProcess_posix.cpp)
endif()

add_library(snack_core STATIC ${SNACK_CORE_SOURCES})
add_library(Snack::Core ALIAS snack_core)

target_include_directories(snack_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(snack_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql)
if(UNIX AND NOT APPLE)
    target_link_libraries(snack_core PUBLIC util)
endif()
target_compile_definitions(
    snack_core
    PUBLIC
        SNACK_VERSION="${PROJECT_VERSION}"
        SNACK_ORGANIZATION="www666www6661"
        SNACK_APPLICATION_ID="io.github.www666www6661.snack"
)
snack_set_project_warnings(snack_core)

add_library(
    snack_ui STATIC
    ui/ComposerTextEdit.cpp
    ui/ComposerTextEdit.h
    ui/DesktopNotifier.cpp
    ui/DesktopNotifier.h
    ui/MainWindow.cpp
    ui/MainWindow.h
    ui/RichTextPolicy.cpp
    ui/RichTextPolicy.h
    ui/RichTextView.cpp
    ui/RichTextView.h
    ui/TerminalPane.cpp
    ui/TerminalPane.h
    ui/TerminalTabs.cpp
    ui/TerminalTabs.h
    ui/TerminalView.cpp
    ui/TerminalView.h
)
add_library(Snack::Ui ALIAS snack_ui)
target_include_directories(snack_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(snack_ui PUBLIC Snack::Core Qt6::Widgets)
target_compile_definitions(snack_ui PUBLIC SNACK_HAS_WEBENGINE=$<BOOL:${SNACK_HAS_WEBENGINE}>)
if(SNACK_HAS_WEBENGINE)
    target_link_libraries(snack_ui PUBLIC Qt6::WebChannel Qt6::WebEngineWidgets)
    file(GLOB_RECURSE SNACK_RENDERER_ASSETS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/renderer/*"
    )
    qt_add_resources(
        snack_ui renderer_assets
        PREFIX "/renderer"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/renderer"
        FILES ${SNACK_RENDERER_ASSETS}
    )
endif()
snack_set_project_warnings(snack_ui)

qt_add_executable(
    snack
    WIN32
    MACOSX_BUNDLE
    app/main.cpp
    resources/snack.qrc
)

target_link_libraries(snack PRIVATE Snack::Ui)
snack_set_project_warnings(snack)

set_target_properties(
    snack
    PROPERTIES
        OUTPUT_NAME "snack"
        MACOSX_BUNDLE_BUNDLE_NAME "Snack"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.www666www6661.snack"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)

qt_add_translations(
    snack
    TS_FILES
        "${CMAKE_SOURCE_DIR}/translations/snack_zh_CN.ts"
    RESOURCE_PREFIX "/i18n"
)

install(
    TARGETS snack
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/snack")

set(_snack_deploy_tool_options
    --no-system-dxc-compiler
    --include-plugins qsqlite
    --exclude-plugins qsqlmimer,qsqlodbc,qsqlpsql
)
set(_snack_post_build_options ${_snack_deploy_tool_options})
set(_snack_deploy_script_options)

# Qt's deployment tool cannot identify the runtime DLLs used by the official
# LLVM-MinGW package, so copy those explicitly for that development profile.
if(WIN32 AND MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    list(APPEND _snack_post_build_options --no-compiler-runtime)
    list(APPEND _snack_deploy_script_options NO_COMPILER_RUNTIME)

    get_filename_component(_snack_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_snack_qt_prefix "${Qt6Core_DIR}/../../.." ABSOLUTE)
    foreach(_snack_runtime_name IN ITEMS libc++.dll libunwind.dll)
        string(MAKE_C_IDENTIFIER "${_snack_runtime_name}" _snack_runtime_id)
        find_file(
            _snack_runtime_${_snack_runtime_id}
            NAMES "${_snack_runtime_name}"
            HINTS "${_snack_qt_prefix}/bin" "${_snack_compiler_bin}"
            NO_DEFAULT_PATH
        )
        if(NOT _snack_runtime_${_snack_runtime_id})
            message(FATAL_ERROR "Cannot find required LLVM-MinGW runtime: ${_snack_runtime_name}")
        endif()
        list(APPEND _snack_llvm_mingw_runtimes "${_snack_runtime_${_snack_runtime_id}}")
    endforeach()
endif()

if(WIN32 AND SNACK_DEPLOY_QT_AFTER_BUILD)
    add_custom_command(
        TARGET snack
        POST_BUILD
        COMMAND
            "$<TARGET_FILE:Qt6::windeployqt>"
            "$<TARGET_FILE:snack>"
            --dir "$<TARGET_FILE_DIR:snack>"
            --no-translations
            --force
            ${_snack_post_build_options}
        COMMENT "Deploying the Qt runtime beside snack"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    if(_snack_llvm_mingw_runtimes)
        add_custom_command(
            TARGET snack
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                ${_snack_llvm_mingw_runtimes}
                "$<TARGET_FILE_DIR:snack>"
            COMMENT "Deploying the LLVM-MinGW runtime beside snack"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    endif()
endif()

qt_generate_deploy_app_script(
    TARGET snack
    OUTPUT_SCRIPT snack_deploy_script
    NO_TRANSLATIONS
    NO_UNSUPPORTED_PLATFORM_ERROR
    ${_snack_deploy_script_options}
    DEPLOY_TOOL_OPTIONS ${_snack_deploy_tool_options}
)
install(SCRIPT "${snack_deploy_script}")

if(_snack_llvm_mingw_runtimes)
    install(FILES ${_snack_llvm_mingw_runtimes} DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
