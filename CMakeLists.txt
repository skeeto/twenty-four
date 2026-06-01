cmake_minimum_required(VERSION 3.24)
project(twenty_four CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependency: SDL3, fetched over HTTPS with a pinned SHA256, statically linked.
# Text is rendered with the vendored single-header stb_truetype (src/), which
# keeps the static + Emscripten build free of FreeType/HarfBuzz build friction.
# ---------------------------------------------------------------------------
include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)  # keep SDL out of our install/ output
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.24/SDL3-3.2.24.tar.gz
    URL_HASH SHA256=81cc0fc17e5bf2c1754eeca9af9c47a76789ac5efdd165b3b91cbbe4b90bfb76
)
FetchContent_MakeAvailable(SDL3)

if(TARGET SDL3::SDL3-static)
    set(SDL3_LIB SDL3::SDL3-static)
else()
    set(SDL3_LIB SDL3::SDL3)
endif()

# ---------------------------------------------------------------------------
# Generated source: the font embedded as a byte array. (The puzzle table is a
# committed header, src/puzzles.h.)
# ---------------------------------------------------------------------------
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})

set(FONT_HEADER ${GEN_DIR}/font_generated.h)
add_custom_command(
    OUTPUT ${FONT_HEADER}
    COMMAND ${CMAKE_COMMAND}
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/assets/font.ttf
            -DOUTPUT=${FONT_HEADER}
            -DSYMBOL=kFontData
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/font.ttf
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
    COMMENT "Embedding font into font_generated.h"
    VERBATIM
)

# ---------------------------------------------------------------------------
# The game executable.
# ---------------------------------------------------------------------------
add_executable(twenty_four
    src/main.cpp
    src/game.cpp
    src/render.cpp
    src/audio.cpp
    src/storage.cpp
    src/stb_impl.cpp
    ${FONT_HEADER}
)
target_include_directories(twenty_four PRIVATE ${GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(twenty_four PRIVATE ${SDL3_LIB})

if(NOT MSVC)
    target_compile_options(twenty_four PRIVATE -Wall -Wextra)
    # stb_truetype is third-party; don't hold it to our warning level.
    set_source_files_properties(src/stb_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

# ---------------------------------------------------------------------------
# Emscripten (PWA) vs desktop.
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    set_target_properties(twenty_four PROPERTIES OUTPUT_NAME index SUFFIX ".html")
    target_link_options(twenty_four PRIVATE
        "-sALLOW_MEMORY_GROWTH=1"
        "-sSTACK_SIZE=4MB"
        "--shell-file" "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    )
    set_property(TARGET twenty_four APPEND PROPERTY LINK_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
    foreach(asset manifest.webmanifest sw.js icon-192.png icon-512.png icon.svg)
        add_custom_command(TARGET twenty_four POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_CURRENT_SOURCE_DIR}/web/${asset}
                    $<TARGET_FILE_DIR:twenty_four>/${asset}
            VERBATIM)
    endforeach()

    # Install the full PWA into CMAKE_INSTALL_PREFIX so it can be dropped into a
    # gh-pages worktree and committed:
    #   cmake --install build-web --prefix /path/to/gh-pages-worktree
    # Everything uses relative paths, so it also works from a /<repo>/ subpath.
    install(FILES
        "$<TARGET_FILE:twenty_four>"                       # index.html
        "$<TARGET_FILE_DIR:twenty_four>/index.js"
        "$<TARGET_FILE_DIR:twenty_four>/index.wasm"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/manifest.webmanifest"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/sw.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/icon-192.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/icon-512.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/icon.svg"
        DESTINATION ".")
else()
    install(TARGETS twenty_four RUNTIME DESTINATION bin)
endif()
