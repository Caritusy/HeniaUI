cmake_minimum_required(VERSION 3.24)
project(HeniaUIPackageConsumer LANGUAGES CXX)

find_package(HeniaUI 0.1 REQUIRED)

add_executable(HeniaUIPackageConsumer main.cpp)
target_compile_features(HeniaUIPackageConsumer PRIVATE cxx_std_23)
target_link_libraries(HeniaUIPackageConsumer PRIVATE HeniaUI::Core HeniaUI::Gfx)
if(WIN32)
    target_link_libraries(HeniaUIPackageConsumer PRIVATE HeniaUI::D3D12)
endif()
