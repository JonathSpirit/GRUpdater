#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"
#include <iostream>
#include <fstream>

int main ()
{
    using namespace httplib;

    // HTTPS
    Client cli("https://api.github.com");

    // Use your CA bundle
    //cli.set_ca_cert_path("./ca-bundle.crt");

    // Disable cert verification
    //cli.enable_server_certificate_verification(false);

    // Disable host verification
    //cli.enable_server_hostname_verification(false);

    Headers headers = {
        { "Accept", "application/vnd.github+json" },
        { "X-GitHub-Api-Version", "2022-11-28" }
    };

    //Add query parameters per_page=1 and page=1
    if (auto res = cli.Get("/repos/JonathSpirit/FastEngine/releases?per_page=1", headers))
    {
        std::cout << "status: " << res->status << std::endl;
        if (res->status == StatusCode::OK_200)
        {
            //Write in file
            std::ofstream file("releases.json");
            file << res->body;
            file.close();

            //Parse json
            nlohmann::json json = nlohmann::json::parse(res->body);
            std::string tag_name = json[0]["tag_name"];
            std::cout << "tag_name: " << tag_name << std::endl;

            //Test downloading https://github.com/JonathSpirit/FastEngine/releases/download/v0.9.3/FastEngine-windows-i686-mingw32.zip
            Client cli("https://github.com");
            cli.set_follow_location(true);
            if (auto res = cli.Get("/JonathSpirit/FastEngine/releases/download/" + tag_name + "/FastEngine-windows-i686-mingw32.zip"))
            {
                std::cout << "status: " << res->status << std::endl;
                if (res->status == StatusCode::OK_200)
                {
                    std::ofstream file("FastEngine-windows-i686-mingw32.zip", std::ios::binary);
                    file << res->body;
                    file.close();
                    std::cout << "Downloaded FastEngine-windows-i686-mingw32.zip" << std::endl;
                }
            }
            else
            {
                auto err = res.error();
                std::cout << "HTTP error: " << to_string(err) << std::endl;
            }
        }
    }
    else
    {
        auto err = res.error();
        std::cout << "HTTP error: " << to_string(err) << std::endl;
    }

    return 0;
}

