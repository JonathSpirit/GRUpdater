#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <filesystem>
#include <chrono>

#ifndef _WIN32
    #define UPDATER_API
#else
    #ifdef _UPDATER_DEF_BUILDDLL
        #define UPDATER_API __declspec(dllexport)
    #else
        #define UPDATER_API __declspec(dllimport)
    #endif // _UPDATER_DEF_BUILDDLL
#endif     //_WIN32

#define GRUPDATER_TAG_MAJOR 0
#define GRUPDATER_TAG_MINOR 2
#define GRUPDATER_TAG_PATCH 0
#define GRUPDATER_STRINGIFY(_x) #_x
#define GRUPDATER_TOSTRING(_x) GRUPDATER_STRINGIFY(_x)
#define GRUPDATER_TAG_STR "v" GRUPDATER_TOSTRING(GRUPDATER_TAG_MAJOR) "." GRUPDATER_TOSTRING(GRUPDATER_TAG_MINOR) "." GRUPDATER_TOSTRING(GRUPDATER_TAG_PATCH)
#define GRUPDATER_TAG updater::Tag{GRUPDATER_TAG_MAJOR, GRUPDATER_TAG_MINOR, GRUPDATER_TAG_PATCH}

#define GRUPDATER_DEFAULT_SCHEDULE_FILE "./schedule.json"
#define GRUPDATER_DEFAULT_SCHEDULE_DELAY_HOURS 2

#define GRUPDATER_WAIT_PID_TIMEOUT_MS 5000

#define GRUPDATER_EXECUTABLE_NAME "GRUpdaterCmd.exe"
#define GRUPDATER_EXECUTABLE_NAME_W L"GRUpdaterCmd.exe"
#define GRUPDATER_DLL_NAME "libGRUpdater_d.dll"
#define GRUPDATER_DLL_NAME_W L"libGRUpdater_d.dll"

#define GRUPDATER_DEFAULT_DYNAMIC_FILE "./dynamicFiles.json"

namespace updater
{

struct Tag
{
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
};
enum class TagStatus
{
    SameTag,
    NewerTag,
    OlderTag
};
const char* ToString(TagStatus status);
std::optional<TagStatus> FromString(std::string const& status);
struct RepoContext
{
    std::string _owner;
    std::string _repo;
    std::string _asset;
    std::string _assetUrl;
    Tag _latestTag;
};

[[nodiscard]] UPDATER_API std::optional<Tag> ParseTag(std::string const& tag);

[[nodiscard]] UPDATER_API std::optional<RepoContext> RetrieveContext(std::string const& owner, std::string const& repo, bool allowPrerelease = false);
[[nodiscard]] UPDATER_API TagStatus VerifyTag(RepoContext const& context, Tag const& currentTag);

[[nodiscard]] UPDATER_API std::optional<std::filesystem::path> DownloadAsset(RepoContext const& context, std::filesystem::path const& tempDir);
[[nodiscard]] UPDATER_API std::optional<std::filesystem::path> ExtractAsset(std::filesystem::path const& assetPath);

[[nodiscard]] UPDATER_API std::optional<std::chrono::system_clock::time_point> GetScheduleTime(std::filesystem::path const& scheduleFile = GRUPDATER_DEFAULT_SCHEDULE_FILE);
[[nodiscard]] UPDATER_API bool SetScheduleTime(std::filesystem::path const& scheduleFile = GRUPDATER_DEFAULT_SCHEDULE_FILE, std::chrono::system_clock::time_point const& time = std::chrono::system_clock::now());
[[nodiscard]] UPDATER_API bool VerifyScheduleTime(std::chrono::system_clock::time_point const& timePoint, std::chrono::hours const& delay = std::chrono::hours{GRUPDATER_DEFAULT_SCHEDULE_DELAY_HOURS});

//Called from the caller executable
[[nodiscard]] UPDATER_API bool ApplyUpdate(std::filesystem::path const& rootAssetPath, std::filesystem::path const& callerExecutable);

/*
 * MakeAvailable:
 * Will do the following:
 * - Retrieve the schedule time
 * - Verify the schedule time
 * - Set the schedule time
 * - Retrieve the context
 * - Verify the tag
 * - Download the asset
 * - Extract the asset
 * - Return the extracted root
 */
[[nodiscard]] UPDATER_API std::optional<std::filesystem::path> MakeAvailable(Tag const& currentTag,
                                                                             std::string const& owner,
                                                                             std::string const& repo,
                                                                             std::filesystem::path const& tempDir,
                                                                             bool allowPrerelease = false);

}//namespace updater
