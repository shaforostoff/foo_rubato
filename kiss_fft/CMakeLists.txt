# KISS FFT, built at the width BPMCORE_FFT_SCALAR asks for.
#
# kiss_fft_scalar has to agree everywhere the headers are included, so it is
# PUBLIC: bpmcore, the only consumer that includes them, sees the same
# kiss_fft_cpx layout this library was compiled with.
#
# kiss is scalar C at either width and runs at the same speed either way. It is
# no longer the fast path - see pffft/ - but it is the portable one and the
# reference the fast path is checked against, which is why it is always built.

add_library(kiss_fft STATIC
    src/kiss_fft.c
    src/kiss_fftr.c)

# Consumers say <kiss_fft/kiss_fft.h>; the library's own sources include their
# siblings unqualified, so they need the inner directory as well.
target_include_directories(kiss_fft
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include/kiss_fft")
# The width comes from cmake/fft_backend.cmake, which the top level includes
# before any target. kiss is built at it whether or not it is the backend that
# ships: fft_backend_test compares the two at the shipping width, so kiss has
# to be compiled at it too.
target_compile_definitions(kiss_fft PUBLIC kiss_fft_scalar=${BPMCORE_FFT_SCALAR})

if(MSVC)
    # Third party C; do not let its warnings drown out ours. 4244 is the
    # narrowing that a float build is asking for on purpose: the twiddles are
    # computed from cos() in double and stored at the scalar width, which is
    # what keeps them accurate to that width rather than to itself.
    target_compile_options(kiss_fft PRIVATE /W3 /wd4996 /wd4267 /wd4244)
endif()

set_target_properties(kiss_fft PROPERTIES FOLDER "Third party")
