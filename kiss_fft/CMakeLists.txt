# KISS FFT, built at the width BPMCORE_FFT_SCALAR asks for.
#
# kiss_fft_scalar has to agree everywhere the headers are included, so it is
# PUBLIC: bpmcore, the only consumer that includes them, sees the same
# kiss_fft_cpx layout this library was compiled with.
#
# The width is an option rather than a constant because it is the one thing
# about the spectral stage that can move a BPM or a class, and deciding it
# needs a measurement rather than an opinion - see docs/tango-analysis.md.
# kiss is scalar C at either width and runs at the same speed either way, so
# this buys nothing on its own; it exists so the two can be compared over the
# collection before an embedded build commits to float.

add_library(kiss_fft STATIC
    src/kiss_fft.c
    src/kiss_fftr.c)

# Consumers say <kiss_fft/kiss_fft.h>; the library's own sources include their
# siblings unqualified, so they need the inner directory as well.
target_include_directories(kiss_fft
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include/kiss_fft")
if(NOT DEFINED BPMCORE_FFT_SCALAR)
    set(BPMCORE_FFT_SCALAR "double" CACHE STRING
        "Width of the spectral stage: double (as shipped) or float")
    set_property(CACHE BPMCORE_FFT_SCALAR PROPERTY STRINGS double float)
endif()
if(NOT BPMCORE_FFT_SCALAR MATCHES "^(double|float)$")
    message(FATAL_ERROR "BPMCORE_FFT_SCALAR must be double or float, not ${BPMCORE_FFT_SCALAR}")
endif()
message(STATUS "Spectral stage: ${BPMCORE_FFT_SCALAR} precision")

target_compile_definitions(kiss_fft PUBLIC kiss_fft_scalar=${BPMCORE_FFT_SCALAR})

if(MSVC)
    # Third party C; do not let its warnings drown out ours. 4244 is the
    # narrowing that a float build is asking for on purpose: the twiddles are
    # computed from cos() in double and stored at the scalar width, which is
    # what keeps them accurate to that width rather than to itself.
    target_compile_options(kiss_fft PRIVATE /W3 /wd4996 /wd4267 /wd4244)
endif()

set_target_properties(kiss_fft PROPERTIES FOLDER "Third party")
