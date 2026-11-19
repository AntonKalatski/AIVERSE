# VIVID is a pure C++20 presentation module, independent of any genre and RHI.
# Later render/DNA adapters link to it, not the other way round.
add_library(aiverse_vivid STATIC Face.cpp)
add_library(AIVERSE::vivid ALIAS aiverse_vivid)
target_include_directories(aiverse_vivid PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(aiverse_vivid PUBLIC cxx_std_20)

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_vivid_face_tests ${PROJECT_SOURCE_DIR}/tests/vivid/face.cpp)
    target_link_libraries(aiverse_vivid_face_tests PRIVATE aiverse_vivid)
    add_test(NAME vivid_face COMMAND aiverse_vivid_face_tests)
endif()
