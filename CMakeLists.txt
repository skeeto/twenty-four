cmake_minimum_required(VERSION 3.24)
project(twenty_four VERSION 1.0.0 LANGUAGES CXX)

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
# Generated sources: font + window-icon PNG embedded as byte arrays. (The
# puzzle table is a committed header, src/puzzles.h.)
# ---------------------------------------------------------------------------
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})

function(embed_asset out_var input symbol header_name)
    set(header ${GEN_DIR}/${header_name})
    add_custom_command(
        OUTPUT ${header}
        COMMAND ${CMAKE_COMMAND}
                -DINPUT=${input} -DOUTPUT=${header} -DSYMBOL=${symbol}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
        DEPENDS ${input} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
        COMMENT "Embedding ${input}"
        VERBATIM)
    set(${out_var} ${header} PARENT_SCOPE)
endfunction()

embed_asset(FONT_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/assets/font.ttf kFontData font_generated.h)

# Version macro available to the source.
add_compile_definitions(TF_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# The game executable.
# ---------------------------------------------------------------------------
set(GAME_SOURCES
    src/main.cpp
    src/game.cpp
    src/render.cpp
    src/audio.cpp
    src/storage.cpp
    src/stb_impl.cpp
    ${FONT_HEADER}
)

# Desktop builds embed a PNG window icon (decoded by stb_image at startup);
# the web build uses the page favicon instead.
if(NOT EMSCRIPTEN)
    embed_asset(ICON_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png kIconData icon_generated.h)
    list(APPEND GAME_SOURCES src/stb_image_impl.cpp ${ICON_HEADER})
endif()

# Windows: ship the EXE icon + version/file properties via a resource script,
# and build as a GUI app (no console window).
if(WIN32)
    set(TF_VERSION       ${PROJECT_VERSION})
    set(TF_VER_MAJOR     ${PROJECT_VERSION_MAJOR})
    set(TF_VER_MINOR     ${PROJECT_VERSION_MINOR})
    set(TF_VER_PATCH     ${PROJECT_VERSION_PATCH})
    set(TF_ICON_ICO      "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
    file(TO_NATIVE_PATH "${TF_ICON_ICO}" TF_ICON_ICO)
    string(REPLACE "\\" "\\\\" TF_ICON_ICO "${TF_ICON_ICO}")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/app.rc.in ${GEN_DIR}/app.rc @ONLY)
    list(APPEND GAME_SOURCES ${GEN_DIR}/app.rc)
endif()

add_executable(twenty_four ${GAME_SOURCES})
target_include_directories(twenty_four PRIVATE ${GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(twenty_four PRIVATE ${SDL3_LIB})

if(NOT MSVC)
    target_compile_options(twenty_four PRIVATE -Wall -Wextra)
    # stb single-header libs are third-party; don't hold them to our warnings.
    set_source_files_properties(src/stb_impl.cpp src/stb_image_impl.cpp
                                PROPERTIES COMPILE_OPTIONS "-w")
endif()

# macOS: produce a .app bundle with its icon and Info.plist metadata.
if(APPLE AND NOT EMSCRIPTEN)
    set(MACOSX_BUNDLE_ICON_FILE icon.icns)
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(twenty_four PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.icns)
    set_target_properties(twenty_four PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "Twenty Four"
        MACOSX_BUNDLE_BUNDLE_NAME "Twenty Four"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.nullprogram.twentyfour"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
        MACOSX_BUNDLE_COPYRIGHT "Public domain (Unlicense)")
endif()

# Windows: GUI subsystem (SDL provides WinMain through SDL_main.h).
if(WIN32)
    set_target_properties(twenty_four PROPERTIES WIN32_EXECUTABLE TRUE)
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
    foreach(asset manifest.webmanifest sw.js favicon.ico icon-192.png icon-512.png icon.svg)
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
        "${CMAKE_CURRENT_SOURCE_DIR}/web/favicon.ico"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/icon-192.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/icon-512.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/icon.svg"
        DESTINATION ".")
else()
    install(TARGETS twenty_four
        RUNTIME DESTINATION bin       # Windows/Linux executable
        BUNDLE DESTINATION .)         # macOS .app bundle
endif()
