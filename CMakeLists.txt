#specify the minimum required version of cmake
# a version below that will make cmake fail
cmake_minimum_required(VERSION 2.6)

#name of my project
project(fileparser_plugin)

include(common/FindScorep.cmake)
if(SCOREP_FOUND)
    include_directories(${SCOREP_INCLUDE_DIRS})
    link_directories(${SCOREP_LIBRARY_DIRS})
    SET(CMAKE_INSTALL_PREFIX ${SCOREP_ROOT_PATH} CACHE PATH "Install path prefix, prepended onto install directories." FORCE)
else()
    message(SEND_ERROR "Scorep was not found but is required")
endif()



#adds the gcc option to insert entry points to the compiler flags
#probably not required if we specify add_library with SHARED
#add_definitions(-fPIC)

add_library(${PROJECT_NAME} SHARED fileparser_plugin.c measurement_blob.c vector.c)
target_link_libraries(${PROJECT_NAME} pthread)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION lib)
