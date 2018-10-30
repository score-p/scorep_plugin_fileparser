cmake_minimum_required(VERSION 3.9)

project(fileparser_plugin VERSION 1.3.1)

set (CMAKE_CXX_STANDARD 11)

execute_process(
  COMMAND 
    "git" "submodule" "update" "--init" "--recursive"
  WORKING_DIRECTORY
  "${CMAKE_SOURCE_DIR}"
  RESULT_VARIABLE result
  ERROR_VARIABLE error
)


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

find_program(ARCHIVE_GIT archive-git ${CMAKE_SOURCE_DIR})
if(ARCHIVE_GIT)
    set(ARCHIVE_NAME ${CMAKE_PROJECT_NAME}-${PROJECT_VERSION})
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/${ARCHIVE_NAME}.tar.gz
        COMMAND ./archive-git ${CMAKE_BINARY_DIR}/${ARCHIVE_NAME}.tar.gz
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    add_custom_target(dist DEPENDS ${CMAKE_BINARY_DIR}/${ARCHIVE_NAME}.tar.gz)
else()
    message(STATUS "Couldn't find ./archive-git.")
endif()

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION lib)
