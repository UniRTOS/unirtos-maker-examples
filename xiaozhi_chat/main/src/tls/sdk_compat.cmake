# SDK1.0.5 declares a DTLS-only local outside the same feature guard as its
# uses. Our TLS-only configuration exposes -Werror=unused-variable. Stage the
# minimal guarded declaration in the build tree; never edit the installed SDK
# or suppress compiler diagnostics for the application/library.
set(_xz_vtls_source ${CMAKE_SOURCE_DIR}/qos_components/components/vtls/mbedtls.c)
file(READ "${_xz_vtls_source}" _xz_vtls_text)
set(_xz_dtls_local "    qosa_uint32_t                 fin_ms = 0;")
string(FIND "${_xz_vtls_text}" "${_xz_dtls_local}" _xz_dtls_offset)
if(NOT _xz_dtls_offset EQUAL -1)
    string(REPLACE "${_xz_dtls_local}"
        "#if defined(MBEDTLS_TIMING_C) && defined(MBEDTLS_SSL_PROTO_DTLS)\n${_xz_dtls_local}\n#endif"
        _xz_vtls_text "${_xz_vtls_text}")
    set(_xz_vtls_staged ${_coze_tls_stage}/sdk_vtls_mbedtls.c)
    file(WRITE "${_xz_vtls_staged}" "${_xz_vtls_text}")
    get_target_property(_xz_vtls_sources vtls SOURCES)
    set(_xz_vtls_replaced FALSE)
    set(_xz_vtls_updated)
    foreach(_xz_source IN LISTS _xz_vtls_sources)
        if(_xz_source STREQUAL "mbedtls.c" OR _xz_source STREQUAL "${_xz_vtls_source}")
            list(APPEND _xz_vtls_updated "${_xz_vtls_staged}")
            set(_xz_vtls_replaced TRUE)
        else()
            list(APPEND _xz_vtls_updated "${_xz_source}")
        endif()
    endforeach()
    if(NOT _xz_vtls_replaced)
        message(FATAL_ERROR "Unable to identify SDK vtls source for guarded DTLS local")
    endif()
    set_property(TARGET vtls PROPERTY SOURCES "${_xz_vtls_updated}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_xz_vtls_source}")
    message(STATUS "Xiaozhi: SDK vtls DTLS-only local guarded in build-tree copy")
endif()