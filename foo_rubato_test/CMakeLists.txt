# Checks the strings the component writes into people's files.
#
# It links pfc and bpmcore but not the foobar2000 SDK: bpm_key_format.h was
# kept clear of the preferences page precisely so that the tag text could be
# checked without a host to initialise. Anything in there that reaches for a
# cfg_var breaks that, and belongs in bpm_result_format.h instead.

add_executable(foo_rubato_test main.cpp)

target_link_libraries(foo_rubato_test PRIVATE fb2k_pfc bpmcore)

if(MSVC)
    target_compile_options(foo_rubato_test PRIVATE /W4)
endif()

add_test(NAME tag_format COMMAND foo_rubato_test)

set_target_properties(foo_rubato_test PROPERTIES FOLDER "Tests")
