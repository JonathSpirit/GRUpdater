cmake_minimum_required(VERSION 3.23)
project(GRUpdater)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(OpenSSL REQUIRED)

set(CMAKE_DEBUG_POSTFIX "_d")
set(CMAKE_RELEASE_POSTFIX "")

#Executable
add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE user32 ws2_32 winmm crypt32)
target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
target_include_directories(${PROJECT_NAME} PRIVATE extern/includes)

target_compile_options(${PROJECT_NAME} PRIVATE -Wpedantic -Wall -Wextra)

#Dependencies
#add_custom_target(copyAllDeps ALL)
#add_dependencies(${PROJECT_NAME} copyAllDeps)

if(WIN32)
    set(LINK_LIBRARIES "OpenSSL::SSL;OpenSSL::Crypto")

    foreach (LINK_LIBRARY ${LINK_LIBRARIES})
        add_custom_command(TARGET ${PROJECT_NAME} PRE_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${LINK_LIBRARY}>
                $<TARGET_FILE_DIR:${PROJECT_NAME}>)
    endforeach()
endif()