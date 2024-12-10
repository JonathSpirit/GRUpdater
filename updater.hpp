#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <filesystem>

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
#define GRUPDATER_TAG_MINOR 1
#define GRUPDATER_TAG_PATCH 0
#define GRUPDATER_STRINGIFY(_x) #_x
#define GRUPDATER_TOSTRING(_x) GRUPDATER_STRINGIFY(_x)
#define GRUPDATER_TAG_STR "v" GRUPDATER_TOSTRING(GRUPDATER_TAG_MAJOR) "." GRUPDATER_TOSTRING(GRUPDATER_TAG_MINOR) "." GRUPDATER_TOSTRING(GRUPDATER_TAG_PATCH)
#define GRUPDATER_TAG updater::Tag{GRUPDATER_TAG_MAJOR, GRUPDATER_TAG_MINOR, GRUPDATER_TAG_PATCH}

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

}//namespace updater
