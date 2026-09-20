# Which transform the spectral stage uses, and at what width.
#
# Two options rather than one, because they are two questions. The width is a
# property of the analyser and is the only one that can move a BPM or a class;
# the backend is an implementation detail that, at a fixed width, should agree
# with the other to rounding. Conflating them means that if an embedded build
# ever disagrees with the desktop one, there is no way to tell which of the two
# did it.
#
# What the width costs was measured over both collections: nothing. See
# docs/tango-analysis.md, "What the spectral stage costs at float".
#
# PFFFT at float is the shipping default, and has been since that measurement
# was repeated over the whole collection: 12,157 tracks analysed by both, not
# one of them changing its rhythm, its meter or its metrical level, against a
# transform six times faster at these sizes. KISS at double is still here and
# still the reference the other is checked against, one `-D` away.
#
# Included before any of the targets, so kiss_fft, pffft and bpmcore all see
# the same answer. A standalone build of bpmcore includes it itself.

if(NOT DEFINED BPMCORE_FFT_BACKEND)
    set(BPMCORE_FFT_BACKEND "pffft" CACHE STRING
        "Transform to use: pffft (SIMD, float only) or kiss (portable, scalar)")
    set_property(CACHE BPMCORE_FFT_BACKEND PROPERTY STRINGS pffft kiss)
endif()

# Settled after the backend, because pffft has only the one width to offer and
# asking for it should not also mean remembering to ask for float. Naming a
# width explicitly still overrides this, and naming kiss alone gives the double
# reference build rather than a half-switched one.
if(NOT DEFINED BPMCORE_FFT_SCALAR)
    if(BPMCORE_FFT_BACKEND STREQUAL "pffft")
        set(BPMCORE_FFT_SCALAR "float" CACHE STRING
            "Width of the spectral stage: double or float")
    else()
        set(BPMCORE_FFT_SCALAR "double" CACHE STRING
            "Width of the spectral stage: double or float")
    endif()
    set_property(CACHE BPMCORE_FFT_SCALAR PROPERTY STRINGS double float)
endif()

if(NOT BPMCORE_FFT_SCALAR MATCHES "^(double|float)$")
    message(FATAL_ERROR "BPMCORE_FFT_SCALAR must be double or float, not ${BPMCORE_FFT_SCALAR}")
endif()
if(NOT BPMCORE_FFT_BACKEND MATCHES "^(kiss|pffft)$")
    message(FATAL_ERROR "BPMCORE_FFT_BACKEND must be kiss or pffft, not ${BPMCORE_FFT_BACKEND}")
endif()

# pffft is single precision and has no double variant; the fork that adds one
# needs AVX to beat scalar, which is not a trade this ships on x86 or ARM.
if(BPMCORE_FFT_BACKEND STREQUAL "pffft" AND NOT BPMCORE_FFT_SCALAR STREQUAL "float")
    message(FATAL_ERROR
        "BPMCORE_FFT_BACKEND=pffft requires BPMCORE_FFT_SCALAR=float; pffft is "
        "single precision only. Configure with -DBPMCORE_FFT_SCALAR=float.")
endif()

# Included by the top level and again by bpmcore, so that bpmcore stands alone
# in an Android build; say it once either way.
if(NOT BPMCORE_FFT_ANNOUNCED)
    message(STATUS "Spectral stage: ${BPMCORE_FFT_BACKEND}, ${BPMCORE_FFT_SCALAR} precision")
    set(BPMCORE_FFT_ANNOUNCED TRUE CACHE INTERNAL "")
endif()
