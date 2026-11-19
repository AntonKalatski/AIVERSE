# VIVID is a pure C++20 presentation module, independent of any genre and RHI.
# Later render/DNA adapters link to it, not the other way round.
add_library(aiverse_vivid STATIC Face.cpp SparseMorph.cpp FaceShapeBinding.cpp)
add_library(AIVERSE::vivid ALIAS aiverse_vivid)
target_include_directories(aiverse_vivid PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(aiverse_vivid PUBLIC cxx_std_20)

# Opt-in sparse morph GPU execution through the cross-platform gfx::Device RHI.
# No direct Metal or Vulkan implementation is linked from this library.
add_library(aiverse_vivid_gpu STATIC GpuMorph.cpp)
add_library(AIVERSE::vivid_gpu ALIAS aiverse_vivid_gpu)
target_link_libraries(aiverse_vivid_gpu PUBLIC aiverse_vivid aiverse_engine)
target_compile_features(aiverse_vivid_gpu PUBLIC cxx_std_20)

# Legacy render::Pose adapter; core remains usable without aiverse_models.
add_library(aiverse_vivid_render STATIC MorphBinding.cpp)
add_library(AIVERSE::vivid_render ALIAS aiverse_vivid_render)
target_link_libraries(aiverse_vivid_render PUBLIC aiverse_vivid aiverse_models)
target_compile_features(aiverse_vivid_render PUBLIC cxx_std_20)

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_vivid_face_tests ${PROJECT_SOURCE_DIR}/tests/vivid/face.cpp)
    target_link_libraries(aiverse_vivid_face_tests PRIVATE aiverse_vivid)
    add_test(NAME vivid_face COMMAND aiverse_vivid_face_tests)

    add_executable(aiverse_vivid_sparse_tests ${PROJECT_SOURCE_DIR}/tests/vivid/sparse_morph.cpp)
    target_link_libraries(aiverse_vivid_sparse_tests PRIVATE aiverse_vivid)
    add_test(NAME vivid_sparse_morph COMMAND aiverse_vivid_sparse_tests)

    add_executable(aiverse_vivid_shape_tests ${PROJECT_SOURCE_DIR}/tests/vivid/shape_binding.cpp)
    target_link_libraries(aiverse_vivid_shape_tests PRIVATE aiverse_vivid)
    add_test(NAME vivid_shape_binding COMMAND aiverse_vivid_shape_tests)

    add_executable(aiverse_vivid_gpu_contract_tests ${PROJECT_SOURCE_DIR}/tests/vivid/gpu_contract.cpp)
    target_link_libraries(aiverse_vivid_gpu_contract_tests PRIVATE aiverse_vivid_gpu)
    add_test(NAME vivid_gpu_contract COMMAND aiverse_vivid_gpu_contract_tests)

    add_executable(aiverse_vivid_morph_tests ${PROJECT_SOURCE_DIR}/tests/vivid/morph_binding.cpp)
    target_link_libraries(aiverse_vivid_morph_tests PRIVATE aiverse_vivid_render aiverse_zstd_dec)
    add_test(NAME vivid_morph_binding COMMAND aiverse_vivid_morph_tests)
endif()
