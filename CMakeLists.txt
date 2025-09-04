cmake_minimum_required(VERSION 3.22)
project(tmplay CXX)

find_path(MINIAUDIO "miniaudio.h")
find_package(yaml-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
  GIT_TAG v6.1.9
)
FetchContent_MakeAvailable(ftxui)

add_executable(tmplay)
target_sources(tmplay PRIVATE 
    "${CMAKE_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/player.cpp"
)
target_compile_options(tmplay PRIVATE /EHsc)
target_include_directories(tmplay PRIVATE "${CMAKE_SOURCE_DIR}/include/" ${MINIAUDIO})
target_link_libraries(tmplay PRIVATE ftxui::dom ftxui::screen ftxui::component yaml-cpp::yaml-cpp nlohmann_json::nlohmann_json)
