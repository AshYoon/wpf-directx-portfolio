add_library(SceneRenderer SHARED
    include/SceneRenderer.h
    src/SceneRenderer.cpp src/Renderer.h src/Renderer.cpp
    src/SpatialWorld.h src/SpatialWorld.cpp
    src/SVRThreadPool.h src/SVRThreadPool.cpp
    src/ObjectCommandQueue.h src/ObjectCommandQueue.cpp
)
target_compile_features(SceneRenderer PRIVATE cxx_std_17)
target_include_directories(SceneRenderer PUBLIC include)
target_compile_definitions(SceneRenderer PRIVATE
    SCENE_RENDERER_EXPORTS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(SceneRenderer PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(SceneRenderer PRIVATE d3d11 dxgi d3dcompiler user32)
set_target_properties(SceneRenderer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
