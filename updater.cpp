#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"
#include "updater.hpp"
#include <zip.h>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>

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
        //Get handle
        HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, *callerPid);
        if (hProcess != nullptr)
        {
            //Wait for the process to finish
            std::cout << "Waiting for process " << *callerPid << " to finish\n";
            DWORD ret = WaitForSingleObject(hProcess, GRUPDATER_WAIT_PID_TIMEOUT_MS);
            CloseHandle(hProcess);
            if (ret != WAIT_OBJECT_0)
            {
                std::cerr << "Failed to wait for process " << *callerPid << '\n';
                return false;
            }
        }
        else
        {
            std::cout << "Failed to open process " << *callerPid << '\n';
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
    std::filesystem::path currentPath = "./";
    std::filesystem::recursive_directory_iterator itAsset(currentPath);
    for (auto& file : itAsset)
    {
        if (!itAsset->is_regular_file())
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
        //Launch the caller executable
        std::wstring callerExecutableW = callerExecutable.wstring();
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(callerExecutableW.c_str(), nullptr,
            nullptr, nullptr, FALSE,
            CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, nullptr,
            callerExecutable.parent_path().wstring().c_str(), &si, &pi))
        {
            std::cerr << "Failed to create process\n";
            return true; //Return true because the update was successful
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
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

    //Get the caller process id
    auto callerPid = GetCurrentProcessId();

    //Launch the updater executable
    std::wstring updaterPathW = updaterPath.wstring();
    std::wstring commandLine = GRUPDATER_EXECUTABLE_NAME_W L" apply --target \""
            + std::filesystem::current_path().wstring()
            + L"\" --pid " + std::to_wstring(callerPid)
            + L" --caller \"" + callerExecutable.wstring() + L'\"';

    std::wcout << "Command line: " << commandLine << '\n';
    std::wcout << "Updater path: " << updaterPathW << '\n';

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(updaterPathW.c_str(), commandLine.data(),
        nullptr, nullptr, FALSE,
        CREATE_NEW_PROCESS_GROUP | CREATE_NEW_CONSOLE, nullptr,
        rootAssetPath.wstring().c_str(), &si, &pi))
    {
        std::cerr << "Failed to create process\n";
        return false;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

std::optional<std::filesystem::path> MakeAvailable(Tag const& currentTag,
                                                   std::string const& owner,
                                                   std::string const& repo,
                                                   std::filesystem::path const& tempDir,
                                                   bool allowPrerelease)
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
