message(STATUS "cmake config ${CMAKE_CURRENT_SOURCE_DIR}")

# This CMakeLists is designed to be imported by SDK root CMake via add_subdirectory.
# It follows the UniRTOS external app contract (same pattern as unirtos project create).

# Application target name: from environment variable or fallback to app root folder name.
if(DEFINED ENV{UNIRTOS_APP_TARGET_NAME})
    set(target $ENV{UNIRTOS_APP_TARGET_NAME})
else()
    get_filename_component(target ${CMAKE_CURRENT_SOURCE_DIR} NAME)
endif()
message(STATUS "UniRTOS app target name: ${target}")
add_apps_libraries($<TARGET_FILE:${target}>)
add_library(${target} STATIC)
set_target_properties(${target} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${out_unir_lib_dir})

# SDK global include roots are exposed through SOURCE_TOP_DIR in SDK CMake.
target_include_directories(${target} PRIVATE
    ${SOURCE_TOP_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/main/src
)

# Xiaozhi desktop robot: voice + W180 LCD + touch + on-device MCP (motor/volume).
# Only the Xiaozhi provider is active; Coze/legacy demos are not compiled.
target_compile_definitions(${target} PRIVATE VOICE_CHAT_PROVIDER=2)

# SDK v1.0.5 compatibility flag (the verified target SDK).
get_filename_component(_xz_sdk_folder "${CMAKE_SOURCE_DIR}" NAME)
if(_xz_sdk_folder STREQUAL "v1.0.5")
    target_compile_definitions(${target} PRIVATE XIAOZHI_SDK_105=1)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/main/src/xiaozhi.cmake)

# Automatically link external libraries declared in env_config.json.
set(_app_external_library_targets)
if(DEFINED UNIRTOS_EXTERNAL_LIBRARY_TARGETS AND NOT "${UNIRTOS_EXTERNAL_LIBRARY_TARGETS}" STREQUAL "")
    list(APPEND _app_external_library_targets ${UNIRTOS_EXTERNAL_LIBRARY_TARGETS})
endif()
get_property(_global_external_library_targets GLOBAL PROPERTY unirtos_external_library_targets)
if(_global_external_library_targets)
    list(APPEND _app_external_library_targets ${_global_external_library_targets})
endif()
if(_app_external_library_targets)
    list(REMOVE_DUPLICATES _app_external_library_targets)
    message(STATUS "[app-target-link] ${target} links external library targets from env_config: ${_app_external_library_targets}")
    target_link_libraries(${target} PUBLIC ${_app_external_library_targets})
endif()
