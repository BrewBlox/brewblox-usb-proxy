cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)
set(CMAKE_RULE_MESSAGES OFF)

cmake_policy(SET CMP0076 NEW)

# Declare the project
project(usb_proxy)
add_executable(usb_proxy src/main.cpp)

# Get system variables
get_filename_component(APP_ROOT
  ${CMAKE_CURRENT_LIST_DIR}/
  ABSOLUTE
)

# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -ggdb")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -ggdb")
