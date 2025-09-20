# GRUpdater

MIT License
Copyright (c) 2024 Guilllaume Guillet

## Description

This is a C++ application in order to automatically update an app from a Github release.

## Dependencies

A C++ 20 compiler, OpenSSL, and libzip. Supports both Windows and Linux.

### Linux Requirements
- g++ or clang++ with C++20 support
- cmake (>= 3.23)
- libzip-dev
- libssl-dev
- pkg-config

### Windows Requirements  
- Visual Studio or MinGW with C++20 support
- cmake (>= 3.23)
- OpenSSL
- libzip

## Building

### Linux
```bash
mkdir build && cd build
cmake ..
make
```

### Windows
```cmd
mkdir build && cd build
cmake ..
cmake --build .
```

## Step

- Link the DLL/SO library to your program.
- Verify if a newer tag is available from your program.
- Download and extract the release
- Call RequestApplyUpdate() and close your program.
- It should be done !

(TODO): better docs :)

