#include "crow_all.h"
#include <string>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

std::string read_first_line(const fs::path &path)
{
    auto stream = std::ifstream(path);
    auto out = std::string{};
    std::getline(stream, out);
    return out;
}

int main()
{
    crow::SimpleApp app;

    auto discover = [](const crow::request &req)
    {
        auto doc = crow::json::wvalue();
        auto tty_dir = fs::path("/sys/class/tty");

        for (const auto &entry : fs::directory_iterator{tty_dir})
        {
            auto err = std::error_code{};

            if (!std::string(entry.path().filename()).starts_with("ttyACM"))
            {
                continue;
            }

            auto subsystem_link = entry.path() / "device" / "subsystem";
            auto subsystem_path = subsystem_link.parent_path() / fs::read_symlink(subsystem_link, err);

            if (subsystem_path.empty() || fs::canonical(subsystem_path) != fs::path("/sys/bus/usb"))
            {
                CROW_LOG_INFO << subsystem_path << " != /sys/bus/usb";
                continue;
            }

            auto device_link = entry.path() / "device";
            auto device_path = device_link.parent_path() / fs::read_symlink(device_link, err);
            CROW_LOG_INFO << "device path: " << device_link << " -> " << device_path;

            if (device_path.empty())
            {
                CROW_LOG_INFO << device_link << " can't be resolved";
                continue;
            }

            auto usb_vid = read_first_line(device_path / ".." / "idVendor");
            auto usb_pid = read_first_line(device_path / ".." / "idProduct");
            auto usb_serial = read_first_line(device_path / ".." / "serial");

            std::transform(usb_serial.begin(), usb_serial.end(), usb_serial.begin(),
                           [](unsigned char c)
                           { return std::tolower(c); });

            CROW_LOG_INFO << "vid:pid = " << usb_vid << ":" << usb_pid;
            CROW_LOG_INFO << "device ID = " << usb_serial;

            // Check if device is made by Particle
            if (usb_vid != "2b04")
            {
                continue;
            }

            // Check if device is a Photon or P1
            if (usb_pid != "c006" && usb_pid != "c008")
            {
                continue;
            }

            doc[usb_serial] = "/dev/" + entry.path().filename().string();
        }

        return doc;
    };

    CROW_ROUTE(app, "/usb-proxy/discover").methods("GET"_method)(discover);

    app.port(5000).run();
}
