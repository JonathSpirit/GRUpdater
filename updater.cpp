#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"
#include "updater.hpp"
#include <zip.h>

namespace updater
{

const char* ToString(TagStatus status)
{
    switch (status)
    {
    case TagStatus::SameTag:
        return "SameTag";
    case TagStatus::NewerTag:
        return "NewerTag";
    case TagStatus::OlderTag:
        return "OlderTag";
    default:
        return "Unknown";
    }
}
std::optional<TagStatus> FromString(std::string const& status)
{
    if (status == "SameTag")
    {
        return TagStatus::SameTag;
    }
    if (status == "NewerTag")
    {
        return TagStatus::NewerTag;
    }
    if (status == "OlderTag")
    {
        return TagStatus::OlderTag;
    }
    return std::nullopt;
}

std::optional<Tag> ParseTag(std::string const& tag)
{
    Tag result{};
    if (sscanf(tag.c_str(), "v%u.%u.%u", &result.major, &result.minor, &result.patch) == 3)
    {
        return result;
    }
    if (sscanf(tag.c_str(), "%u.%u.%u", &result.major, &result.minor, &result.patch) == 3)
    {
        return result;
    }
    return std::nullopt;
}

std::optional<RepoContext> RetrieveContext(std::string const& owner, std::string const& repo, bool allowPrerelease)
{
    using namespace httplib;

    if (owner.empty() || repo.empty())
    {
        return std::nullopt;
    }

    Client cli("https://api.github.com");

    Headers headers = {
        { "Accept", "application/vnd.github+json" },
        { "X-GitHub-Api-Version", "2022-11-28" }
    };

    if (auto res = cli.Get("/repos/" + owner + "/" + repo + "/releases?per_page=1", headers))
    {
        if (res->status != StatusCode::OK_200)
        {
            return std::nullopt;
        }

        nlohmann::json json = nlohmann::json::parse(res->body);
        if (json.empty() && !json.is_array())
        {
            return std::nullopt;
        }

        bool prerelease = json[0]["prerelease"];

        if (!allowPrerelease && prerelease)
        {
            return std::nullopt;
        }

        std::string tag_name = json[0]["tag_name"];

        auto tag = ParseTag(tag_name);
        if (!tag)
        {
            return std::nullopt;
        }

		auto assets = json[0]["assets"];
        if (assets.empty() && !assets.is_array())
        {
            return std::nullopt;
        }

        for (auto const& asset : assets)
        {
            std::string asset_name = asset["name"];
            std::string asset_name_lower = asset_name;
            std::ranges::transform(asset_name_lower, asset_name_lower.begin(), ::tolower);

            if (asset_name_lower.find("windows") == std::string::npos)
            {
                continue;
            }

            if constexpr (sizeof(void*) == 8)
            {
                if (asset_name_lower.find("64") == std::string::npos)
                {
                    continue;
                }
            }
            else
            {
                if (asset_name_lower.find("32") == std::string::npos)
                {
                    continue;
                }
            }

            std::string content_type = asset["content_type"];
            if (content_type != "application/x-zip-compressed")
            {
                continue;
            }

            std::string asset_url = asset["browser_download_url"];

            RepoContext context;
            context._owner = owner;
            context._repo = repo;
            context._asset = std::move(asset_name);
            context._assetUrl = std::move(asset_url);
            context._latestTag = tag.value();
            return context;
        }
    }
    return std::nullopt;
}
TagStatus VerifyTag(RepoContext const& context, Tag const& currentTag)
{
	if (context._latestTag.major == currentTag.major)
    {
        if (context._latestTag.minor == currentTag.minor)
        {
            if (context._latestTag.patch == currentTag.patch)
            {
                return TagStatus::SameTag;
            }
            if (context._latestTag.patch > currentTag.patch)
            {
                return TagStatus::NewerTag;
            }
            return TagStatus::OlderTag;
        }
        if (context._latestTag.minor > currentTag.minor)
        {
            return TagStatus::NewerTag;
        }
        return TagStatus::OlderTag;
    }
    if (context._latestTag.major > currentTag.major)
    {
        return TagStatus::NewerTag;
    }
    return TagStatus::OlderTag;
}

std::optional<std::filesystem::path> DownloadAsset(RepoContext const& context, std::filesystem::path const& tempDir)
{
	using namespace httplib;

    if (context._assetUrl.empty())
    {
        return std::nullopt;
    }

    if (tempDir.empty() || !tempDir.is_relative())
    {
        return std::nullopt;
    }

    //Check if tempDir is inside working directory
    auto currentPath = std::filesystem::current_path();
    auto fullPath = std::filesystem::absolute(tempDir);
    auto relativePath = std::filesystem::relative(fullPath, currentPath) / "";

    if (relativePath != tempDir || currentPath == fullPath)
    {
        return std::nullopt;
    }

    if (!std::filesystem::exists(tempDir))
    {
        if (!std::filesystem::create_directories(tempDir))
        {
            return std::nullopt;
        }
    }
    else if (!std::filesystem::is_directory(tempDir))
    {
        return std::nullopt;
    }

    if (!std::filesystem::is_empty(tempDir))
    {
        std::filesystem::remove_all(tempDir);
        if (!std::filesystem::create_directories(tempDir))
        {
            return std::nullopt;
        }
    }

    Client cli("https://github.com");
    cli.set_follow_location(true);

    if (auto res = cli.Get(context._assetUrl))
    {
        if (res->status != StatusCode::OK_200)
        {
            return std::nullopt;
        }

        std::filesystem::path assetPath = tempDir / context._asset;
        std::ofstream file(assetPath, std::ios::binary);
        file << res->body;
        file.close();
        return assetPath;
    }
    return std::nullopt;
}
std::optional<std::filesystem::path> ExtractAsset(std::filesystem::path const& assetPath)
{
    if (!std::filesystem::exists(assetPath) || !std::filesystem::is_regular_file(assetPath))
    {
        return std::nullopt;
    }
    if (assetPath.extension() != ".zip")
    {
        return std::nullopt;
    }

    auto parentPath = assetPath.parent_path();

    int err;

    auto assetPathStr = assetPath.string();
    auto* zip = zip_open(assetPathStr.c_str(), 0, &err);
    if (zip == nullptr)
    {
        zip_error error{};
        zip_error_init_with_code(&error, err);
        auto buffer = zip_error_strerror(&error);
        std::cerr << "Failed to open zip archive " << assetPathStr << ": " << buffer << '\n';
        return std::nullopt;
    }

    bool isRootPathValid = false;
    std::filesystem::path extractRootPath{};

    for (zip_int64_t i = 0; i < zip_get_num_entries(zip, 0); ++i)
    {
        struct zip_stat zipStat{};
        if (zip_stat_index(zip, i, 0, &zipStat) != 0)
        {
            std::cerr << "Failed to get stat index " << i << " in zip archive " << assetPathStr << '\n';
            zip_close(zip);
            return std::nullopt;
        }

        auto extractFilePath = std::filesystem::path{zipStat.name};
        auto filePath = parentPath / extractFilePath;
        std::cout << "Name: ["<< extractFilePath <<"], ";
        std::cout << "Size: ["<< zipStat.size <<"], ";
        std::cout << "mtime: ["<< zipStat.mtime <<"]\n";

        if (extractFilePath.begin() != extractFilePath.end() && *extractFilePath.begin() != extractRootPath)
        {
            if (extractRootPath.empty())
            {
                extractRootPath = *extractFilePath.begin();
                isRootPathValid = true;
            }
            else if (!isRootPathValid)
            {
                isRootPathValid = false; //Must be unique
            }
        }

        std::error_code errorCode;
        std::filesystem::create_directories(filePath.parent_path(), errorCode);

        if (errorCode.value() != 0)
        {
            std::cerr << "Failed to create directory " << filePath.parent_path() << " " << errorCode.message() << '\n';
            zip_close(zip);
            return std::nullopt;
        }

        if (zipStat.name[std::strlen(zipStat.name) - 1] == '/')
        {
            continue;
        }

        zip_file* zipFile = zip_fopen_index(zip, i, 0);
        if (zipFile == nullptr)
        {
            std::cerr << "Failed to open index " << i << " in zip archive " << assetPathStr << '\n';
            zip_close(zip);
            return std::nullopt;
        }

        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            std::cerr << "Failed to create file " << filePath << '\n';
            zip_fclose(zipFile);
            zip_close(zip);
            return std::nullopt;
        }

        std::array<char, 100> buffer{};
        zip_uint64_t total = 0;
        while (total != zipStat.size)
        {
            auto const dataSize = zip_fread(zipFile, buffer.data(), buffer.size());
            if (dataSize < 0)
            {
                std::cerr << "Failed to read data from zip archive " << assetPathStr << '\n';
                zip_fclose(zipFile);
                zip_close(zip);
                return std::nullopt;
            }
            file.write(buffer.data(), dataSize);
            total += dataSize;
        }
        file.close();
        zip_fclose(zipFile);
    }

    zip_close(zip);

    if (!isRootPathValid)
    {
        return assetPath.parent_path();
    }
    return assetPath.parent_path() / extractRootPath;
}

}//namespace updater
