message(STATUS "cmake config ${CMAKE_CURRENT_SOURCE_DIR}")

# UniRTOS external app contract: imported by the SDK root CMake via add_subdirectory.
if(DEFINED ENV{UNIRTOS_APP_TARGET_NAME})
    set(target $ENV{UNIRTOS_APP_TARGET_NAME})
else()
    get_filename_component(target ${CMAKE_CURRENT_SOURCE_DIR} NAME)
endif()
message(STATUS "UniRTOS app target name: ${target}")

add_apps_libraries($<TARGET_FILE:${target}>)
add_library(${target} STATIC)
set_target_properties(${target} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${out_unir_lib_dir})

target_include_directories(${target} PRIVATE ${SOURCE_TOP_DIR})

# Only the UniRTOS application source; B570_streamline (STM32 reference code) is not built.
target_sources(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/balancing_car.c
)
