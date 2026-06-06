cmake_minimum_required(VERSION 3.16)
project(my_bedrock_bot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(BedrockProtocol REQUIRED)

add_executable(my_bedrock_bot main.cpp)
target_link_libraries(my_bedrock_bot PRIVATE BedrockProtocol::bedrock_protocol)
