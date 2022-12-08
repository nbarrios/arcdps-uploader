cmake_minimum_required(VERSION 3.15.0)

cmake_policy(SET CMP0091 NEW)

project(arcdps_uploader LANGUAGES CXX C)

find_package(CURL CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GW2_DIR "" CACHE PATH "GW2 Directory")

set(SOURCE
    arcdps_uploader/arcdps_uploader.cpp
    arcdps_uploader/arc_logging.cpp
    arcdps_uploader/Aleeva.cpp
    arcdps_uploader/Uploader.cpp
    arcdps_uploader/Settings.cpp
    arcdps_uploader/Log.cpp
    arcdps_uploader/imgui/imgui.cpp
    arcdps_uploader/imgui/imgui_demo.cpp
    arcdps_uploader/imgui/imgui_draw.cpp
    arcdps_uploader/imgui/imgui_tables.cpp
    arcdps_uploader/imgui/imgui_widgets.cpp
    arcdps_uploader/imgui/imgui_stdlib.cpp
    arcdps_uploader/loguru.cpp
    arcdps_uploader/sqlite3.c
    revtc/Revtc.cpp
)

set(HEADERS
    arcdps_uploader/arcdps_uploader.h
    arcdps_uploader/arcdps_defs.h
    arcdps_uploader/arc_logging.h
    arcdps_uploader/Uploader.h
    arcdps_uploader/Settings.h
    arcdps_uploader/Log.h
    arcdps_uploader/imgui/imgui.h
    arcdps_uploader/imgui/imgui_internal.h
    arcdps_uploader/imgui/imconfig.h
    arcdps_uploader/imgui/imstb_rectpack.h
    arcdps_uploader/imgui/imstb_textedit.h
    arcdps_uploader/imgui/imstb_truetype.h
    arcdps_uploader/imgui/imgui_stdlib.h
)

add_library(d3d9_uploader SHARED
    ${SOURCE}
    ${HEADERS}
)

set_property(TARGET d3d9_uploader PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include_directories(
    arcdps_uploader/includes
    revtc    
)
target_compile_definitions(d3d9_uploader PRIVATE
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    CURL_STATICLIB
)
target_compile_options(d3d9_uploader PUBLIC "/Zc:__cplusplus")

add_executable(uploader_standalone
    arcdps_uploader/main.cpp
    arcdps_uploader/imgui/imgui_impl_dx9.cpp
    arcdps_uploader/imgui/imgui_impl_dx9.h
    arcdps_uploader/imgui/imgui_impl_win32.cpp
    arcdps_uploader/imgui/imgui_impl_win32.h
    ${SOURCE}
    ${HEADERS}
)

set_property(TARGET uploader_standalone PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_compile_definitions(uploader_standalone PRIVATE
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    CURL_STATICLIB
    STANDALONE
)
target_compile_options(uploader_standalone PUBLIC "/Zc:__cplusplus")
set_property(TARGET uploader_standalone PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${GW2_DIR})

target_link_libraries(d3d9_uploader PUBLIC
    CURL::libcurl
    cpr::cpr
)

target_link_libraries(uploader_standalone PUBLIC
    CURL::libcurl
    cpr::cpr
    D3d9
)

add_custom_command(TARGET d3d9_uploader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:d3d9_uploader> ${GW2_DIR}/bin64/$<TARGET_FILE_NAME:d3d9_uploader>
)