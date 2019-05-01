cmake_minimum_required(VERSION 3.2.1)

project(arcdps_uploader)

add_library(d3d9_uploader SHARED
    arcdps_uploader/arcdps_uploader.cpp
    arcdps_uploader/arcdps_uploader.h
    arcdps_uploader/arcdps_defs.h
    arcdps_uploader/arc_logging.cpp
    arcdps_uploader/arc_logging.h
    arcdps_uploader/imgui.cpp
    arcdps_uploader/imgui.h
    arcdps_uploader/imgui_demo.cpp
    arcdps_uploader/imgui_draw.cpp
    arcdps_uploader/imgui_internal.h
    arcdps_uploader/imconfig.h
    arcdps_uploader/miniz.c
    arcdps_uploader/miniz.h
    arcdps_uploader/randombytes.c
    arcdps_uploader/tweetnacl.c
    arcdps_uploader/cpr/auth.cpp
    arcdps_uploader/cpr/cookies.cpp
    arcdps_uploader/cpr/cprtypes.cpp
    arcdps_uploader/cpr/digest.cpp
    arcdps_uploader/cpr/error.cpp
    arcdps_uploader/cpr/multipart.cpp
    arcdps_uploader/cpr/parameters.cpp
    arcdps_uploader/cpr/payload.cpp
    arcdps_uploader/cpr/proxies.cpp
    arcdps_uploader/cpr/session.cpp
    arcdps_uploader/cpr/ssl_options.cpp
    arcdps_uploader/cpr/timeout.cpp
    arcdps_uploader/cpr/util.cpp
    revtc/Revtc.cpp
)
target_include_directories(d3d9_uploader PUBLIC
    arcdps_uploader/includes
    revtc    
)
target_link_libraries(d3d9_uploader PUBLIC
    ${CMAKE_SOURCE_DIR}/lib/libcurl.lib
    ws2_32
    crypt32
)
target_compile_definitions(d3d9_uploader PRIVATE
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    CURL_STATICLIB
)