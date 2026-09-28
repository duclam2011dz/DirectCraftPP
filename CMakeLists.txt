cmake_minimum_required(VERSION 3.25)

project(DirectCraft VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "DirectCraft++ v1.0.0 requires Windows and DirectX 12.")
endif()

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(directcraft_core
    src/voxel/voxel.cpp
)
target_include_directories(directcraft_core PUBLIC src)
target_compile_definitions(directcraft_core PUBLIC NOMINMAX UNICODE _UNICODE)

add_executable(DirectCraft WIN32
    src/main.cpp
    src/platform/win32_window.cpp
    src/renderer/d3d12_renderer.cpp
)
target_link_libraries(DirectCraft PRIVATE directcraft_core d3d12 dxgi dxguid d3dcompiler)
target_compile_definitions(DirectCraft PRIVATE NOMINMAX UNICODE _UNICODE DIRECTCRAFT_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
target_compile_options(DirectCraft PRIVATE /W4 /permissive-)

add_custom_command(TARGET DirectCraft POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:DirectCraft>/shaders"
)

if(BUILD_TESTING)
    add_executable(DirectCraftUnitTests tests/unit_tests.cpp)
    target_link_libraries(DirectCraftUnitTests PRIVATE directcraft_core)
    target_compile_definitions(DirectCraftUnitTests PRIVATE NOMINMAX UNICODE _UNICODE)
    add_test(NAME DirectCraft.UnitTests COMMAND DirectCraftUnitTests)

    add_test(NAME DirectCraft.GpuSmoke
        COMMAND DirectCraft --render-test "${CMAKE_CURRENT_BINARY_DIR}/directcraft_smoke.bmp")
    set_tests_properties(DirectCraft.GpuSmoke PROPERTIES TIMEOUT 30)
    add_test(NAME DirectCraft.GpuGolden COMMAND ${CMAKE_COMMAND} -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/directcraft_smoke.bmp -DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/directcraft_smoke.bmp -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_bmp.cmake)
    set_tests_properties(DirectCraft.GpuGolden PROPERTIES DEPENDS DirectCraft.GpuSmoke TIMEOUT 5)
endif()


