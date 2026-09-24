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

# Allow nested user components in common conventions.
# add_subdirectory_if_exist(app_components)

# SDK global include roots are exposed through SOURCE_TOP_DIR in SDK CMake.
target_include_directories(${target} PRIVATE 
    ${SOURCE_TOP_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/main/src
)

# Add your include directories here.
target_include_directories(${target} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/main/inc
)

# Add your source files here.
file(GLOB_RECURSE APP_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/main/src/*.c
)

target_sources(${target} PRIVATE ${APP_SRC})

# Automatically link external libraries declared in env_config.json.
# This lets app sources include library headers and use library APIs directly.
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

# UNcomment to add the libraries you need here.
# target_link_libraries(${target} libxxx)
