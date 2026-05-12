cmake_minimum_required(VERSION 3.16)
project(mgadump VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mgadump
    src/main.cpp
    src/mga.cpp
    src/output.cpp
    src/profile.cpp
    src/dumper.cpp
)

# nlohmann/json はヘッダファイルを include に配置して使用
# https://github.com/nlohmann/json/releases から json.hpp を取得してください
target_include_directories(mgadump PRIVATE src include)

install(TARGETS mgadump DESTINATION bin)
install(FILES mga_profiles.json DESTINATION share/mgadump)