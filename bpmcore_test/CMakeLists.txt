# Verification and benchmark harness for the analysis.
#
# It links bpmcore alone, so the test runs without foobar2000 installed - which
# is only possible because the analysis carries no host dependencies.

add_executable(bpmcore_test main.cpp)

# kiss_fft is named here as well as reached through bpmcore, and that is not
# redundant: it is what puts kiss_fft_scalar in this target's preprocessor, so
# the batch mode can say which width it was built at without including any of
# kiss's headers.
target_link_libraries(bpmcore_test PRIVATE bpmcore kiss_fft)

if(MSVC)
    target_compile_options(bpmcore_test PRIVATE /W4)
    # std::getenv, which the batch mode uses to find ffmpeg, is standard C++;
    # MSVC deprecates it in favour of its own _dupenv_s.
    target_compile_definitions(bpmcore_test PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Checks the exported decision trees still reproduce the classifier they were
# generated from. Regenerated alongside the model by scripts/train_rhythm_model.py.
add_test(NAME rhythm_model
         COMMAND bpmcore_test model "${CMAKE_CURRENT_SOURCE_DIR}/reference_cases.tsv")

# Checks the resampler, and that a synthesised track gives the same tempo,
# rhythm and metre at every input rate. Synthesises its own audio, so it needs
# nothing on disk.
add_test(NAME resample COMMAND bpmcore_test resample)

# Checks the local-tempo spread against click tracks whose fluctuation is known
# in advance: steady reads near zero, a linear ramp reads the width the window
# geometry predicts, and a wobble reads clear of steady. Synthesises its own
# audio too.
add_test(NAME tempo_spread COMMAND bpmcore_test tempo_spread)

set_target_properties(bpmcore_test PROPERTIES FOLDER "Tests")
