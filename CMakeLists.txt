cmake_minimum_required(VERSION 3.23)
project(GRUpdater)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(OPENSSL_USE_STATIC_LIBS ON)
find_package(OpenSSL REQUIRED)
set(IS_SHARED OFF)
find_package(libzip REQUIRED)

set(CMAKE_DEBUG_POSTFIX "_d")
set(CMAKE_RELEASE_POSTFIX "")

#Library
add_library(${PROJECT_NAME} SHARED)
target_sources(${PROJECT_NAME} PRIVATE updater.cpp)
target_sources(${PROJECT_NAME} PUBLIC FILE_SET HEADERS FILES updater.hpp)

target_compile_definitions(${PROJECT_NAME} PRIVATE _UPDATER_DEF_BUILDDLL)
target_link_libraries(${PROJECT_NAME} PRIVATE user32 ws2_32 winmm crypt32)
target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(${PROJECT_NAME} PRIVATE libzip::zip)
target_include_directories(${PROJECT_NAME} PRIVATE extern/includes)

target_compile_options(${PROJECT_NAME} PRIVATE -Wpedantic -Wall -Wextra)

#Executable
add_executable(${PROJECT_NAME}Cmd main.cpp)
target_link_libraries(${PROJECT_NAME}Cmd PRIVATE ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME}Cmd PRIVATE extern/includes)

if(WIN32)
    set(LINK_LIBRARIES "OpenSSL::SSL;OpenSSL::Crypto;libzip::zip")

    foreach (LINK_LIBRARY ${LINK_LIBRARIES})
        add_custom_command(TARGET ${PROJECT_NAME}Cmd PRE_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${LINK_LIBRARY}>
                $<TARGET_FILE_DIR:${PROJECT_NAME}Cmd>)
    endforeach()
endif()