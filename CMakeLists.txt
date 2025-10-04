cmake_minimum_required(VERSION 3.22)

set(PNAME tmplay)
set(PLANG CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(${PNAME} ${PLANG})

find_path(MINIAUDIO "miniaudio.h")
find_package(FFMPEG REQUIRED)
find_package(nlohmann_json REQUIRED)

include(FetchContent)
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v6.1.9
)
FetchContent_MakeAvailable(ftxui)

set(SOURCES 
    "${CMAKE_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/maudio.cpp"
)
set(INCLUDES "${CMAKE_SOURCE_DIR}/include" ${FFMPEG_INCLUDE_DIRS})

set(LINK_LIBRARIES 
    ftxui::dom  
    ftxui::component
    ftxui::screen
    nlohmann_json::nlohmann_json 
    ${FFMPEG_LIBRARIES}
)
# set(COMPILE_OPTIONS <OPTS>)
set(COMPILE_DEFINITIONS NOMINMAX)

set(ADD_MSVC_COMPILE_OPTS /W4 /WX /EHsc)
set(ADD_GCC_CLANG_COMPILE_OPTS -Wall -Wextra -Wpedantic -Werror)

add_executable(${PNAME})

target_sources(${PNAME} PRIVATE ${SOURCES})
target_include_directories(${PNAME} PRIVATE ${INCLUDES})

target_link_libraries(${PNAME} PRIVATE ${LINK_LIBRARIES})
# target_compile_options(${PNAME} PRIVATE ${COMPILE_OPTIONS})
target_compile_definitions(${PNAME} PRIVATE ${COMPILE_DEFINITIONS})

if (MSVC) 
    target_compile_options(${PNAME} PRIVATE ${ADD_MSVC_COMPILE_OPTS})
else() 
    target_compile_options(${PNAME} PRIVATE ${ADD_GCC_CLANG_COMPILE_OPTS})
endif()
