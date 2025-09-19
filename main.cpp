#include "updater.hpp"
#include "CLI11.hpp"
#include <iostream>
#include <bits/this_thread_sleep.h>

int main (int argc, char **argv)
{
    using namespace updater;

    CLI::App app{"A updater for GitHub releases", "GRUpdater"};

    app.add_flag_callback("--version", [](){
        std::cout << "GRUpdater " GRUPDATER_TAG_STR "\n";
        throw CLI::Success{};
    }, "Print the version (and do nothing else)");

    std::string currentTagString;
    std::string owner;
    std::string repo;

    auto subcommandFetch = app.add_subcommand("fetch", "Fetch the latest release from GitHub");

    subcommandFetch->add_option("-c,--current", currentTagString, "The current version tag of the software")
        ->required();
    subcommandFetch->add_option("-o,--owner", owner, "The owner of the repository")
        ->required();
    subcommandFetch->add_option("-r,--repo", repo, "The repository name")
        ->required();

    std::filesystem::path tempDir = "./temp/";
    subcommandFetch->add_option("-t,--temp", tempDir, "The relative temporary directory to store the asset (default: ./temp/)");

    bool verifyTag = true;
    bool downloadAsset = false;
    bool extractAsset = false;
    bool allowPrerelease = false;

    subcommandFetch->add_flag("--verify", verifyTag, "Verify the tag");
    subcommandFetch->add_flag("--download", downloadAsset, "Download the asset");
    subcommandFetch->add_flag("--extract", extractAsset, "Extract the asset");
    subcommandFetch->add_flag("--prerelease", allowPrerelease, "Allow prerelease tag");

    subcommandFetch->callback([&] {
        auto currentTag = ParseTag(currentTagString);
        if (!currentTag)
        {
            std::cerr << "Failed to parse current tag\n";
            throw CLI::RuntimeError{1};
        }

        //Verify schedule time in order to avoid spamming GitHub API requests
        auto scheduleTime = GetScheduleTime();
        if (scheduleTime && !VerifyScheduleTime(*scheduleTime))
        {
            std::cerr << "Schedule time not reached yet, remaining "
                      << GRUPDATER_DEFAULT_SCHEDULE_DELAY_HOURS * 60 - std::chrono::duration_cast<std::chrono::minutes>(std::chrono::system_clock::now() - *scheduleTime).count() << " minutes\n";
            throw CLI::RuntimeError{1};
        }
        if (!SetScheduleTime())
        {
            std::cerr << "Failed to set schedule time (will continue anyway)\n";
        }

        auto context = RetrieveContext(owner, repo, allowPrerelease);
        if (!context)
        {
            std::cerr << "Failed to retrieve context\n";
            throw CLI::RuntimeError{1};
        }
        std::cout << "Context retrieved :\n";
        std::cout << "\tOwner: " << context->_owner << '\n';
        std::cout << "\tRepo: " << context->_repo << '\n';
        std::cout << "\tAsset: " << context->_asset << '\n';
        std::cout << "\tAsset URL: " << context->_assetUrl << '\n';
        std::cout << "\tLatest Tag: " << context->_latestTag.major << '.' << context->_latestTag.minor << '.' << context->_latestTag.patch << '\n';

        if (verifyTag)
        {
            if (VerifyTag(*context, *currentTag) != TagStatus::NewerTag)
            {
                std::cerr << "No newer tag available\n";
                throw CLI::RuntimeError{1};
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
                throw CLI::RuntimeError{1};
            }
        }
        else
        {
            std::cerr << "Not implemented\n";
            throw CLI::RuntimeError{1};
        }

        std::cout << "Asset downloaded to " << *zipFile << '\n';

        std::optional<std::filesystem::path> extractRoot;
        if (extractAsset)
        {
            extractRoot = ExtractAsset(*zipFile);
            if (!extractRoot)
            {
                std::cerr << "Failed to extract asset\n";
                throw CLI::RuntimeError{1};
            }
        }
        else
        {
            std::cerr << "Not implemented\n";
            throw CLI::RuntimeError{1};
        }

        std::cout << "Asset extracted to " << *extractRoot << '\n';

        throw CLI::Success{};
    });

    auto subcommandApply = app.add_subcommand("apply", "Apply the update");

    std::filesystem::path rootAssetPath;
    subcommandApply->add_option("-r,--root", rootAssetPath, "The root asset path (absolute path)")
        ->required()
        ->check(CLI::ExistingDirectory);

    subcommandApply->add_option("--caller", callerExecutable, "The caller executable path")
        ->required()
        ->check(CLI::ExistingFile);

    subcommandApply->callback([&] {
        if (ApplyUpdate(rootAssetPath, callerExecutable))
        {
            std::cout << "Request to apply update sent\n";
            std::cout << "This process will now close in order to apply it\n";
            std::this_thread::sleep_for(std::chrono::seconds{2});
            throw CLI::Success{};
        }
        std::cerr << "Failed to call ApplyUpdate()\n";
        throw CLI::RuntimeError{1};
    });

    CLI11_PARSE(app, argc, argv);
}

