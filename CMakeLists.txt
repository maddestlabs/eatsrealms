cmake_minimum_required(VERSION 3.20)
project(eatsrealms VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(MSVC)
    add_compile_options(/W4 /O2 /permissive- /wd4244 /wd4267 /wd4324 /openmp)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
elseif(EMSCRIPTEN)
    add_compile_options(-Wall -Wextra -O3 -fexceptions --use-port=emdawnwebgpu)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -O3)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# 1. GLFW Windowing
if(NOT EMSCRIPTEN)
    # Bundled GLFW 3.4 (Zero network fetch required)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)
endif()

# 2. Standalone Eatscript Scripting & Host ABI Library
add_library(eats_script STATIC
    src/eatscript/lexer.cpp
    src/eatscript/parser.cpp
    src/eatscript/vm.cpp
    src/eatscript/transpiler.cpp
    src/eatscript/evaluator.cpp
    src/eatscript/host_abi.cpp
    src/eatscript/stdlib.cpp
    src/eatscript/repl.cpp
    src/eatscript/dispatch_scanner.cpp
)
target_include_directories(eats_script PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 3. Eatsrealms HD-2D Core (Procedural PixelCanvas & Normal Atlas, Multi-Height TileMap, PropFactory & Foley)
add_library(eatsrealms_core STATIC
    src/procgen/pixel_canvas.cpp
    src/world/realm_world.cpp
    src/world/script_bindings.cpp
    src/audio/audio_foley.cpp
)
target_include_directories(eatsrealms_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
)
target_link_libraries(eatsrealms_core PUBLIC eats_script)

if(WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(eatsrealms_core PUBLIC ole32 winmm)
elseif(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(eatsrealms_core PUBLIC pthread dl m)
endif()

# 4. Eatsrealms WebGPU / Dawn HD-2D Diorama Renderer Library
add_library(eatsrealms_render STATIC
    src/render/hd2d_renderer.cpp
)
target_include_directories(eatsrealms_render PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(NOT EMSCRIPTEN)
    target_include_directories(eatsrealms_render PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wgpu/include
    )
endif()

if(EMSCRIPTEN)
    target_link_libraries(eatsrealms_render PUBLIC eatsrealms_core)
else()
    target_link_libraries(eatsrealms_render PUBLIC eatsrealms_core glfw)
    if(WIN32)
        set(WGPU_NATIVE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wgpu/lib/wgpu_native.dll.lib")
        target_link_libraries(eatsrealms_render PUBLIC
            user32 gdi32 shell32 opengl32 "${WGPU_NATIVE_LIB}"
        )
    endif()
endif()

# 5. Main Eatsrealms Executable
if(EMSCRIPTEN)
    add_executable(eatsrealms src/main.cpp)
    target_compile_options(eatsrealms PRIVATE -O3 -fexceptions)
    target_compile_definitions(eatsrealms PRIVATE NDEBUG)
    target_link_libraries(eatsrealms PRIVATE eatsrealms_render)
    set_target_properties(eatsrealms PROPERTIES
        OUTPUT_NAME "eatsrealms"
        SUFFIX ".html"
        LINK_FLAGS "-O3 -sNO_EXIT_RUNTIME=1 -fexceptions --use-port=emdawnwebgpu -sUSE_GLFW=3 -sALLOW_MEMORY_GROWTH=1 --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/scenes@/scenes --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html"
    )
    add_custom_command(TARGET eatsrealms POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/eatsrealms.html"
            "${CMAKE_BINARY_DIR}/index.html"
    )
else()
    add_executable(eatsrealms src/main.cpp)
    target_link_libraries(eatsrealms PRIVATE eatsrealms_render)

    # Copy wgpu_native.dll next to executables on Windows
    if(WIN32)
        add_custom_command(TARGET eatsrealms POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wgpu/lib/wgpu_native.dll"
                "$<TARGET_FILE_DIR:eatsrealms>/wgpu_native.dll"
        )
    endif()

    # 6. Automated Unit Test Suite
    enable_testing()
    add_executable(eatsrealms_tests tests/hd2d_core_tests.cpp)
    target_link_libraries(eatsrealms_tests PRIVATE eatsrealms_core)
    add_test(NAME eatsrealms_tests COMMAND eatsrealms_tests)
endif()
