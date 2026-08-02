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
    src/resource/TextureStore.cpp
    src/text/FontStore.cpp
    src/text/TextLayout.cpp
    src/text/Utf8.cpp
    src/widget/UiDocument.cpp
    src/widget/Widget.cpp
    src/widget/controls/Button.cpp
    src/widget/controls/Label.cpp
    src/widget/controls/NumericInput.cpp
    src/widget/controls/Panel.cpp
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

add_library(HeniaUIGfx STATIC
    src/gfx/ShapeBatch3D.cpp
)
add_library(HeniaUI::Gfx ALIAS HeniaUIGfx)
target_compile_features(HeniaUIGfx PUBLIC cxx_std_23)
target_include_directories(
    HeniaUIGfx
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if(MSVC)
    target_compile_options(HeniaUIGfx PRIVATE /W4 /permissive- /utf-8 /GR-)
else()
    target_compile_options(HeniaUIGfx PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -fno-rtti)
endif()
set_target_properties(HeniaUIGfx PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME Gfx
    FOLDER "HeniaUI"
)

if(WIN32)
    add_library(HeniaUIWin32 STATIC
        src/platform/win32/Win32FontLoader.cpp
        src/platform/win32/Win32InputAdapter.cpp
    )
    add_library(HeniaUI::Win32 ALIAS HeniaUIWin32)
    target_compile_features(HeniaUIWin32 PUBLIC cxx_std_23)
    target_include_directories(
        HeniaUIWin32
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(HeniaUIWin32 PUBLIC HeniaUI::Core PRIVATE gdi32)
    if(MSVC)
        target_compile_options(HeniaUIWin32 PRIVATE /W4 /permissive- /utf-8 /GR-)
    endif()
    set_target_properties(HeniaUIWin32 PROPERTIES
        CXX_EXTENSIONS OFF
        EXPORT_NAME Win32
        FOLDER "HeniaUI/Platform"
    )

    add_library(HeniaUIOpenGL STATIC
        src/backend/opengl/OpenGlRenderer.cpp
        src/backend/opengl/OpenGlRenderDevice.cpp
    )
    add_library(HeniaUI::OpenGL ALIAS HeniaUIOpenGL)
    target_compile_features(HeniaUIOpenGL PUBLIC cxx_std_23)
    target_include_directories(
        HeniaUIOpenGL
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(HeniaUIOpenGL PUBLIC HeniaUI::Core HeniaUI::Gfx PRIVATE opengl32)
    if(MSVC)
        target_compile_options(HeniaUIOpenGL PRIVATE /W4 /permissive- /utf-8 /GR-)
    endif()
    set_target_properties(HeniaUIOpenGL PROPERTIES
        CXX_EXTENSIONS OFF
        EXPORT_NAME OpenGL
        FOLDER "HeniaUI/Backends"
    )

    add_library(HeniaUID3D12 STATIC
        src/backend/d3d12/D3D12Renderer.cpp
        src/backend/d3d12/D3D12RenderDevice.cpp
    )
    add_library(HeniaUI::D3D12 ALIAS HeniaUID3D12)
    target_compile_features(HeniaUID3D12 PUBLIC cxx_std_23)
    target_include_directories(
        HeniaUID3D12
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(
        HeniaUID3D12
        PUBLIC HeniaUI::Core HeniaUI::Gfx
        PRIVATE d3d12 d3dcompiler dxgi
    )
    target_compile_definitions(HeniaUID3D12 PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(HeniaUID3D12 PRIVATE /W4 /permissive- /utf-8 /GR-)
    endif()
    set_target_properties(HeniaUID3D12 PROPERTIES
        CXX_EXTENSIONS OFF
        EXPORT_NAME D3D12
        FOLDER "HeniaUI/Backends"
    )
endif()

if(HENIAUI_BUILD_SANDBOX)
    add_executable(HeniaUISandbox sandbox/main.cpp)
    target_link_libraries(HeniaUISandbox PRIVATE HeniaUI::Core)
    target_compile_features(HeniaUISandbox PRIVATE cxx_std_23)
    set_target_properties(HeniaUISandbox PROPERTIES FOLDER "HeniaUI/Sandbox")

    if(WIN32)
        add_executable(HeniaUIVisualSandbox WIN32 sandbox/win32/VisualSandbox.cpp)
        target_link_libraries(
            HeniaUIVisualSandbox
            PRIVATE
                HeniaUI::Core
                HeniaUI::OpenGL
                HeniaUI::Win32
                opengl32
                user32
        )
        target_compile_features(HeniaUIVisualSandbox PRIVATE cxx_std_23)
        if(MSVC)
            target_compile_options(HeniaUIVisualSandbox PRIVATE /W4 /permissive- /utf-8 /GR-)
        endif()
        set_target_properties(HeniaUIVisualSandbox PROPERTIES FOLDER "HeniaUI/Sandbox")
    endif()
endif()

if(HENIAUI_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    add_executable(HeniaUITests tests/CoreTests.cpp)
    target_link_libraries(HeniaUITests PRIVATE HeniaUI::Core Threads::Threads)
    target_compile_features(HeniaUITests PRIVATE cxx_std_23)
    set_target_properties(HeniaUITests PROPERTIES FOLDER "HeniaUI/Tests")
    add_test(NAME HeniaUI.Core COMMAND HeniaUITests)

    add_executable(HeniaUIWidgetTests tests/WidgetTests.cpp)
    target_link_libraries(HeniaUIWidgetTests PRIVATE HeniaUI::Core)
    target_compile_features(HeniaUIWidgetTests PRIVATE cxx_std_23)
    set_target_properties(HeniaUIWidgetTests PROPERTIES FOLDER "HeniaUI/Tests")
    add_test(NAME HeniaUI.Widgets COMMAND HeniaUIWidgetTests)

    add_executable(HeniaUIGfxTests tests/GfxTests.cpp)
    target_link_libraries(HeniaUIGfxTests PRIVATE HeniaUI::Gfx)
    target_compile_features(HeniaUIGfxTests PRIVATE cxx_std_23)
    set_target_properties(HeniaUIGfxTests PROPERTIES FOLDER "HeniaUI/Tests")
    add_test(NAME HeniaUI.Gfx COMMAND HeniaUIGfxTests)

    add_executable(HeniaUIOpenGlUploadRingTests tests/OpenGlUploadRingTests.cpp)
    target_include_directories(
        HeniaUIOpenGlUploadRingTests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/opengl
    )
    target_compile_features(HeniaUIOpenGlUploadRingTests PRIVATE cxx_std_23)
    set_target_properties(HeniaUIOpenGlUploadRingTests PROPERTIES FOLDER "HeniaUI/Tests")
    add_test(NAME HeniaUI.OpenGL.UploadRing COMMAND HeniaUIOpenGlUploadRingTests)

    if(WIN32)
        add_executable(HeniaUIWin32Tests tests/Win32Tests.cpp)
        target_link_libraries(HeniaUIWin32Tests PRIVATE HeniaUI::Core HeniaUI::Win32)
        target_compile_features(HeniaUIWin32Tests PRIVATE cxx_std_23)
        if(MSVC)
            target_compile_options(HeniaUIWin32Tests PRIVATE /W4 /permissive- /utf-8 /GR-)
        endif()
        set_target_properties(HeniaUIWin32Tests PROPERTIES FOLDER "HeniaUI/Tests")
        add_test(NAME HeniaUI.Win32 COMMAND HeniaUIWin32Tests)

        add_executable(HeniaUID3D12Tests tests/D3D12Tests.cpp)
        target_link_libraries(
            HeniaUID3D12Tests
            PRIVATE
                HeniaUI::Core
                HeniaUI::D3D12
                d3d12
                dxgi
        )
        target_compile_features(HeniaUID3D12Tests PRIVATE cxx_std_23)
        target_compile_definitions(HeniaUID3D12Tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        if(MSVC)
            target_compile_options(HeniaUID3D12Tests PRIVATE /W4 /permissive- /utf-8 /GR-)
        endif()
        set_target_properties(HeniaUID3D12Tests PROPERTIES FOLDER "HeniaUI/Tests")
        add_test(NAME HeniaUI.D3D12 COMMAND HeniaUID3D12Tests)

        add_executable(HeniaUID3D12GfxTests tests/D3D12GfxTests.cpp)
        target_link_libraries(
            HeniaUID3D12GfxTests
            PRIVATE
                HeniaUI::Gfx
                HeniaUI::D3D12
                d3d12
                dxgi
        )
        target_compile_features(HeniaUID3D12GfxTests PRIVATE cxx_std_23)
        target_compile_definitions(HeniaUID3D12GfxTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        if(MSVC)
            target_compile_options(HeniaUID3D12GfxTests PRIVATE /W4 /permissive- /utf-8 /GR-)
        endif()
        set_target_properties(HeniaUID3D12GfxTests PROPERTIES FOLDER "HeniaUI/Tests")
        add_test(NAME HeniaUI.D3D12.Gfx COMMAND HeniaUID3D12GfxTests)
    endif()
endif()

install(
    TARGETS HeniaUI HeniaUIGfx
    EXPORT HeniaUITargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
if(TARGET HeniaUIWin32)
    install(
        TARGETS HeniaUIWin32
        EXPORT HeniaUITargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()
if(TARGET HeniaUIOpenGL)
    install(
        TARGETS HeniaUIOpenGL
        EXPORT HeniaUITargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()
if(TARGET HeniaUID3D12)
    install(
        TARGETS HeniaUID3D12
        EXPORT HeniaUITargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()
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
