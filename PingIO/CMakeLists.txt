cmake_minimum_required(VERSION 3.10)
project(PingIO)

set(CMAKE_CXX_STANDARD 17)

# Include Crow headers
include_directories('C:\Users\braha\OneDrive\Desktop\devansh\PingIO\Crow/path/to/crow/include')

# Include uWebSockets headers
include_directories('C:\Users\braha\OneDrive\Desktop\devansh\PingIO\uWebSocketspath/to/uWebSockets/src')

# Add the executable
add_executable(PingIO main.cpp)

# Link uWebSockets library
target_link_libraries(PingIO PRIVATE 'C:\Users\braha\OneDrive\Desktop\devansh\PingIO\uWebSockets/path/to/uWebSockets/build/libuWS.a')
