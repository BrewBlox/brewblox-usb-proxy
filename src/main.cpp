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
#include <spawn.h>
#include <sys/wait.h>

namespace fs = std::filesystem;

static constexpr uint16_t SERVICE_PORT = 5000;
static constexpr uint16_t PROXY_START_PORT = 9000;

struct ProxyConnection
{
    // Lower-case controller device ID
    std::string device_id = "";

    // This will be "ttyACM[0-255]" or "ttyUSB[0-255]"
    std::string tty_name = "";

    // Port is always start + tty number
    // ttyACM uses ports 9000-9255, ttyUSB uses ports 9256-9511
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
                   [](char c)
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

// Supported device configurations:
// - ttyACM + /sys/bus/usb: Particle Photon/P1 (Spark 2/3), ESP32-S3 native USB
// (Spark 5)
// - ttyUSB + /sys/bus/usb-serial: ESP32 with CP210x bridge (Spark 4)
struct TtyConfig {
  const char *prefix;
  const char *subsystem;
  int parent_levels;    // How many ".." to reach USB device root
  uint16_t port_offset; // Added to PROXY_START_PORT + tty index
};

static constexpr TtyConfig TTY_CONFIGS[] = {
    {"ttyACM", "/sys/bus/usb", 1, 0},          // ports 9000-9255
    {"ttyUSB", "/sys/bus/usb-serial", 2, 256}, // ports 9256-9511
};

// Check if VID:PID matches a supported Spark controller
bool is_supported_device(const std::string &vid, const std::string &pid) {
  // Particle Photon (Spark 2): VID 2b04, PID c006
  // Particle P1 (Spark 3): VID 2b04, PID c008
  if (vid == "2b04" && (pid == "c006" || pid == "c008"))
    return true;
  // ESP32-S3 native USB (Spark 5 / dev): VID 303a, PID 1001
  if (vid == "303a" && pid == "1001")
    return true;
  // ESP32 with CP210x USB-UART bridge (Spark 4): VID 10c4, PID ea60
  if (vid == "10c4" && pid == "ea60")
    return true;
  return false;
}

int main()
{
    crow::SimpleApp app;
    std::map<std::string, ProxyConnection> connections;

    auto discover = [&connections](std::string desired_id)
    {
        make_lower_case(desired_id);
        // key is tty name, value is device ID
        auto detected_tty = std::map<std::string, std::string>();

        // Reap exited socat processes before the liveness check below.
        // posix_spawn'ed children linger in the process table as zombies
        // until they are waited for, and kill(pid, 0) succeeds for a zombie.
        // Without this, a socat that died while its tty stayed present is
        // never detected: the entry survives, its port keeps being reported
        // as available, and no replacement is ever spawned.
        while (waitpid(-1, nullptr, WNOHANG) > 0)
        {
        }

        // If a socat process has ended, remove it now
        std::erase_if(connections,
                      [](const auto &item)
                      {
                          const auto &[key, value] = item;

                          // kill(pid, 0) will only confirm whether the process exists
                          // No actual signal will be sent to the process
                          if (value.handle == 0 || kill(value.handle, 0) != 0)
                          {
                              CROW_LOG_INFO << "Discarded " << value;
                              return true;
                          }

                          return false;
                      });

        // Helper lambda to process a detected USB device
        auto process_usb_device = [&](const TtyConfig &config,
                                      const std::string &tty_name,
                                      const std::string &usb_vid,
                                      const std::string &usb_pid,
                                      const std::string &usb_serial) {
          // Always include all detected devices
          // We will be removing connections for devices that are no longer
          // detected
          detected_tty[tty_name] = usb_serial;
          CROW_LOG_DEBUG << "Detected " << tty_name << " | " << usb_serial
                         << " | " << usb_vid << ":" << usb_pid;

          // Skip devices that already have a running proxy process
          auto existing = connections.find(tty_name);
          if (existing != connections.end()) {
            // If a new device is now associated with this proxy,
            // we want to close socat to force clients to reconnect.
            // This prevents the connection being silently transferred to a new
            // device.
            if (existing->second.device_id != usb_serial) {
              kill(existing->second.handle, SIGINT);
              connections.erase(existing);
            } else {
              return;
            }
          }

          // Skip devices that don't match the URL parameter
          if (desired_id != "all" && desired_id != usb_serial) {
            CROW_LOG_DEBUG << "Skipped " << tty_name;
            return;
          }

          // Calculate port: PROXY_START_PORT + port_offset + tty_index
          // ttyACM devices use ports 9000-9255, ttyUSB devices use ports
          // 9256-9511
          uint16_t tty_index = 0;
          auto prefix_len = strlen(config.prefix);
          auto port_parse_ret =
              std::from_chars(tty_name.data() + prefix_len,
                              tty_name.data() + tty_name.size(), tty_index);

          if (port_parse_ret.ec != std::errc()) {
            CROW_LOG_ERROR << "Failed to parse port from " << tty_name;
            return;
          }

          uint16_t port = PROXY_START_PORT + config.port_offset + tty_index;

          // Verify the device node exists before spawning socat
          // The sysfs entry may be present while /dev/ node is not
          // (e.g. udev delay, or device not mapped into container)
          auto dev_path = fs::path("/dev") / tty_name;
          if (!fs::exists(dev_path)) {
            CROW_LOG_WARNING << dev_path << " not found, skipping";
            return;
          }

          // Spawn a socat process to proxy the USB device to our chosen TCP
          // port Services can now connect to this port as if it were a TCP
          // connection to the Spark
          std::string arg0 = "/usr/bin/socat";
          std::string arg1 =
              "tcp-listen:" + std::to_string(port) + ",reuseaddr,fork";
          std::string arg2 = "file:/dev/" + tty_name + ",raw,echo=0,b115200";
          std::array<char *, 4> command{arg0.data(), arg1.data(), arg2.data(),
                                        nullptr};
          pid_t handle = 0;

          int spawn_ret = posix_spawn(&handle, arg0.c_str(), nullptr, nullptr,
                                      command.data(), environ);
          if (spawn_ret != 0) {
            CROW_LOG_ERROR << "Failed to spawn: " << arg0 << " " << arg1 << " "
                           << arg2;
            return;
          }

          auto conn = ProxyConnection{
              .device_id = usb_serial,
              .tty_name = tty_name,
              .port = port,
              .handle = handle,
          };

          CROW_LOG_INFO << "Started " << conn;
          connections.emplace(tty_name, std::move(conn));
        };

        // Iterate over all tty devices to detect valid USB devices
        for (const auto &entry : fs::directory_iterator{"/sys/class/tty"}) {
          auto err = std::error_code{};
          auto tty_name = entry.path().filename().string();

          // Find matching tty config
          const TtyConfig *config = nullptr;
          for (const auto &cfg : TTY_CONFIGS) {
            if (tty_name.starts_with(cfg.prefix)) {
              config = &cfg;
              break;
            }
          }

          if (!config) {
            continue;
          }

          auto subsystem_link = entry.path() / "device" / "subsystem";
          auto subsystem_path = subsystem_link.parent_path() /
                                fs::read_symlink(subsystem_link, err);

          if (subsystem_path.empty() ||
              fs::canonical(subsystem_path) != fs::path(config->subsystem)) {
            CROW_LOG_DEBUG << subsystem_path << " != " << config->subsystem;
            continue;
          }

          auto device_link = entry.path() / "device";
          auto device_path =
              device_link.parent_path() / fs::read_symlink(device_link, err);

          if (device_path.empty()) {
            CROW_LOG_DEBUG << device_link << " can't be resolved";
            continue;
          }

          // Navigate to USB device root (different depth for ttyACM vs ttyUSB)
          auto usb_root = device_path;
          for (int i = 0; i < config->parent_levels; ++i) {
            usb_root = usb_root / "..";
          }

          auto usb_vid = read_first_line(usb_root / "idVendor");
          auto usb_pid = read_first_line(usb_root / "idProduct");
          auto usb_serial = read_first_line(usb_root / "serial");

          if (!is_supported_device(usb_vid, usb_pid)) {
            continue;
          }

          process_usb_device(*config, tty_name, usb_vid, usb_pid, usb_serial);
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

        // Output is a JSON object
        // - key is device ID
        // - value is port number if a proxy is active and null if not
        auto doc = crow::json::wvalue(crow::json::wvalue::object());

        // Set the device ID of all detected devices
        for (const auto &[tty_name, device_id] : detected_tty)
        {
            doc[device_id] = nullptr;
        }

        // Set the device IDs of all proxied devices, mapped to proxy TCP port
        // This will overwrite the null values set for detected devices
        for (const auto &[tty_name, conn] : connections)
        {
            doc[conn.device_id] = conn.port;
        }

        return doc;
    };

    CROW_ROUTE(app, "/usb-proxy/discover/<string>").methods("GET"_method)(discover);

    // Only use a single thread to handle requests
    // The discovery endpoint is not thread-safe
    app
        .loglevel(crow::LogLevel::Warning)
        .port(SERVICE_PORT)
        .run();
}
