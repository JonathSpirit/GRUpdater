# GRUpdater
MIT License
Copyright (c) 2026 Guillaume Guillet

GRUpdater is a C++ library and command-line tool for downloading and applying software updates from a GitHub release.

It checks the latest release for a repository, compares version tags, downloads the matching archive, extracts it, and then replaces the installed application files with the new version.

## Platform support

GRUpdater supports both Windows and Linux.

- Windows executable name: `GRUpdaterCmd.exe`
- Linux executable name: `GRUpdaterCmd`

When publishing releases, provide a `.zip` asset that includes the platform and architecture in its filename (for example `windows-x64` or `linux-x64`) so GRUpdater can select the correct artifact.

## Project files

- `updater.hpp`: public API
- `updater.cpp`: core update logic
- `main.cpp`: command-line interface
- `CMakeLists.txt`: build configuration
- `extern/includes`: bundled dependencies (`CLI11.hpp`, `httplib.h`, `json.hpp`)

## Build

Requirements:
- C++20 compiler
- CMake
- OpenSSL
- libzip

Tested with MSVC on Windows and GCC/Clang on Linux.

Example:

```bash
cmake -S . -B build
cmake --build build

or

mkdir build && cd build
cmake ..
cmake --build .
```

The build produces:
- a shared library for the updater
- a command-line executable for testing and update operations

## Integration requirements

To use GRUpdater in your application, you must:

1. Link the `GRUpdater` library to your application executable.
2. Place the `GRUpdaterCmd` executable next to your app executable.
3. Keep the updater executable in the same application folder as the main binary so it can be launched during the update process.
4. Call `updater::MakeAvailable()` or `updater::RequestApplyUpdate()` from your app when you want to check for and apply an update.

The library is the logic layer; the CLI executable is the helper used to trigger the replacement flow after the user confirms the update.

## Practical example with SDL2

```cpp
#include <filesystem>
#include <iostream>
#include <SDL.h>
#include "updater.hpp"

constexpr const char* F_OWNER = "YourUser";
constexpr const char* F_REPO = "YourRepo";
constexpr const char* F_TAG = "v1.2.3";
const std::filesystem::path F_TEMP_DIR = "./temp";

int main(int argc, char** argv)
{
    std::filesystem::remove_all(F_TEMP_DIR);

    if (auto const extractPath = updater::MakeAvailable(F_TAG, F_OWNER, F_REPO, F_TEMP_DIR, true))
    {
        SDL_MessageBoxData messageBoxData;
        messageBoxData.flags = SDL_MESSAGEBOX_INFORMATION;
        messageBoxData.window = nullptr;
        messageBoxData.title = "Game update";
        messageBoxData.message = "A new version of the game is available, do you want to update now? (from GitHub)";

        SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Yes"},
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "No"},
        };

        messageBoxData.numbuttons = std::size(buttons);
        messageBoxData.buttons = buttons;

        int buttonId = 1;
        if (SDL_ShowMessageBox(&messageBoxData, &buttonId) < 0)
        {
            std::cout << "Error displaying message box: " << SDL_GetError() << std::endl;
        }
        else if (buttonId == 1)
        {
            std::cout << "Update cancelled" << std::endl;
        }
        else
        {
            std::cout << "Updating game..." << std::endl;
            if (updater::RequestApplyUpdate(*extractPath, std::filesystem::current_path() / argv[0]))
            {
                return 0;
            }
        }
    }

    return 0;
}
```

This pattern is simple:
- the app checks for a newer release
- a message box asks the user if they want the update
- if they accept, the app asks the extracted updater to replace the installed files
- the app exits so the update can complete cleanly

## Practical example without GUI dependencies

```cpp
#include <filesystem>
#include <iostream>
#include "updater.hpp"

constexpr const char* F_OWNER = "YourUser";
constexpr const char* F_REPO = "YourRepo";
constexpr const char* F_TAG = "v1.2.3";
const std::filesystem::path F_TEMP_DIR = "./temp";

int main(int argc, char** argv)
{
    std::filesystem::remove_all(F_TEMP_DIR);

    if (auto const extractPath = updater::MakeAvailable(F_TAG, F_OWNER, F_REPO, F_TEMP_DIR, true))
    {
        std::cout << "A new version is available. Update now? [y/N]" << std::endl;
        char answer = 'n';
        std::cin >> answer;

        if (answer == 'y' || answer == 'Y')
        {
            if (updater::RequestApplyUpdate(*extractPath, std::filesystem::current_path() / argv[0]))
            {
                return 0;
            }
        }
    }

    return 0;
}
```

This version does the same job without dependencies: it checks for an update, asks the user, and then starts the replacement flow.

## How it works

1. The app retrieves the latest release metadata for a GitHub repository.
2. It filters the available release assets and selects the archive matching the current platform and architecture.
3. It parses the release tag and compares it with the local application version.
4. If the remote tag is newer, it downloads the archive into a temporary folder.
5. It extracts the archive and identifies the root folder of the update.
6. It starts the updater flow from the extracted files.
7. The updater waits for the current process to exit, removes obsolete files, copies the new files, and tries to relaunch the application.

## Update flow

### 1. Retrieve release metadata
The updater reads the release metadata from GitHub and collects:
- repository owner
- repository name
- asset name
- asset download URL
- latest release tag

The asset selection logic looks for:
- a zip-compressed release asset
- a platform-compatible archive
- an architecture-compatible asset

### 2. Compare versions
The updater compares the remote version with the current local version.

Only a newer release is considered valid for installation.

### 3. Download and extract
The archive is saved in a temporary directory and then extracted.

The extraction step detects whether the archive contains a root folder or whether files are placed directly in the extracted directory.

### 4. Apply the update
The updater launches the replacement process with:
- the target installation directory
- the caller executable path
- the caller process identifier

Then it:
- waits for the running app to close
- removes obsolete files that are not protected
- copies the new files into the target directory
- tries to relaunch the application

## CLI usage

### Version check
```bash
GRUpdaterCmd --version
```

### Fetch and validate a release
```bash
GRUpdaterCmd fetch \
  --current v1.2.3 \
  -o YourUser \
  -r YourRepo \
  --verify \
  --download \
  --extract \
  -t ./temp/
```

### Local test mode
```bash
GRUpdaterCmd fetch \
  --current v1.2.3 \
  --localPath ./test-release \
  --verify \
  --download \
  --extract \
  -t ./temp/
```

### Request the update from the running app
```bash
GRUpdaterCmd requestApply \
  -r /path/to/extracted/update \
  --caller /path/to/your/app
```

### Apply the update
```bash
GRUpdaterCmd apply \
  -t /path/to/installed/app \
  --pid 12345 \
  --caller /path/to/your/app
```

## Runtime details

- `schedule.json` stores the last check time to avoid repeated GitHub API requests.
- `dynamicFiles.json` marks files that must not be removed during the update.
- The updater is built to work with a running application that needs to restart after replacement.
- The downloaded release asset must be a valid archive for the current environment.
