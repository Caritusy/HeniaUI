cmake_minimum_required(VERSION 3.24)

project(HeniaUI VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(HENIAUI_BUILD_TESTS "Build HeniaUI tests" ${PROJECT_IS_TOP_LEVEL})
option(HENIAUI_BUILD_SANDBOX "Build the batching sandbox" ${PROJECT_IS_TOP_LEVEL})

add_library(HeniaUI STATIC
    src/BatchCompiler.cpp
    src/Canvas.cpp
    src/DisplayList.cpp
    src/Frame.cpp
    src/RenderPacket.cpp
)
add_library(HeniaUI::Core ALIAS HeniaUI)

target_compile_features(HeniaUI PUBLIC cxx_std_23)
target_include_directories(
    HeniaUI
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(MSVC)
    target_compile_options(HeniaUI PRIVATE /W4 /permissive- /utf-8 /GR-)
else()
    target_compile_options(HeniaUI PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -fno-rtti)
endif()

set_target_properties(HeniaUI PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME Core
    FOLDER "HeniaUI"
)

if(HENIAUI_BUILD_SANDBOX)
    add_executable(HeniaUISandbox sandbox/main.cpp)
    target_link_libraries(HeniaUISandbox PRIVATE HeniaUI::Core)
    target_compile_features(HeniaUISandbox PRIVATE cxx_std_23)
    set_target_properties(HeniaUISandbox PROPERTIES FOLDER "HeniaUI/Sandbox")
endif()

if(HENIAUI_BUILD_TESTS)
    enable_testing()
    add_executable(HeniaUITests tests/CoreTests.cpp)
    target_link_libraries(HeniaUITests PRIVATE HeniaUI::Core)
    target_compile_features(HeniaUITests PRIVATE cxx_std_23)
    set_target_properties(HeniaUITests PROPERTIES FOLDER "HeniaUI/Tests")
    add_test(NAME HeniaUI.Core COMMAND HeniaUITests)
endif()

install(
    TARGETS HeniaUI
    EXPORT HeniaUITargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    EXPORT HeniaUITargets
    FILE HeniaUITargets.cmake
    NAMESPACE HeniaUI::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HeniaUI
)

configure_package_config_file(
    cmake/HeniaUIConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/HeniaUIConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HeniaUI
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/HeniaUIConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/HeniaUIConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/HeniaUIConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HeniaUI
)
