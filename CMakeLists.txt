cmake_minimum_required(VERSION 3.22)

project(
    Snack
    VERSION 0.1.0
    DESCRIPTION "A local-first desktop workbench for coding agents"
    HOMEPAGE_URL "https://github.com/www666www6661/snack"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(SNACK_BUILD_TESTS "Build Snack tests" ON)
option(SNACK_ENABLE_COVERAGE "Enable LLVM source-based coverage" OFF)
option(SNACK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(
    SNACK_DEPLOY_QT_AFTER_BUILD
    "Deploy the Qt runtime beside Snack after Windows development builds"
    ${WIN32}
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Sql Widgets LinguistTools)

include(SnackCoverage)
include(SnackWarnings)
include(GNUInstallDirs)

add_subdirectory(src)

if(SNACK_BUILD_TESTS)
    include(CTest)
    enable_testing()
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
