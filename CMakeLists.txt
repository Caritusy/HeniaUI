cmake_minimum_required(VERSION 3.24)

project(HeniaUI VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(HENIAUI_BUILD_TESTS "Build HeniaUI tests" ${PROJECT_IS_TOP_LEVEL})
option(HENIAUI_BUILD_SANDBOX "Build the batching sandbox" ${PROJECT_IS_TOP_LEVEL})
option(HENIAUI_BUILD_BENCHMARKS "Build reproducible HeniaUI benchmarks" ${HENIAUI_BUILD_TESTS})
option(HENIAUI_ENABLE_ADDRESS_SANITIZER "Build with AddressSanitizer" OFF)
option(HENIAUI_ENABLE_UNDEFINED_SANITIZER "Build with UndefinedBehaviorSanitizer" OFF)

set(HENIAUI_ADDRESS_SANITIZER_RUNTIME_DIRECTORY "")
if(HENIAUI_ENABLE_ADDRESS_SANITIZER)
    if(MSVC)
        add_compile_options(/fsanitize=address /Zi)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            # clang-cl ASan does not support the debug CRT (/MDd). Keep debug
            # symbols while using the supported dynamic release CRT.
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
            execute_process(
                COMMAND ${CMAKE_CXX_COMPILER} --print-resource-dir
                OUTPUT_VARIABLE HENIAUI_CLANG_RESOURCE_DIRECTORY
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            set(
                HENIAUI_ADDRESS_SANITIZER_RUNTIME_DIRECTORY
                "${HENIAUI_CLANG_RESOURCE_DIRECTORY}/lib/windows"
            )
            set(
                HENIAUI_CLANG_ASAN_DYNAMIC_LIBRARY
                "${HENIAUI_ADDRESS_SANITIZER_RUNTIME_DIRECTORY}/clang_rt.asan_dynamic-x86_64.lib"
            )
            set(
                HENIAUI_CLANG_ASAN_THUNK_LIBRARY
                "${HENIAUI_ADDRESS_SANITIZER_RUNTIME_DIRECTORY}/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib"
            )
            if(NOT EXISTS "${HENIAUI_CLANG_ASAN_DYNAMIC_LIBRARY}"
                OR NOT EXISTS "${HENIAUI_CLANG_ASAN_THUNK_LIBRARY}")
                message(FATAL_ERROR "clang-cl AddressSanitizer runtime libraries were not found")
            endif()
            # Visual Studio generators invoke lld-link directly, bypassing the
            # clang driver that would normally add compiler-rt at final link.
            link_libraries("${HENIAUI_CLANG_ASAN_DYNAMIC_LIBRARY}")
            add_link_options("/WHOLEARCHIVE:${HENIAUI_CLANG_ASAN_THUNK_LIBRARY}")
        else()
            get_filename_component(
                HENIAUI_ADDRESS_SANITIZER_RUNTIME_DIRECTORY
                "${CMAKE_CXX_COMPILER}"
                DIRECTORY
            )
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    else()
        message(FATAL_ERROR "AddressSanitizer is unsupported by ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

if(HENIAUI_ENABLE_UNDEFINED_SANITIZER)
    if(MSVC)
        message(FATAL_ERROR "UndefinedBehaviorSanitizer requires Clang or GCC in this project")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-fsanitize=undefined -fno-sanitize-recover=all)
        add_link_options(-fsanitize=undefined -fno-sanitize-recover=all)
    else()
        message(FATAL_ERROR "UndefinedBehaviorSanitizer is unsupported by ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

set(HENIAUI_DISABLE_RTTI_OPTION -fno-rtti)
if(HENIAUI_ENABLE_UNDEFINED_SANITIZER)
    # UBSan's vptr checks require RTTI. Sanitizer builds enable it so those
    # checks remain active instead of suppressing a category of project bugs.
    set(HENIAUI_DISABLE_RTTI_OPTION "")
endif()

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
    target_compile_options(
        HeniaUI
        PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow ${HENIAUI_DISABLE_RTTI_OPTION}
    )
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
    target_compile_options(
        HeniaUIGfx
        PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow ${HENIAUI_DISABLE_RTTI_OPTION}
    )
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

if(HENIAUI_BUILD_BENCHMARKS AND NOT HENIAUI_ENABLE_ADDRESS_SANITIZER)
    add_executable(HeniaUIBenchmarks benchmarks/main.cpp)
    target_link_libraries(HeniaUIBenchmarks PRIVATE HeniaUI::Core HeniaUI::Gfx)
    target_compile_features(HeniaUIBenchmarks PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(HeniaUIBenchmarks PRIVATE /W4 /permissive- /utf-8 /GR-)
    else()
        target_compile_options(
            HeniaUIBenchmarks
            PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow ${HENIAUI_DISABLE_RTTI_OPTION}
        )
    endif()
    set_target_properties(HeniaUIBenchmarks PROPERTIES FOLDER "HeniaUI/Benchmarks")

    if(WIN32)
        add_executable(
            HeniaUID3D12InstanceBenchmarks
            benchmarks/D3D12InstanceStorage.cpp
        )
        target_link_libraries(
            HeniaUID3D12InstanceBenchmarks
            PRIVATE
                HeniaUI::Gfx
                HeniaUI::D3D12
                d3d12
                d3dcompiler
                dxgi
        )
        target_compile_features(HeniaUID3D12InstanceBenchmarks PRIVATE cxx_std_23)
        target_compile_definitions(
            HeniaUID3D12InstanceBenchmarks
            PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
        )
        if(MSVC)
            target_compile_options(
                HeniaUID3D12InstanceBenchmarks
                PRIVATE /W4 /permissive- /utf-8 /GR-
            )
        endif()
        set_target_properties(
            HeniaUID3D12InstanceBenchmarks
            PROPERTIES FOLDER "HeniaUI/Benchmarks"
        )
    endif()
endif()

if(HENIAUI_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    if(TARGET HeniaUIBenchmarks)
        add_test(
            NAME HeniaUI.Benchmarks.Verify
            COMMAND HeniaUIBenchmarks --verify --iterations 3 --warmup 2
        )
        set_tests_properties(HeniaUI.Benchmarks.Verify PROPERTIES TIMEOUT 60)
    endif()
    add_executable(HeniaUITests tests/CoreTests.cpp)
    target_link_libraries(HeniaUITests PRIVATE HeniaUI::Core Threads::Threads)
    target_compile_features(HeniaUITests PRIVATE cxx_std_23)
    set_target_properties(HeniaUITests PROPERTIES FOLDER "HeniaUI/Tests")
    add_test(NAME HeniaUI.Core COMMAND HeniaUITests)

    add_executable(HeniaUIWidgetTests tests/WidgetTests.cpp)
    target_link_libraries(HeniaUIWidgetTests PRIVATE HeniaUI::Core)
    target_compile_features(HeniaUIWidgetTests PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(HeniaUIWidgetTests PRIVATE /GR-)
    elseif(HENIAUI_DISABLE_RTTI_OPTION)
        target_compile_options(HeniaUIWidgetTests PRIVATE ${HENIAUI_DISABLE_RTTI_OPTION})
    endif()
    set_target_properties(HeniaUIWidgetTests PROPERTIES FOLDER "HeniaUI/Tests")
    add_test(NAME HeniaUI.Widgets COMMAND HeniaUIWidgetTests)

    add_executable(HeniaUIRetainedWidgetTests tests/RetainedWidgetTests.cpp)
    target_link_libraries(HeniaUIRetainedWidgetTests PRIVATE HeniaUI::Core)
    target_compile_features(HeniaUIRetainedWidgetTests PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(HeniaUIRetainedWidgetTests PRIVATE /GR-)
    elseif(HENIAUI_DISABLE_RTTI_OPTION)
        target_compile_options(HeniaUIRetainedWidgetTests PRIVATE ${HENIAUI_DISABLE_RTTI_OPTION})
    endif()
    set_target_properties(HeniaUIRetainedWidgetTests PROPERTIES FOLDER "HeniaUI/Tests")
    add_test(NAME HeniaUI.Widgets.RetainedSubtrees COMMAND HeniaUIRetainedWidgetTests)

    add_executable(HeniaUIGfxTests tests/GfxTests.cpp)
    target_link_libraries(HeniaUIGfxTests PRIVATE HeniaUI::Gfx)
    target_compile_features(HeniaUIGfxTests PRIVATE cxx_std_23)
    set_target_properties(HeniaUIGfxTests PROPERTIES FOLDER "HeniaUI/Tests")
    add_test(NAME HeniaUI.Gfx COMMAND HeniaUIGfxTests)

    add_executable(HeniaUIValidationTests tests/ValidationTests.cpp)
    target_link_libraries(HeniaUIValidationTests PRIVATE HeniaUI::Core HeniaUI::Gfx)
    target_compile_features(HeniaUIValidationTests PRIVATE cxx_std_23)
    set_target_properties(HeniaUIValidationTests PROPERTIES FOLDER "HeniaUI/Tests")
    add_test(NAME HeniaUI.Validation COMMAND HeniaUIValidationTests)

    add_executable(HeniaUIOpenGlUploadRingTests tests/OpenGlUploadRingTests.cpp)
    target_include_directories(
        HeniaUIOpenGlUploadRingTests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/opengl
    )
    target_compile_features(HeniaUIOpenGlUploadRingTests PRIVATE cxx_std_23)
    set_target_properties(HeniaUIOpenGlUploadRingTests PROPERTIES FOLDER "HeniaUI/Tests")
    add_test(NAME HeniaUI.OpenGL.UploadRing COMMAND HeniaUIOpenGlUploadRingTests)

    add_executable(
        HeniaUIOpenGlTextureTransactionTests
        tests/OpenGlTextureTransactionTests.cpp
    )
    target_include_directories(
        HeniaUIOpenGlTextureTransactionTests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/opengl
    )
    target_compile_features(HeniaUIOpenGlTextureTransactionTests PRIVATE cxx_std_23)
    set_target_properties(
        HeniaUIOpenGlTextureTransactionTests
        PROPERTIES FOLDER "HeniaUI/Tests"
    )
    add_test(
        NAME HeniaUI.OpenGL.TextureTransaction
        COMMAND HeniaUIOpenGlTextureTransactionTests
    )

    add_executable(
        HeniaUID3D12TextureUploadTransactionTests
        tests/D3D12TextureUploadTransactionTests.cpp
    )
    target_include_directories(
        HeniaUID3D12TextureUploadTransactionTests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/d3d12
    )
    target_compile_features(HeniaUID3D12TextureUploadTransactionTests PRIVATE cxx_std_23)
    set_target_properties(
        HeniaUID3D12TextureUploadTransactionTests
        PROPERTIES FOLDER "HeniaUI/Tests"
    )
    add_test(
        NAME HeniaUI.D3D12.TextureUploadTransaction
        COMMAND HeniaUID3D12TextureUploadTransactionTests
    )

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
                d3dcompiler
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
                d3dcompiler
                dxgi
        )
        target_compile_features(HeniaUID3D12GfxTests PRIVATE cxx_std_23)
        target_compile_definitions(HeniaUID3D12GfxTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        if(MSVC)
            target_compile_options(HeniaUID3D12GfxTests PRIVATE /W4 /permissive- /utf-8 /GR-)
        endif()
        set_target_properties(HeniaUID3D12GfxTests PROPERTIES FOLDER "HeniaUI/Tests")
        add_test(NAME HeniaUI.D3D12.Gfx COMMAND HeniaUID3D12GfxTests)

        add_executable(HeniaUIOpenGlOutputTests tests/OpenGlOutputTests.cpp)
        target_link_libraries(
            HeniaUIOpenGlOutputTests
            PRIVATE
                HeniaUI::Core
                HeniaUI::OpenGL
                opengl32
                user32
        )
        target_compile_features(HeniaUIOpenGlOutputTests PRIVATE cxx_std_23)
        target_compile_definitions(
            HeniaUIOpenGlOutputTests
            PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
        )
        if(MSVC)
            target_compile_options(HeniaUIOpenGlOutputTests PRIVATE /W4 /permissive- /utf-8 /GR-)
        endif()
        set_target_properties(HeniaUIOpenGlOutputTests PROPERTIES FOLDER "HeniaUI/Tests")
        add_test(NAME HeniaUI.OpenGL.Output COMMAND HeniaUIOpenGlOutputTests)
        set_tests_properties(HeniaUI.OpenGL.Output PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    if(HENIAUI_ADDRESS_SANITIZER_RUNTIME_DIRECTORY)
        get_property(HENIAUI_TEST_NAMES DIRECTORY PROPERTY TESTS)
        set_tests_properties(
            ${HENIAUI_TEST_NAMES}
            PROPERTIES ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${HENIAUI_ADDRESS_SANITIZER_RUNTIME_DIRECTORY}"
        )
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
