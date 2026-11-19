# Completely separate, opt-in executable, no ImGui editor source files.
enable_language(OBJCXX)
add_executable(aiverse_studio MACOSX_BUNDLE mac/Main.mm StudioSession.cpp)
target_include_directories(aiverse_studio PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(aiverse_studio PRIVATE aiverse_editor_core "-framework Cocoa" "-framework WebKit")
target_compile_definitions(aiverse_studio PRIVATE AIVERSE_STUDIO_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
set_source_files_properties(mac/Main.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
set_target_properties(aiverse_studio PROPERTIES
    OUTPUT_NAME AiverseStudioHTML5
    MACOSX_BUNDLE_GUI_IDENTIFIER com.aiverse.studio.html5)
if(EXISTS "${PROJECT_SOURCE_DIR}/apps/studio-web/dist/index.html")
    add_custom_command(TARGET aiverse_studio POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${PROJECT_SOURCE_DIR}/apps/studio-web/dist"
            "$<TARGET_BUNDLE_CONTENT_DIR:aiverse_studio>/Resources/studio-web")
else()
    message(WARNING "HTML5 dist absent: run npm install && npm run build in apps/studio-web before configuring")
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_studio_session_tests tests/StudioSessionTests.cpp StudioSession.cpp)
    target_include_directories(aiverse_studio_session_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_studio_session_tests PRIVATE aiverse_editor_core)
    add_test(NAME studio_session COMMAND aiverse_studio_session_tests)
    set_tests_properties(studio_session PROPERTIES LABELS "EDITOR")
endif()
