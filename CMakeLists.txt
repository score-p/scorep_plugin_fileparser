#specify the minimum required version of cmake
# a version below that will make cmake fail
cmake_minimum_required(VERSION 2.6)

#name of my project
project(fileparser_plugin)

#adds the gcc option to insert entry points to the compiler flags
add_definitions(-fPIC)


include_directories(/opt/scorep/include)
link_directories(/opt/scorep/include)
add_library(${PROJECT_NAME} SHARED fileparser_plugin.c)
target_link_libraries(${PROJECT_NAME} pthread)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION lib)
