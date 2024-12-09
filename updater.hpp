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
