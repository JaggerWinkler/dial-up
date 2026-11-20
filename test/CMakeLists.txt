# Unit tests (label "unit") run anywhere. Tests labelled "audio" play sound and
# record from the microphone, so they need speakers, a mic, and mic permission.
#
#   ctest --test-dir build -L unit  --output-on-failure
#   ctest --test-dir build -L audio --output-on-failure

function(dial_up_test name label)
    add_executable(${name} ${name}.cpp check.h)
    target_link_libraries(${name} PRIVATE dial_up_core)
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(${name} PROPERTIES LABELS ${label})
endfunction()

dial_up_test(test_convolutional unit)
dial_up_test(test_reed_solomon  unit)
dial_up_test(test_fsk           unit)
dial_up_test(test_speaker       unit)
dial_up_test(test_pipeline      unit)

dial_up_test(test_speaker_audio audio)
dial_up_test(test_end_to_end    audio)
set_tests_properties(test_speaker_audio test_end_to_end PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
