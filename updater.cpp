// Copyright (c) 2026. Guillaume Guillet MIT License
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"
#include "updater.hpp"
#include <zip.h>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <csignal>
    #include <cerrno>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

namespace updater
{

namespace
{

bool IsSubDirectory(std::filesystem::path p, std::filesystem::path const& root)
{
    while(!p.empty())
    {
        if (p == root)
        {
            return true;
        }
        p = p.parent_path();
    }
    return false;
}

std::string GetPlatformToken()
{
#if defined(_WIN32)
    return "windows";
#elif defined(__linux__)
    return "linux";
#else
    return "";
#endif
}

bool IsArchitectureMatch(std::string const& assetNameLower)
{
    if constexpr (sizeof(void*) == 8)
    {
        return assetNameLower.find("64") != std::string::npos
            || assetNameLower.find("x64") != std::string::npos
            || assetNameLower.find("amd64") != std::string::npos;
    }
    return assetNameLower.find("32") != std::string::npos
        || assetNameLower.find("x86") != std::string::npos
        || assetNameLower.find("i386") != std::string::npos;
}

bool IsZipContentType(std::string const& contentType)
{
    return contentType == "application/zip"
        || contentType == "application/x-zip-compressed"
        || contentType == "application/octet-stream";
}

std::string ToLower(std::string str)
{
    std::ranges::transform(str, str.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return str;
}

bool IsSha256Hex(std::string const& str)
{
    return str.size() == 64 && std::ranges::all_of(str, [](unsigned char c){ return std::isxdigit(c) != 0; });
}

//Parse a "sha256sum" like content, one entry per line: "<hash>", "<hash>  <name>" or "<hash> *<name>"
//An entry without name is only used if no entry match the asset name
std::optional<std::string> ParseChecksumFile(std::string const& content, std::string const& assetName)
{
    std::optional<std::string> unnamedHash;

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line))
    {
        std::istringstream lineStream(line);
        std::string hash;
        if (!(lineStream >> hash))
        {
            continue;
        }
        hash = ToLower(std::move(hash));
        if (!IsSha256Hex(hash))
        {
            continue;
        }

        std::string name;
        std::getline(lineStream >> std::ws, name);
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
        {
            name.pop_back();
        }
        if (!name.empty() && name.front() == '*')
        {//Binary mode marker
            name.erase(0, 1);
        }

        if (name.empty())
        {
            if (!unnamedHash)
            {
                unnamedHash = std::move(hash);
            }
            continue;
        }

        if (std::filesystem::path{name}.filename().string() == assetName)
        {
            return hash;
        }
    }
    return unnamedHash;
}

//GitHub provide a "digest" field for each asset, formatted as "sha256:<hash>" (can be null for older assets)
std::optional<std::string> ParseAssetDigest(nlohmann::json const& asset)
{
    if (!asset.contains("digest") || !asset["digest"].is_string())
    {
        return std::nullopt;
    }

    constexpr std::string_view prefix = "sha256:";
    auto digest = ToLower(asset["digest"].get<std::string>());
    if (!digest.starts_with(prefix))
    {
        return std::nullopt;
    }
    digest.erase(0, prefix.size());
    if (!IsSha256Hex(digest))
    {
        return std::nullopt;
    }
    return digest;
}

//Look for a "<asset>.sha256" file in the release assets and download it
std::optional<std::string> FetchRemoteChecksumFile(nlohmann::json const& assets, std::string const& assetName)
{
    using namespace httplib;

    auto const checksumAssetName = assetName + GRUPDATER_CHECKSUM_FILE_EXTENSION;
    for (auto const& asset : assets)
    {
        if (!asset.contains("name") || !asset["name"].is_string() || asset["name"].get<std::string>() != checksumAssetName)
        {
            continue;
        }
        if (!asset.contains("browser_download_url") || !asset["browser_download_url"].is_string())
        {
            return std::nullopt;
        }

        Client cli("https://github.com");
        cli.set_follow_location(true);

        auto res = cli.Get(asset["browser_download_url"].get<std::string>());
        if (!res || res->status != StatusCode::OK_200)
        {
            std::cerr << "Failed to download checksum file " << checksumAssetName << '\n';
            return std::nullopt;
        }
        return ParseChecksumFile(res->body, assetName);
    }
    return std::nullopt;
}

bool WaitForProcessToExit(uint32_t pid)
{
#if defined(_WIN32)
    HANDLE processHandle = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (processHandle == nullptr)
    {
        std::cout << "Failed to open process " << pid << '\n';
        return true;
    }

    std::cout << "Waiting for process " << pid << " to finish\n";
    DWORD result = WaitForSingleObject(processHandle, GRUPDATER_WAIT_PID_TIMEOUT_MS);
    CloseHandle(processHandle);
    if (result != WAIT_OBJECT_0)
    {
        std::cerr << "Failed to wait for process " << pid << '\n';
        return false;
    }
    return true;
#else
    std::cout << "Waiting for process " << pid << " to finish\n";
    constexpr std::chrono::milliseconds pollInterval{100};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{GRUPDATER_WAIT_PID_TIMEOUT_MS};
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (kill(static_cast<pid_t>(pid), 0) == -1)
        {
            if (errno == ESRCH)
            {
                return true;
            }
            if (errno == EPERM)
            {
                return false;
            }
        }
        std::this_thread::sleep_for(pollInterval);
    }

    std::cerr << "Failed to wait for process " << pid << '\n';
    return false;
#endif
}

bool SpawnProcessDetached(std::filesystem::path const& executablePath,
                          std::vector<std::string> const& args,
                          std::filesystem::path const& workingDirectory)
{
#if defined(_WIN32)
    std::wstring commandLine;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        if (i > 0)
        {
            commandLine += L' ';
        }
        commandLine += L'\"';
        commandLine += std::filesystem::path{args[i]}.wstring();
        commandLine += L'\"';
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(executablePath.wstring().c_str(), commandLine.empty() ? nullptr : commandLine.data(),
                        nullptr, nullptr, FALSE,
                        CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, nullptr,
                        workingDirectory.wstring().c_str(), &startupInfo, &processInfo))
    {
        return false;
    }
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);
    return true;
#else
    pid_t pid = fork();
    if (pid < 0)
    {
        return false;
    }
    if (pid == 0)
    {
        if (setsid() < 0)
        {
            _exit(127);
        }
        if (chdir(workingDirectory.c_str()) != 0)
        {
            _exit(127);
        }

        std::vector<char*> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(const_cast<char*>(executablePath.c_str()));
        for (auto const& arg : args)
        {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(executablePath.c_str(), argv.data());
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, WNOHANG);
    return true;
#endif
}
}

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
const char* ToString(ChecksumStatus status)
{
    switch (status)
    {
    case ChecksumStatus::Valid:
        return "Valid";
    case ChecksumStatus::Invalid:
        return "Invalid";
    case ChecksumStatus::Unavailable:
        return "Unavailable";
    default:
        return "Unknown";
    }
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

std::optional<RepoContext> RetrieveLocalContext(std::filesystem::path const& localPath, Tag const& localTag)
{
    if (localPath.empty())
    {
        return std::nullopt;
    }

    if (!std::filesystem::exists(localPath))
    {
        return std::nullopt;
    }

    RepoContext context;
    context._owner = "local";
    context._repo = "local";
    context._asset = !localPath.has_filename() ? "" : localPath.filename().string();
    context._assetUrl = localPath.string();
    context._latestTag = localTag;

    //Look for a "<asset>.sha256" file next to the local asset
    if (std::filesystem::is_regular_file(localPath))
    {
        auto checksumPath = localPath;
        checksumPath += GRUPDATER_CHECKSUM_FILE_EXTENSION;

        std::ifstream checksumFile(checksumPath, std::ios::binary);
        if (checksumFile.is_open())
        {
            std::string const content{std::istreambuf_iterator<char>{checksumFile}, std::istreambuf_iterator<char>{}};
            if (auto checksum = ParseChecksumFile(content, context._asset))
            {
                context._assetSha256 = std::move(*checksum);
            }
            else
            {
                std::cerr << "No valid checksum found in " << checksumPath << " (ignored)\n";
            }
        }
    }
    return context;
}
std::optional<RepoContext> RetrieveContext(std::string const& owner, std::string const& repo, [[maybe_unused]] bool allowPrerelease)
{
    using namespace httplib;

    if (owner.empty() || repo.empty())
    {
        return std::nullopt;
    }

#ifdef _UPDATER_DEF_DUMMYTEST
    RepoContext context;
    context._owner = owner;
    context._repo = repo;
    context._asset = "dummy.zip";
    context._assetUrl = "dummy.zip";
    context._latestTag = { 2, 0, 0 };
    return context;
#else
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
        if (json.empty() || !json.is_array())
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
        if (!assets.is_array() || assets.empty())
        {
            return std::nullopt;
        }

        auto platformToken = GetPlatformToken();
        for (auto const& asset : assets)
        {
            std::string asset_name = asset["name"];
            std::string asset_name_lower = ToLower(asset_name);

            if (!platformToken.empty() && asset_name_lower.find(platformToken) == std::string::npos)
            {
                continue;
            }

            if (!IsArchitectureMatch(asset_name_lower))
            {
                continue;
            }

            std::string content_type = asset["content_type"];
            std::string asset_url = asset["browser_download_url"];

            bool validZipExtension = asset_name_lower.size() >= 4 && asset_name_lower.rfind(".zip") == asset_name_lower.size() - 4;
            if (!IsZipContentType(content_type) && !validZipExtension)
            {
                continue;
            }

            RepoContext context;
            context._owner = owner;
            context._repo = repo;
            context._asset = std::move(asset_name);
            context._assetUrl = std::move(asset_url);
            context._latestTag = tag.value();

            if (auto digest = ParseAssetDigest(asset))
            {
                context._assetSha256 = std::move(*digest);
            }
            else if (auto checksum = FetchRemoteChecksumFile(assets, context._asset))
            {
                context._assetSha256 = std::move(*checksum);
            }
            return context;
        }
    }
    return std::nullopt;
#endif // _UPDATER_DEF_DUMMYTEST
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

std::optional<std::filesystem::path> DownloadLocalAsset(RepoContext const& context, std::filesystem::path const& tempDir)
{
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

    if (relativePath.lexically_normal() != tempDir.lexically_normal() || currentPath == fullPath)
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

#ifdef _UPDATER_DEF_DUMMYTEST
    std::filesystem::path assetPath = tempDir / context._asset;
    std::ofstream file(assetPath, std::ios::binary);
    file << "Dummy data";
    file.close();
    return assetPath;
#else
    if (!std::filesystem::exists(context._assetUrl))
    {
        return std::nullopt;
    }

    std::filesystem::path assetPath;

    if (std::filesystem::is_directory(context._assetUrl))
    {
        assetPath = context._assetUrl;
        // Remove trailing separator if present
        if (assetPath.filename().empty())
        {
            assetPath = assetPath.parent_path();
        }
        assetPath = tempDir / assetPath.filename();
    }
    else
    {
        assetPath = tempDir / std::filesystem::path{context._assetUrl}.filename();
    }

    std::error_code err;
    std::filesystem::copy(context._assetUrl, assetPath, std::filesystem::copy_options::recursive, err);
    if (err)
    {
        return std::nullopt;
    }
    return assetPath;
#endif // _UPDATER_DEF_DUMMYTEST
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

    if (relativePath.lexically_normal() != tempDir.lexically_normal() || currentPath == fullPath)
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

#ifdef _UPDATER_DEF_DUMMYTEST
    std::filesystem::path assetPath = tempDir / context._asset;
    std::ofstream file(assetPath, std::ios::binary);
    file << "Dummy data";
    file.close();
    return assetPath;
#else
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
#endif // _UPDATER_DEF_DUMMYTEST
}

std::optional<std::string> ComputeSha256(std::filesystem::path const& filePath)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        return std::nullopt;
    }

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
    {
        return std::nullopt;
    }

    std::vector<char> buffer(64 * 1024);
    while (file)
    {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        auto const dataSize = file.gcount();
        if (dataSize > 0 && EVP_DigestUpdate(ctx.get(), buffer.data(), static_cast<std::size_t>(dataSize)) != 1)
        {
            return std::nullopt;
        }
    }
    if (file.bad())
    {
        return std::nullopt;
    }

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digestSize = 0;
    if (EVP_DigestFinal_ex(ctx.get(), digest.data(), &digestSize) != 1)
    {
        return std::nullopt;
    }

    constexpr char hexChars[] = "0123456789abcdef";
    std::string result;
    result.reserve(digestSize * 2);
    for (unsigned int i = 0; i < digestSize; ++i)
    {
        result += hexChars[digest[i] >> 4];
        result += hexChars[digest[i] & 0x0F];
    }
    return result;
}
ChecksumStatus VerifyChecksum(RepoContext const& context, std::filesystem::path const& assetPath)
{
    if (context._assetSha256.empty())
    {
        return ChecksumStatus::Unavailable;
    }

    auto const expected = ToLower(context._assetSha256);
    if (!IsSha256Hex(expected))
    {
        std::cerr << "Malformed expected checksum: " << context._assetSha256 << '\n';
        return ChecksumStatus::Invalid;
    }

    if (!std::filesystem::is_regular_file(assetPath))
    {
        std::cerr << "Can't compute checksum of " << assetPath << ": not a regular file\n";
        return ChecksumStatus::Invalid;
    }

    auto const actual = ComputeSha256(assetPath);
    if (!actual)
    {
        std::cerr << "Failed to compute checksum of " << assetPath << '\n';
        return ChecksumStatus::Invalid;
    }

    if (*actual != expected)
    {
        std::cerr << "Checksum mismatch for " << assetPath << '\n';
        std::cerr << "\tExpected: " << expected << '\n';
        std::cerr << "\tActual:   " << *actual << '\n';
        return ChecksumStatus::Invalid;
    }
    return ChecksumStatus::Valid;
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

#ifdef _UPDATER_DEF_DUMMYTEST
    std::filesystem::path extractPath = assetPath.parent_path() / "dummy";
    if (!std::filesystem::exists(extractPath))
    {
        std::filesystem::create_directories(extractPath);
    }
    else if (!std::filesystem::is_directory(extractPath))
    {
        return std::nullopt;
    }

    std::ofstream file(extractPath / "dummy.txt");
    file << "Dummy data";
    file.close();

    //Copy exe and dll files
    auto currentPath = std::filesystem::current_path() / GRUPDATER_EXECUTABLE_NAME_W;
    std::filesystem::copy_file(currentPath, extractPath / GRUPDATER_EXECUTABLE_NAME_W, std::filesystem::copy_options::overwrite_existing);
    currentPath = std::filesystem::current_path() / GRUPDATER_DLL_NAME_W;
    std::filesystem::copy_file(currentPath, extractPath / GRUPDATER_DLL_NAME_W, std::filesystem::copy_options::overwrite_existing);

    return extractPath;
#else
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

    bool extractedFilesHaveRoot = true;
    std::filesystem::path rootPath{};

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

        if (extractFilePath.begin() != extractFilePath.end() && extractedFilesHaveRoot)
        {
            if (!extractFilePath.has_parent_path() && extractFilePath.has_filename())
            {//File inside the root so there is no root directory
                extractedFilesHaveRoot = false;
            }

            if (rootPath.empty())
            {
                rootPath = *extractFilePath.begin();
            }
            else if (rootPath != *extractFilePath.begin())
            {
                extractedFilesHaveRoot = false;
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

    if (extractedFilesHaveRoot && rootPath.empty())
    {
        extractedFilesHaveRoot = false;
    }

    zip_close(zip);

    if (!extractedFilesHaveRoot)
    {
        return assetPath.parent_path();
    }
    return assetPath.parent_path() / rootPath;
#endif // _UPDATER_DEF_DUMMYTEST
}

std::optional<std::chrono::system_clock::time_point> GetScheduleTime(std::filesystem::path const &scheduleFile)
{
    if (scheduleFile.empty() || !std::filesystem::exists(scheduleFile) || !std::filesystem::is_regular_file(scheduleFile))
    {
        return std::nullopt;
    }

    std::ifstream file(scheduleFile);
    if (!file.is_open())
    {
        return std::nullopt;
    }

    try
    {
        nlohmann::json json = nlohmann::json::parse(file);
        auto scheduleTimePoint = std::chrono::system_clock::time_point(std::chrono::seconds{json["time"].get<std::chrono::seconds::rep>()});
        file.close();
        return scheduleTimePoint;
    }
    catch (const nlohmann::json::parse_error& e)
    {
        file.close();
    }
    return std::nullopt;
}

bool SetScheduleTime(std::filesystem::path const &scheduleFile, std::chrono::system_clock::time_point const &time)
{
    if (scheduleFile.empty())
    {
        return false;
    }

    nlohmann::json scheduleTime = nlohmann::json{{"time", std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count()}};
    std::ofstream file(scheduleFile);
    if (!file.is_open())
    {
        return false;
    }
    file << scheduleTime.dump(4);
    file.close();
    return true;
}

bool VerifyScheduleTime(std::chrono::system_clock::time_point const &timePoint, std::chrono::hours const &delay)
{
    auto const now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::hours>(now - timePoint).count() >= delay.count();
}

bool ApplyUpdate(std::filesystem::path const &target, std::filesystem::path callerExecutable, std::optional<uint32_t> callerPid)
{
    //Wait for the caller to close
    if (callerPid)
    {
        if (!WaitForProcessToExit(*callerPid))
        {
            return false;
        }
    }

    if (!target.is_absolute())
    {
        std::cerr << "Target path must be absolute\n";
        return false;
    }
    if (target.empty() || !std::filesystem::exists(target) || !std::filesystem::is_directory(target))
    {
        std::cerr << "Invalid target path\n";
        return false;
    }

    std::cout << "Current directory: " << std::filesystem::current_path() << '\n';
    std::cout << "Applying update to " << target << '\n';

    //GRUpdater executable (from the caller side) should be in the same directory as the target
    auto updaterPath = target / GRUPDATER_EXECUTABLE_NAME;
    if (!std::filesystem::exists(updaterPath) || !std::filesystem::is_regular_file(updaterPath))
    {
        std::cerr << "Invalid updater path: " << updaterPath << '\n';
        return false;
    }

    //Retrieve where this executable is stored (only the first folder from caller root (this should be "temp/"))
    std::filesystem::path temporaryPath = std::filesystem::relative(std::filesystem::current_path(), target);
    if (temporaryPath.empty())
    {
        std::cerr << "Failed to get temporary path\n";
        return false;
    }

    std::cout << "Temporary path: " << temporaryPath << '\n';

    if (temporaryPath.has_parent_path())
    {
        temporaryPath = temporaryPath.parent_path();
    }

    //Get the current json file telling where is all the dynamic files that should not be touched
    std::vector<std::filesystem::path> dynamicFiles;
    auto dynamicFilesPath = target / GRUPDATER_DEFAULT_DYNAMIC_FILE;
    if (std::filesystem::exists(dynamicFilesPath) && std::filesystem::is_regular_file(dynamicFilesPath))
    {
        std::ifstream dynamicFile(dynamicFilesPath);
        if (!dynamicFile.is_open())
        {
            std::cerr << "Failed to open dynamic files json\n";
            return false;
        }

        try
        {
            nlohmann::json dynamicFilesJson = nlohmann::json::parse(dynamicFile);
            dynamicFile.close();

            for (auto& dynamicFileJson : dynamicFilesJson["files"])
            {
                dynamicFiles.emplace_back(dynamicFileJson.get<std::filesystem::path>());
            }
        }
        catch (const nlohmann::json::parse_error& e)
        {
            dynamicFile.close();
            std::cerr << "Failed to parse dynamicFiles.json: " << e.what() << '\n';
            return false;
        }
    }

    //Remove all files that are not in dynamicFiles and avoid removing the temporary folder
    std::filesystem::recursive_directory_iterator itTarget(target);
    for (auto& file : itTarget)
    {
        if (!file.is_regular_file())
        {
            continue;
        }

        if (IsSubDirectory(std::filesystem::relative(file.path(), target), temporaryPath))
        {
            continue;
        }

        if (std::ranges::find(dynamicFiles, std::filesystem::relative(file.path(), target)) == dynamicFiles.end())
        {
            std::cout << "Removing file: " << file << '\n';
            std::filesystem::remove(file);
        }
    }

    //Take all files from the extracted asset (root) and copy them to the target directory
    //Ignore all *.zip file, TODO: we only want to ignore the downloaded zip file.
    std::filesystem::path currentPath = "./";
    std::filesystem::recursive_directory_iterator itAsset(currentPath);
    for (auto& file : itAsset)
    {
        if (!itAsset->is_regular_file())
        {
            continue;
        }

        if (file.path().extension().string() == ".zip")
        {
            continue;
        }

        auto resultFilePath = target / std::filesystem::relative(file.path(), currentPath);
        std::filesystem::create_directories(resultFilePath.parent_path());
        std::cout << "Copy file: " << file << " to " << resultFilePath << '\n';
        std::filesystem::copy(file, resultFilePath);
    }

    if (!callerExecutable.empty())
    {
        std::cout << "Successfully applied update, you can now restart the application !\n";
        std::this_thread::sleep_for(std::chrono::seconds{2});

        std::cout << "Caller executable: " << callerExecutable << '\n';
        if (!SpawnProcessDetached(callerExecutable, {}, callerExecutable.parent_path()))
        {
            std::cerr << "Failed to relaunch caller executable\n";
            return true; //Return true because the update was successful
        }
    }

    return true;
}

bool RequestApplyUpdate(std::filesystem::path const &rootAssetPath, std::filesystem::path const& callerExecutable)
{
    if (rootAssetPath.empty() || !std::filesystem::exists(rootAssetPath) || !std::filesystem::is_directory(rootAssetPath))
    {
        std::cerr << "Invalid root asset path\n";
        return false;
    }

    //GRUpdater executable (from the extracted assets) should be in the same directory as the root asset
    auto updaterPath = rootAssetPath / GRUPDATER_EXECUTABLE_NAME;
    if (!std::filesystem::exists(updaterPath) || !std::filesystem::is_regular_file(updaterPath))
    {
        std::cerr << "Invalid updater path\n";
        return false;
    }

    uint32_t callerPid = 0;
#if defined(_WIN32)
    callerPid = GetCurrentProcessId();
#else
    callerPid = static_cast<uint32_t>(getpid());
#endif

    std::vector<std::string> args = {
        "apply",
        "--target", std::filesystem::current_path().string(),
        "--pid", std::to_string(callerPid),
        "--caller", callerExecutable.string()
    };

    std::cout << "Updater path: " << updaterPath << '\n';
    if (!SpawnProcessDetached(updaterPath, args, rootAssetPath))
    {
        std::cerr << "Failed to create process\n";
        return false;
    }
    return true;
}

std::optional<std::filesystem::path> MakeAvailable(Tag const& currentTag,
                                                   std::string const& owner,
                                                   std::string const& repo,
                                                   std::filesystem::path const& tempDir,
                                                   bool allowPrerelease,
                                                   bool requireChecksum)
{
    //Verify schedule time in order to avoid spamming GitHub API requests
    auto scheduleTime = GetScheduleTime();
    if (scheduleTime && !VerifyScheduleTime(*scheduleTime))
    {
        std::cerr << "Schedule time not reached yet\n";
        return std::nullopt;
    }
    if (!SetScheduleTime())
    {
        std::cerr << "Failed to set schedule time (will continue anyway)\n";
    }

    auto context = RetrieveContext(owner, repo, allowPrerelease);
    if (!context)
    {
        std::cerr << "Failed to retrieve context\n";
        return std::nullopt;
    }
    std::cout << "Context retrieved :\n";
    std::cout << "\tOwner: " << context->_owner << '\n';
    std::cout << "\tRepo: " << context->_repo << '\n';
    std::cout << "\tAsset: " << context->_asset << '\n';
    std::cout << "\tAsset URL: " << context->_assetUrl << '\n';
    std::cout << "\tLatest Tag: " << context->_latestTag.major << '.' << context->_latestTag.minor << '.' << context->_latestTag.patch << '\n';
    std::cout << "\tAsset SHA-256: " << (context->_assetSha256.empty() ? "<unavailable>" : context->_assetSha256) << '\n';

    if (VerifyTag(*context, currentTag) != TagStatus::NewerTag)
    {
        std::cerr << "No newer tag available\n";
        return std::nullopt;
    }
    std::cout << "Newer tag available\n";

    auto zipFile = DownloadAsset(*context, tempDir);
    if (!zipFile)
    {
        std::cerr << "Failed to download asset\n";
        return std::nullopt;
    }

    std::cout << "Asset downloaded to " << *zipFile << '\n';

    switch (VerifyChecksum(*context, *zipFile))
    {
    case ChecksumStatus::Valid:
        std::cout << "Checksum verified\n";
        break;
    case ChecksumStatus::Invalid:
    {
        std::cerr << "Invalid checksum, the downloaded asset is corrupted or has been tampered with\n";
        std::cerr << "File will be removed\n";
        std::error_code err;
        std::filesystem::remove(*zipFile, err);
        return std::nullopt;
    }
    case ChecksumStatus::Unavailable:
        if (requireChecksum)
        {
            std::cerr << "No checksum available for the asset\n";
            std::cerr << "File will be removed\n";
            std::error_code err;
            std::filesystem::remove(*zipFile, err);
            return std::nullopt;
        }
        std::cerr << "No checksum available for the asset, skipping verification\n";
        break;
    }

    auto extractRoot = ExtractAsset(*zipFile);
    if (!extractRoot)
    {
        std::cerr << "Failed to extract asset\n";
        return std::nullopt;
    }

    std::cout << "Asset extracted to " << *extractRoot << '\n';
    return extractRoot;
}

}//namespace updater
