#include "updater.hpp"
#include "CLI11.hpp"
#include <iostream>
#include <json.hpp>

#define GRUPDATER_SCHEDULE_TIME_HOURS 2

int main (int argc, char **argv)
{
    using namespace updater;

    CLI::App app{"A updater for GitHub releases", "GRUpdater"};

    app.add_flag_callback("--version", [](){
        std::cout << "GRUpdater " GRUPDATER_TAG_STR "\n";
        throw CLI::Success{};
    }, "Print the version (and do nothing else)");

    std::string currentTagString;
    app.add_option("--current", currentTagString, "The current version of the software")
        ->required();

    std::string owner;
    app.add_option("--owner", owner, "The owner of the repository")
        ->required();

    std::string repo;
    app.add_option("--repo", repo, "The repository name")
        ->required();

    std::filesystem::path assetPath;
    app.add_option("--asset", assetPath, "The asset path, if already extracted");

    bool verifyTag = true;
    app.add_flag("--verify", verifyTag, "Verify the tag");
    bool downloadAsset = false;
    app.add_flag("--download", downloadAsset, "Download the asset");
    bool extractAsset = false;
    app.add_flag("--extract", extractAsset, "Extract the asset");
    bool allowPrerelease = false;
    app.add_flag("--prerelease", allowPrerelease, "Allow prerelease tag");
    bool applyUpdate = false;
    app.add_flag("--apply", applyUpdate, "Apply the update");

    std::filesystem::path tempDir = "temp/";
    app.add_option("--temp", tempDir, "The temporary directory to store the asset");

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e)
    {
        return app.exit(e);
    }

    if (applyUpdate && assetPath.empty())
    {
        std::cerr << "Asset path is required to apply update\n";
        return 1;
    }
    if (applyUpdate && !std::filesystem::exists(assetPath))
    {
        std::cerr << "Asset path does not exist\n";
        return 1;
    }

    auto currentTag = ParseTag(currentTagString);
    if (!currentTag)
    {
        std::cerr << "Failed to parse current tag\n";
        return 1;
    }

    //Applying update
    if (applyUpdate)
    {
        //Get the current json file telling where is all the dynamic files that should not be touched
        std::vector<std::filesystem::path> dynamicFiles;
        if (std::filesystem::exists("./dynamicFiles.json"))
        {
            std::ifstream dynamicFile("./dynamicFiles.json");
            try
            {
                nlohmann::json dynamicFilesJson = nlohmann::json::parse(dynamicFile);
                dynamicFile.close();

                for (auto& dynamicFileJson : dynamicFilesJson["files"])
                {
                    dynamicFiles.emplace_back(dynamicFileJson.get<std::string>());
                }
            }
            catch (const nlohmann::json::parse_error& e)
            {
                dynamicFile.close();
                std::cerr << "Failed to parse dynamicFiles.json: " << e.what() << '\n';
            }
        }

        std::filesystem::path currentPath = "./test/";
        std::cout << "Current path: " << currentPath << '\n';

        //Remove all files that are not in dynamicFiles and the current executable
        dynamicFiles.emplace_back(std::filesystem::absolute(argv[0]));

        std::filesystem::recursive_directory_iterator itCurrent(currentPath);
        for (auto& file : itCurrent)
        {
            if (!itCurrent->is_regular_file())
            {
                continue;
            }

            if (std::ranges::find(dynamicFiles, file) == dynamicFiles.end())
            {
                //std::filesystem::remove(file);
                std::cout << "Remove file: " << file << '\n';
            }
        }

        //Take all files from the extracted asset and copy them to the current directory
        std::filesystem::recursive_directory_iterator itAsset(assetPath);
        for (auto& file : itAsset)
        {
            if (!itAsset->is_regular_file())
            {
                continue;
            }

            std::filesystem::create_directories(currentPath / std::filesystem::relative(file.path(), assetPath).parent_path());
            std::cout << "Copy file: " << file << " to " << currentPath / std::filesystem::relative(file.path(), assetPath) << '\n';
            std::filesystem::copy(file, currentPath / std::filesystem::relative(file.path(), assetPath));
        }

        return 0;
    }

    //Verify schedule time in order to avoid spamming GitHub API requests
    nlohmann::json scheduleTime;
    std::optional<std::chrono::system_clock::time_point> scheduleTimePoint;
    if (std::filesystem::exists("./schedule.json"))
    {
        std::ifstream file("./schedule.json");
        try
        {
            scheduleTime = nlohmann::json::parse(file);
            scheduleTimePoint = std::chrono::system_clock::from_time_t(scheduleTime["time"].get<std::time_t>());
            file.close();
        }
        catch (const nlohmann::json::parse_error& e)
        {
            file.close();
            std::cerr << "Failed to parse schedule.json: " << e.what() << '\n';
        }
    }

    auto const now = std::chrono::system_clock::now();
    if (scheduleTimePoint && std::chrono::duration_cast<std::chrono::hours>(now - *scheduleTimePoint).count() < GRUPDATER_SCHEDULE_TIME_HOURS)
    {
        std::cerr << "Schedule time not reached yet\n";
        return 1;
    }

    {
        scheduleTime = nlohmann::json{{"time", std::chrono::system_clock::to_time_t(now)}};
        std::ofstream file("./schedule.json");
        file << scheduleTime.dump(4);
        file.close();
    }

    auto context = RetrieveContext(owner, repo, allowPrerelease);
    if (!context)
    {
        std::cerr << "Failed to retrieve context\n";
        return 1;
    }

    if (verifyTag)
    {
        if (VerifyTag(*context, *currentTag) != TagStatus::NewerTag)
        {
            std::cerr << "No newer tag available\n";
            return 1;
        }
        std::cout << "Newer tag available\n";
    }

    std::optional<std::filesystem::path> zipFile;
    if (downloadAsset)
    {
        zipFile = DownloadAsset(*context, tempDir);
        if (!zipFile)
        {
            std::cerr << "Failed to download asset\n";
            return 1;
        }
    }
    else
    {
        std::cerr << "Not implemented\n";
        return 1;
    }

    std::cout << "Asset downloaded to " << *zipFile << '\n';

    std::optional<std::filesystem::path> extractRoot;
    if (extractAsset)
    {
        extractRoot = ExtractAsset(*zipFile);
        if (!extractRoot)
        {
            std::cerr << "Failed to extract asset\n";
            return 1;
        }
    }
    else
    {
        std::cerr << "Not implemented\n";
        return 1;
    }

    std::cout << "Asset extracted to " << *extractRoot << '\n';

    return 0;
}

