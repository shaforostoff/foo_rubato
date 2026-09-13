# Checks pffft against kiss_fft, which is what makes the fast transform safe to
# swap in. Links both regardless of which one bpmcore was built with - it is
# comparing them, so it needs them both - and does not link bpmcore at all.
#
# kiss is built at whatever BPMCORE_FFT_SCALAR asks for, so this compares the
# two at the width that is actually shipping. Nothing to configure.

add_executable(fft_backend_test main.cpp)

target_link_libraries(fft_backend_test PRIVATE kiss_fft pffft)

if(MSVC)
    target_compile_options(fft_backend_test PRIVATE /W4)
endif()

add_test(NAME fft_backend COMMAND fft_backend_test)

set_target_properties(fft_backend_test PROPERTIES FOLDER "Tests")
