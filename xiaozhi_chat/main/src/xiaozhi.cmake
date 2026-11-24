set(_xz_dir ${CMAKE_CURRENT_LIST_DIR})
# Keep the established TLS symbol namespace, but no dependency on Coze sources.
set(_coze_dir ${_xz_dir}/tls)
include(${_xz_dir}/credentials_guard.cmake)
target_compile_definitions(${target} PRIVATE XIAOZHI_CHAT_ENABLE=1)
# New W180 bare-FPC panel: physical display confirmed after fixing CS/DC wiring.
# Voice + screen concurrency on SDK1.0.5 still requires a separate board test.
target_compile_definitions(${target} PRIVATE
    XIAOZHI_DISPLAY_ENABLE=1 XIAOZHI_ST77916_PROFILE=1 XIAOZHI_W180_PANEL=1
    XIAOZHI_EMOTE_ANIMATION=1 XIAOZHI_MOTION_ENABLE=1
    XIAOZHI_TOUCH_ENABLE=1 XIAOZHI_TOUCH_FACE_MODE=1 XIAOZHI_TOUCH_TALK=1)
# The Xiaozhi display task exclusively owns the shared LCD port. Never run
# the old standalone color/blink demo against the same SPI/GPIO resources.
target_compile_definitions(${target} PRIVATE ROBOT_LCD_DEMO_ENABLE=0)
# COZE_CHAT_ENABLE is deliberately NOT set: no Coze app/protocol/PAT compiled.
target_sources(${target} PRIVATE
    ${_xz_dir}/display.c ${_xz_dir}/activation_screen.c ${_xz_dir}/status_text.c ${_xz_dir}/clock.c ${_xz_dir}/face.c ${_xz_dir}/st77916.c
    ${_xz_dir}/touch.c ${_xz_dir}/touch_policy.c
    ${_xz_dir}/mpu6050.c ${_xz_dir}/mpu6050_port.c ${_xz_dir}/imu_screen.c ${_xz_dir}/shake.c
    ${_xz_dir}/app.c ${_xz_dir}/network.c ${_xz_dir}/protocol.c
    ${_xz_dir}/motion.c ${_xz_dir}/motion_port.c
    ${_xz_dir}/transport.c ${_xz_dir}/ws_parser.c ${_xz_dir}/codec.c
    ${_xz_dir}/activation.c ${_xz_dir}/activation_http.c
    ${_xz_dir}/audio/audio_backend.c ${_xz_dir}/audio/coze_policy.c
    ${_xz_dir}/audio/speaker_backend.c ${_xz_dir}/audio/codec_diag.c
    ${_xz_dir}/audio/es8311_playback.c ${_xz_dir}/audio/i2c_probe.c
    ${_xz_dir}/graphics/renderer.c ${_xz_dir}/graphics/board_config.c
    ${_xz_dir}/graphics/lcd_port.c)
target_sources(${target} PRIVATE ${_xz_dir}/emote_animation.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/uzlib/src/tinflate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/uzlib/src/tinfzlib.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/uzlib/src/adler32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/uzlib/src/crc32.c)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/third_party/uzlib/src/tinflate.c
    PROPERTIES COMPILE_DEFINITIONS UZLIB_CONF_PARANOID_CHECKS=1)
target_include_directories(${target} PRIVATE ${_xz_dir} ${_coze_dir} ${_xz_dir}/audio
    ${CMAKE_SOURCE_DIR}/qos_components/components/qcm_websocket
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/uzlib/src)
include(${_coze_dir}/tls_full.cmake)
include(${_coze_dir}/sdk_compat.cmake)
target_include_directories(${target} BEFORE PRIVATE
    ${_coze_tls2}/include ${_coze_tls_stage})

# Activation is screen/log only. Historical speech source/assets remain on
# disk for reproduction/tests but are neither generated nor linked here.

# SDK v1.0.15 PUSH and BUFFER both concatenate frame payloads and lose opcode.
# Rename ONLY the old implementation, not TCP/HTTP callers. Their frame parser
# is replaced with the bounded, message-preserving Xiaozhi hook. The installed
# SDK is not modified, and the Coze build still uses the original SDK parser.
set(_xz_sdk_ws ${CMAKE_SOURCE_DIR}/qos_components/components/qcm_websocket/qcm_web_ws.c)
if(NOT EXISTS ${_xz_sdk_ws})
    message(FATAL_ERROR "Missing selected SDK WS parser source")
endif()
set_property(SOURCE ${_xz_sdk_ws} TARGET_DIRECTORY qcm_websocket APPEND PROPERTY
    COMPILE_DEFINITIONS qcm_ws_parse_frame=qcm_ws_legacy_parse_frame)
# SDK logs HTTP headers and the unencrypted upgrade request at debug level.
# Turn off its compiled debug/verbose logs for this credential-bearing path.
target_compile_options(qcm_websocket PRIVATE "SHELL:-include ${_xz_dir}/sdk_log_guard.h")
include(${_xz_dir}/opus.cmake)
message(STATUS "Xiaozhi v1: strict WSS, manual PTT, binary Opus16k/mono/60ms, receive24k decoded at16k")