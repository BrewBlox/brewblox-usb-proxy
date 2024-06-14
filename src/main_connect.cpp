#include <iostream>
#include "discover.hpp"

extern "C"
{
#include <dirent.h>
#include <sys/stat.h>
}

int main()
{
    std::cout << "Content-type: application/json"
              << std::endl
              << std::endl;

    std::cout << "{ \"devices\": [";

    if (auto *dir = opendir("/sys/class/tty"))
    {
        while (auto *entry = readdir(dir))
        {
            auto name = std::string(entry->d_name);
            if (!name.starts_with("ttyACM"))
            {
                continue;
            }
            std::cout << "\"" << name << "\",";
        }
        closedir(dir);
    }

    std::cout << "\"\"]}" << std::endl;
    return 0;
}
