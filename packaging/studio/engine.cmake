cmake_minimum_required(VERSION 3.28)
# Build the pinned upstream sources with the host OBS toolchain and resource layout.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" spec)
string(JSON engine_name GET "${spec}" name)
string(JSON engine_version GET "${spec}" version)
string(REPLACE "." ";" version_parts "${engine_version}")
list(GET version_parts 0 version_major)
list(GET version_parts 1 version_minor)
list(GET version_parts 2 version_patch)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version.h"
  "#pragma once\n#define PROJECT_VERSION \"${engine_version}-cherries\"\n#define PROJECT_VERSION_MAJOR ${version_major}\n#define PROJECT_VERSION_MINOR ${version_minor}\n#define PROJECT_VERSION_PATCH ${version_patch}\n")
file(GLOB engine_sources CONFIGURE_DEPENDS "*.cpp" "*.c" "*.hpp" "*.h")
add_library(${engine_name} MODULE ${engine_sources} resources.qrc)
target_include_directories(${engine_name} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_BINARY_DIR}/config")
target_link_libraries(${engine_name} PRIVATE OBS::libobs OBS::frontend-api Qt::Widgets CURL::libcurl)
target_compile_features(${engine_name} PRIVATE cxx_std_17)
# obsconfig.h supplies ENABLE_WAYLAND using the host build configuration.
# Keep upstream deprecation/unused-parameter warnings visible without applying
# OBS's first-party warnings-as-errors policy to the imported engines.
set_target_properties(${engine_name} PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
set_target_properties_obs(${engine_name} PROPERTIES PREFIX "" AUTOMOC ON AUTORCC ON AUTOUIC ON FOLDER plugins/cherries-studio)
target_enable(${engine_name})
