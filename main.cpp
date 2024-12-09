#include "updater.hpp"
#include "CLI11.hpp"
#include <iostream>

int main (int argc, char **argv)
{
    using namespace updater;

    CLI::App app{"A updater for GitHub releases", "GRUpdater"};

    app.add_flag_callback("--version", [](){
        std::cout << "GRUpdater v0.0.1\n";
        throw CLI::Success{};
    }, "Print the version (and do nothing else)");

    //TODO: Commands goes here

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e)
    {
        return app.exit(e);
    }

    /*RepoContext context;
    context._owner = "JonathSpirit";
    context._repo = "FastEngine";
    context._asset = "FastEngine-windows-ucrt-x86_64-ucrt64.zip";
    context._assetUrl = "";
    context._latestTag.major = 0;
    context._latestTag.minor = 9;
    context._latestTag.patch = 0;

    std::filesystem::path zipFile = "temp/FastEngine-windows-ucrt-x86_64-ucrt64.zip";

    auto testExtractPath = ExtractAsset(zipFile);
    if (testExtractPath)
    {
        std::cout << "Asset extracted to " << testExtractPath->string() << '\n';
    }
    else
    {
        std::cerr << "Failed to extract asset\n";
    }

    return 0;*/

    auto myTag = ParseTag("v0.0.1");
    if (!myTag)
    {
        std::cerr << "Failed to parse tag\n";
        return 1;
    }

    auto myContext = RetrieveContext("JonathSpirit", "FastEngine", true);
    if (!myContext)
    {
        std::cerr << "Failed to retrieve context\n";
        return 1;
    }

    if (VerifyTag(*myContext, *myTag) == TagStatus::NewerTag)
    {
        std::cout << "Newer tag available\n";

        auto zipFile = DownloadAsset(*myContext, "temp/");
        if (!zipFile)
        {
            std::cerr << "Failed to download asset\n";
            return 1;
        }

        std::cout << "Asset downloaded to " << *zipFile << '\n';
        auto extractRoot = ExtractAsset(*zipFile);

        if (!extractRoot)
        {
            std::cerr << "Failed to extract asset\n";
            return 1;
        }

        std::cout << "Asset extracted to " << *extractRoot << '\n';
    }
    else
    {
        std::cout << "No newer tag available\n";
    }

    return 0;
}

