cmake_minimum_required(VERSION 3.20)
project(loadDriver)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(loadDriver SHARED
        loader.cpp
        kdmapper/kdmapper.cpp
        kdmapper/intel_driver.cpp
        kdmapper/portable_executable.cpp
        kdmapper/utils.cpp
        kdmapper/service.cpp
)

target_include_directories(loadDriver PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/kdmapper
)

target_compile_definitions(loadDriver PRIVATE DRIVERLOADER_EXPORTS)

target_link_libraries(loadDriver PRIVATE
        ntdll
        ws2_32
        advapi32
)