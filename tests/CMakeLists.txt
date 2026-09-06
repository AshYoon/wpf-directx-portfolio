add_executable(SpatialWorldTests SpatialWorldTests.cpp
    ../native/SceneRenderer/src/SpatialWorld.cpp
    ../native/SceneRenderer/src/SVRThreadPool.cpp)
target_include_directories(SpatialWorldTests PRIVATE ../native/SceneRenderer/src)
target_compile_features(SpatialWorldTests PRIVATE cxx_std_17)
target_compile_options(SpatialWorldTests PRIVATE /W4 /permissive- /utf-8)
add_executable(RendererApiTests RendererApiTests.cpp)
target_compile_features(RendererApiTests PRIVATE cxx_std_17)
target_compile_definitions(RendererApiTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(RendererApiTests PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(RendererApiTests PRIVATE SceneRenderer user32)
set_target_properties(SpatialWorldTests RendererApiTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_test(NAME SpatialWorldCorrectness COMMAND SpatialWorldTests)
add_test(NAME RendererAbiLifecycle COMMAND RendererApiTests)
if(TARGET NativeHost)
    add_test(NAME NativeLifecycle COMMAND NativeHost --smoke)
endif()
add_executable(ThreadingTests ThreadingTests.cpp
    ../native/SceneRenderer/src/SpatialWorld.cpp
    ../native/SceneRenderer/src/SVRThreadPool.cpp
    ../native/SceneRenderer/src/ObjectCommandQueue.cpp)
target_include_directories(ThreadingTests PRIVATE ../native/SceneRenderer/src ../native/SceneRenderer/include)
target_compile_features(ThreadingTests PRIVATE cxx_std_17)
target_compile_options(ThreadingTests PRIVATE /W4 /permissive- /utf-8)
set_target_properties(ThreadingTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_test(NAME NativeThreading COMMAND ThreadingTests)
# 대기·종료 회귀가 생겨도 CI가 무한 대기하지 않게 제한한다.
set_tests_properties(NativeThreading RendererAbiLifecycle PROPERTIES TIMEOUT 30)
