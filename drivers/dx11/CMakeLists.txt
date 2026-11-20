# dx11z.dll, a THRASH driver on Direct3D 11.
if ( MSVC )
    set( DX11Z_EXPORTS exports_msvc.def )
else()
    set( DX11Z_EXPORTS exports.def )
endif()

# Dear ImGui (external/imgui, MIT) for the settings menu in the game, its own library: built without /WX.
set( DX11Z_IMGUI ${CMAKE_SOURCE_DIR}/external/imgui )
add_library( dx11z_imgui STATIC
    ${DX11Z_IMGUI}/imgui.cpp
    ${DX11Z_IMGUI}/imgui_draw.cpp
    ${DX11Z_IMGUI}/imgui_tables.cpp
    ${DX11Z_IMGUI}/imgui_widgets.cpp
    ${DX11Z_IMGUI}/backends/imgui_impl_dx11.cpp
    ${DX11Z_IMGUI}/backends/imgui_impl_win32.cpp
)
target_include_directories( dx11z_imgui PUBLIC ${DX11Z_IMGUI} ${DX11Z_IMGUI}/backends )
target_compile_definitions( dx11z_imgui PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00 )
if ( MSVC )
    set_property( TARGET dx11z_imgui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" )
    target_compile_options( dx11z_imgui PRIVATE /utf-8 )
endif()

add_library( dx11z SHARED
    src/driver.cpp
    src/formats.cpp
    src/menu.cpp
    src/thrash_exports.cpp
    ${DX11Z_EXPORTS}
)

set_target_properties( dx11z PROPERTIES
    PREFIX ""
    OUTPUT_NAME "dx11z"
)

target_compile_definitions( dx11z PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00 )
target_link_libraries( dx11z PRIVATE dx11z_imgui d3d11 dxgi uuid d3dcompiler dwmapi )

if ( MSVC )
    set_property( TARGET dx11z PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" )
    target_compile_definitions( dx11z PRIVATE _CRT_SECURE_NO_WARNINGS )
    target_compile_options( dx11z PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8 /WX )
    target_link_options( dx11z PRIVATE /SAFESEH:NO )
else()
    target_compile_options( dx11z PRIVATE -mstackrealign -msse2 -mfpmath=sse )
    target_compile_options( dx11z PRIVATE -Wall -Wextra -Wno-cast-function-type -Wno-missing-field-initializers )
    target_link_options( dx11z PRIVATE -static -static-libgcc -static-libstdc++ -s )
endif()