message(STATUS "cmake config ${CMAKE_CURRENT_SOURCE_DIR}")

if(DEFINED ENV{UNIRTOS_APP_TARGET_NAME})
    set(target $ENV{UNIRTOS_APP_TARGET_NAME})
else()
    get_filename_component(target ${CMAKE_CURRENT_SOURCE_DIR} NAME)
endif()

add_apps_libraries($<TARGET_FILE:${target}>)
add_library(${target} STATIC)
set_target_properties(${target} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${out_unir_lib_dir})

target_include_directories(${target} PRIVATE
    ${SOURCE_TOP_DIR}
    ${SOURCE_SYS_DEBUG_DIR}
    ${SOURCE_SYS_OS_DIR}
    ${SOURCE_COMPONENTS_PUBLIC_INC}
    ${CMAKE_CURRENT_SOURCE_DIR}/main/inc
)
target_sources(${target} PRIVATE
    main/src/app/lvgl_demo.c
    main/src/pages/lvgl_home.c
    main/src/resources/images/lvgl_home_images.c
    main/src/pages/lvgl_industry.c
    main/src/resources/images/lvgl_industry_images.c
    main/src/pages/lvgl_smart_home.c
    main/src/resources/images/lvgl_smart_home_images.c
    main/src/pages/lvgl_agri_dashboard.c
    main/src/resources/fonts/lvgl_agri_font.c
    main/src/resources/images/lvgl_agri_icons.c
    main/src/drivers/display/lvgl_port_display.c
    main/src/drivers/touch/lvgl_touch_ft6336.c
    main/src/drivers/touch/lvgl_touch_calibration.c
    main/src/drivers/display/lcd_drv_st7796s.c
)
target_link_libraries(${target} PRIVATE unirtos-lvgl)
target_link_libraries_if(CONFIG_QOSA_LINK_SYSTEM_LIBRARIES_FUNC THEN ${target} system_os)
