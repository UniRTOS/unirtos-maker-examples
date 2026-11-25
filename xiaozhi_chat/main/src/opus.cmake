set(_opus ${CMAKE_CURRENT_SOURCE_DIR}/third_party/opus-1.5.2)
if(NOT EXISTS ${_opus}/COPYING)
    message(FATAL_ERROR "Missing official Opus1.5.2 source distribution")
endif()
# Use the release's audited source manifests, no demos/tests/arch/FPU/DNN.
include(${_opus}/cmake/OpusFunctions.cmake)
function(_xz_opus_manifest name manifest result)
    # Official parser understands CMake file(STRINGS) continuation grouping.
    # A regex over the raw makefile can wrongly swallow the FLOAT/ASM groups.
    get_opus_sources(${name} ${_opus}/${manifest} _sources)
    if(NOT _sources)
        message(FATAL_ERROR "Opus release manifest missing ${name}")
    endif()
    list(TRANSFORM _sources PREPEND "${_opus}/")
    set(${result} ${_sources} PARENT_SCOPE)
endfunction()
_xz_opus_manifest(OPUS_SOURCES opus_sources.mk _opus_src)
_xz_opus_manifest(CELT_SOURCES celt_sources.mk _celt_src)
_xz_opus_manifest(SILK_SOURCES silk_sources.mk _silk_src)
_xz_opus_manifest(SILK_SOURCES_FIXED silk_sources.mk _fixed_src)
add_library(xiaozhi_opus STATIC ${_opus_src} ${_celt_src} ${_silk_src} ${_fixed_src})
target_compile_definitions(xiaozhi_opus PRIVATE OPUS_BUILD FIXED_POINT
    DISABLE_FLOAT_API VAR_ARRAYS HAVE_LRINT HAVE_LRINTF ENABLE_HARDENING)
target_compile_options(xiaozhi_opus PRIVATE -O2 -ffunction-sections -fdata-sections)
target_include_directories(xiaozhi_opus BEFORE PUBLIC ${_opus}/include PRIVATE
    ${_opus} ${_opus}/celt ${_opus}/silk ${_opus}/silk/fixed)
target_link_libraries(${target} PUBLIC xiaozhi_opus)
# UniRTOS's final link collects archives explicitly, not only transitive deps.
add_apps_libraries($<TARGET_FILE:xiaozhi_opus>)