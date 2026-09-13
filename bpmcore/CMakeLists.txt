# The analysis, with no host in it.
#
# Nothing here includes foobar2000, pfc, ATL or windows.h - only the standard
# library and one FFT - so the same sources build for a command line tool, a
# macOS host or an Android target. The foobar2000 component is a shell around
# this, and `real_fft.h` is the only place a transform is named.

add_library(bpmcore STATIC
    analyse.cpp
    odf.cpp
    real_fft.cpp
    resample.cpp
    rhythm.cpp
    tempo.cpp

    bpmcore.h
    internal.h
    parallel.h
    real_fft.h
    rhythm_model.h)

# Consumers say #include <bpmcore/bpmcore.h>.
target_include_directories(bpmcore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# Which transform, and at what width. Both are PUBLIC where they have to be
# seen by a consumer's headers: fft_scalar is part of real_fft.h's meaning, and
# a test that links bpmcore and reads its bins has to agree about the layout.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/fft_backend.cmake")

if(BPMCORE_FFT_BACKEND STREQUAL "pffft")
    target_link_libraries(bpmcore PRIVATE pffft)
    target_compile_definitions(bpmcore PUBLIC BPMCORE_FFT_PFFFT)
else()
    target_link_libraries(bpmcore PRIVATE kiss_fft)
endif()
target_compile_definitions(bpmcore PUBLIC BPMCORE_FFT_SCALAR_TYPE=${BPMCORE_FFT_SCALAR})

target_compile_features(bpmcore PUBLIC cxx_std_11)

if(MSVC)
    target_compile_options(bpmcore PRIVATE /W4)
    # windows.h is not included here, but a consumer's precompiled header may
    # have dragged in its min/max macros before this target's headers.
    target_compile_definitions(bpmcore PUBLIC NOMINMAX)
endif()

set_target_properties(bpmcore PROPERTIES FOLDER "bpmcore")
