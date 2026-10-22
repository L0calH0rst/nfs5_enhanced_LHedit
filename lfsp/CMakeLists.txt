# lfsp.asi, the LFSP plugin (windows and roof, lights, traffic, racer list). dx11z finds it by module name.
add_library( lfsp SHARED lfsp.cpp )

set_target_properties( lfsp PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "lfsp"
)

target_compile_definitions( lfsp PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS )
target_link_libraries( lfsp PRIVATE user32 winmm )

if ( MSVC )
    set_property( TARGET lfsp PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" )
    target_compile_options( lfsp PRIVATE /W4 /permissive- /utf-8 /EHsc )
    target_link_options( lfsp PRIVATE /SAFESEH:NO )
else()
    target_compile_options( lfsp PRIVATE -mstackrealign -msse2 -mfpmath=sse )
    target_link_options( lfsp PRIVATE -static -static-libgcc -static-libstdc++ -s -Wl,--enable-stdcall-fixup )
endif()
