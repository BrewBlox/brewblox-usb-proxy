#include "crow_all.h"
#include <string>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <cerrno>
#include <charconv>
#include <set>
#include <spawn.h>

namespace fs = std::filesystem;

static constexpr uint16_t SERVICE_PORT = 5000;
static constexpr uint16_t PROXY_START_PORT = 9000;

struct ProxyConnection
{
    // Lower-case controller device ID
    std::string device_id = "";

    // This will be "ttyACM[0-256]"
    std::string tty_name = "";

    // Port is always start + tty number
    // If start = 9000, /dev/ttyACM2 -> 9002
    uint16_t port = 0;

    // Process ID of the socat process
    pid_t handle = 0;
};

std::ostream &operator<<(std::ostream &os, const ProxyConnection &conn)
{
    os << "tty=" << conn.tty_name
       << " id=" << conn.device_id
       << " pid=" << conn.handle;
    return os;
}

void make_lower_case(std::string &s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c)
                   { return std::tolower(c); });
}

std::string read_first_line(const fs::path &path)
{
    auto stream = std::ifstream(path);
    auto out = std::string{};
    std::getline(stream, out);
    make_lower_case(out);
    return out;
}

int main()
{
    crow::SimpleApp app;
    std::map<std::string, ProxyConnection> connections;

    auto discover = [&connections](std::string desired_id)
    {
        make_lower_case(desired_id);
        auto detected_tty = std::set<std::string>();

        // If a socat process has ended, remove it now
        std::erase_if(connections,
                      [](const auto &item)
                      {
                          const auto &[key, value] = item;

                          if (value.handle == 0 || kill(value.handle, 0) != 0)
                          {
                              CROW_LOG_INFO << "Discarded " << value;
                              return true;
                          }

                          return false;
                      });

        // Iterate over all tty devices to detect valid USB devices
        for (const auto &entry : fs::directory_iterator{"/sys/class/tty"})
        {
            auto err = std::error_code{};

            // We're only interested in ACM devices
            if (!std::string(entry.path().filename()).starts_with("ttyACM"))
            {
                continue;
            }

            auto subsystem_link = entry.path() / "device" / "subsystem";
            auto subsystem_path = subsystem_link.parent_path() / fs::read_symlink(subsystem_link, err);

            // We only want USB devices
            if (subsystem_path.empty() || fs::canonical(subsystem_path) != fs::path("/sys/bus/usb"))
            {
                CROW_LOG_DEBUG << subsystem_path << " != /sys/bus/usb";
                continue;
            }

            auto device_link = entry.path() / "device";
            auto device_path = device_link.parent_path() / fs::read_symlink(device_link, err);

            if (device_path.empty())
            {
                CROW_LOG_DEBUG << device_link << " can't be resolved";
                continue;
            }

            auto tty_name = entry.path().filename().string();
            auto usb_vid = read_first_line(device_path / ".." / "idVendor");
            auto usb_pid = read_first_line(device_path / ".." / "idProduct");
            auto usb_serial = read_first_line(device_path / ".." / "serial");

            // We only want the Particle-made Sparks
            if (usb_vid != "2b04")
            {
                continue;
            }

            // We only want the Photon (Spark 2) or P1 (Spark 3)
            if (usb_pid != "c006" && usb_pid != "c008")
            {
                continue;
            }

            // Always include all detected devices
            // We will be removing connections for devices that are no longer detected
            detected_tty.insert(tty_name);
            CROW_LOG_DEBUG << "Detected " << tty_name << " | " << usb_serial << " | " << usb_vid << ":" << usb_pid;

            // Skip devices that already have a running proxy process
            auto existing = connections.find(tty_name);
            if (existing != connections.end())
            {
                // Update device ID for connection,
                // in case the original Spark was unplugged and replaced with another
                existing->second.device_id = usb_serial;
                continue;
            }

            // Skip devices that don't match the URL parameter
            if (desired_id != "all" && desired_id != usb_serial)
            {
                CROW_LOG_DEBUG << "Skipped " << tty_name;
                continue;
            }

            // We want to use a deterministic port for each tty device
            // Linux only supports 256 ttyACM devices
            // We use a start port, and then bind the proxy to start port + tty index
            // If start port is 9000, then ttyACM2 is always bound to 9002
            uint16_t port = 0;
            auto port_parse_ret = std::from_chars(tty_name.data() + 6, // "ttyACM" prefix
                                                  tty_name.data() + tty_name.size(),
                                                  port);

            if (port_parse_ret.ec == std::errc())
            {
                port += PROXY_START_PORT;
            }
            else
            {
                CROW_LOG_ERROR << "Failed to parse port from " << tty_name;
                continue;
            }

            // Spawn a socat process to proxy the USB device to our chosen TCP port
            // Services can now connect to this port as if it were a TCP connection to the Spark
            std::string arg0 = "/usr/bin/socat";
            std::string arg1 = "tcp-listen:" + std::to_string(port) + ",reuseaddr,fork";
            std::string arg2 = "file:/dev/" + tty_name + ",raw,echo=0,b115200";
            std::array<char *, 4> command{arg0.data(), arg1.data(), arg2.data(), nullptr};
            pid_t handle = 0;

            int spawn_ret = posix_spawn(&handle, arg0.c_str(), nullptr, nullptr, command.data(), environ);
            if (spawn_ret != 0)
            {
                CROW_LOG_ERROR << "Failed to spawn: " << arg0 << " " << arg1 << " " << arg2;
                continue;
            }

            auto conn = ProxyConnection{
                .device_id = usb_serial,
                .tty_name = tty_name,
                .port = port,
                .handle = handle,
            };

            CROW_LOG_INFO << "Started " << conn;
            connections.emplace(tty_name, std::move(conn));
        }

        // socat does not automatically terminate if the USB device is disconnected
        // If the USB device is no longer listed, we want to kill the proxy process
        std::erase_if(connections,
                      [&detected_tty](const auto &item)
                      {
                          const auto &[key, value] = item;
                          if (detected_tty.find(key) == detected_tty.end())
                          {
                              int result = kill(value.handle, SIGINT);
                              CROW_LOG_INFO << "Stopped " << value << " " << result;
                              return true;
                          }
                          return false;
                      });

        // Return the device IDs of all connected devices, mapped to proxy TCP port
        auto doc = crow::json::wvalue(crow::json::wvalue::object());
        for (const auto &[key, value] : connections)
        {
            doc[value.device_id] = value.port;
        }

        return doc;
    };

    CROW_ROUTE(app, "/usb-proxy/discover/<string>").methods("GET"_method)(discover);

    // Only use a single thread to handle requests
    // The discovery endpoint is not thread-safe
    app.port(SERVICE_PORT).run();
}
