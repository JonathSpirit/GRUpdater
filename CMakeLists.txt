cmake_minimum_required(VERSION 3.23)
project(GRUpdater)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(OPENSSL_USE_STATIC_LIBS ON)
find_package(OpenSSL REQUIRED)
set(IS_SHARED OFF)

# Find libzip using PkgConfig for better cross-platform compatibility
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED libzip)

# Create an imported target for libzip
add_library(libzip::zip INTERFACE IMPORTED)
target_link_libraries(libzip::zip INTERFACE ${LIBZIP_LIBRARIES})
target_include_directories(libzip::zip INTERFACE ${LIBZIP_INCLUDE_DIRS})
target_compile_options(libzip::zip INTERFACE ${LIBZIP_CFLAGS_OTHER})

set(CMAKE_DEBUG_POSTFIX "_d")
set(CMAKE_RELEASE_POSTFIX "")

#Options
option(UPDATER_DUMMY_TEST "The update will create a dummy folder instead of a real app env (debug only)" OFF)

#Library
add_library(${PROJECT_NAME} SHARED)

# Platform-specific sources
if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE updater.cpp infodll.rc)
else()
    target_sources(${PROJECT_NAME} PRIVATE updater.cpp)
endif()

target_sources(${PROJECT_NAME} PUBLIC FILE_SET HEADERS FILES updater.hpp)

target_compile_definitions(${PROJECT_NAME} PRIVATE _UPDATER_DEF_BUILDDLL)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE user32 ws2_32 winmm crypt32)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(${PROJECT_NAME} PRIVATE libzip::zip)
target_include_directories(${PROJECT_NAME} PRIVATE extern/includes)

target_compile_options(${PROJECT_NAME} PRIVATE -Wpedantic -Wall -Wextra)

#Executable
add_executable(${PROJECT_NAME}Cmd main.cpp)

# Platform-specific sources for executable
if(WIN32)
    target_sources(${PROJECT_NAME}Cmd PRIVATE infoexe.rc)
endif()

target_link_libraries(${PROJECT_NAME}Cmd PRIVATE ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME}Cmd PRIVATE extern/includes)

if (UPDATER_DUMMY_TEST)
    target_compile_definitions(${PROJECT_NAME} PUBLIC _UPDATER_DEF_DUMMYTEST)
endif()

if(WIN32)
    set(LINK_LIBRARIES "OpenSSL::SSL;OpenSSL::Crypto;libzip::zip")

    foreach (LINK_LIBRARY ${LINK_LIBRARIES})
        add_custom_command(TARGET ${PROJECT_NAME}Cmd PRE_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${LINK_LIBRARY}>
                $<TARGET_FILE_DIR:${PROJECT_NAME}Cmd>)
    endforeach()
endif()