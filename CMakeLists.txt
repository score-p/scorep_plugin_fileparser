cmake_minimum_required(VERSION 3.9)

project(fileparser_plugin)

include(cmake/DefaultBuildType.cmake)
include(cmake/GitSubmoduleUpdate.cmake)
git_submodule_update()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/common")

find_package(Scorep REQUIRED)
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
find_package(Threads REQUIRED)

add_library(${PROJECT_NAME} MODULE fileparser_plugin.c measurement_blob.c vector.c)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        Threads::Threads
    PRIVATE
        Scorep::Plugin
)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -pedantic -Wextra)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION lib)
