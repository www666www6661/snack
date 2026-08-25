if(NOT DEFINED APP_PATH OR NOT EXISTS "${APP_PATH}")
    message(FATAL_ERROR "Snack executable does not exist: ${APP_PATH}")
endif()

get_filename_component(app_directory "${APP_PATH}" DIRECTORY)
set(required_files
    Qt6Core.dll
    Qt6Gui.dll
    Qt6Network.dll
    Qt6Sql.dll
    Qt6Widgets.dll
    platforms/qwindows.dll
    sqldrivers/qsqlite.dll
)

if(EXPECT_LLVM_MINGW_RUNTIME)
    list(APPEND required_files libc++.dll libunwind.dll)
endif()

foreach(relative_path IN LISTS required_files)
    if(NOT EXISTS "${app_directory}/${relative_path}")
        message(FATAL_ERROR "Missing deployed runtime file: ${relative_path}")
    endif()
endforeach()
