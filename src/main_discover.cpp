#include <iostream>
#include "discover.hpp"

int main()
{
    std::cout << "Content-type: application/json"
              << std::endl
              << std::endl
              << "{\"entrypoint\": \"discover\"}"
              << std::endl;
    return 0;
}
