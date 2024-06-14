#include <vector>
#include <string>

namespace proxy
{

    struct SparkDevice
    {
        std::string serial = "";
        std::string device_path = "";
    };

    std::vector<SparkDevice> discover_devices()
    {
        return {};
    }

} // namespace proxy
