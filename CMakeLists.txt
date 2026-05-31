cmake_minimum_required(VERSION 3.20)
project(imtile VERSION 0.1.0)

add_library(imtile INTERFACE)
target_include_directories(imtile INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(imtile INTERFACE cxx_std_20)

install(TARGETS imtile EXPORT imtileTargets)
install(DIRECTORY include/ DESTINATION include)
