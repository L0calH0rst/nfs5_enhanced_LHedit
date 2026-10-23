# nfsp_texpool.asi: enlarges the in-race texture pool 'DTEX' (24 MB in the game) for HD textures.
add_library( nfsp_texpool SHARED nfsp_texpool.cpp )

set_target_properties( nfsp_texpool PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "nfsp_texpool"
)

target_compile_definitions( nfsp_texpool PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS )

if ( MSVC )
    set_property( TARGET nfsp_texpool PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" )
    target_compile_options( nfsp_texpool PRIVATE /W4 /permissive- /utf-8 /EHsc )
    target_link_options( nfsp_texpool PRIVATE /SAFESEH:NO )
else()
    target_link_options( nfsp_texpool PRIVATE -static -static-libgcc -static-libstdc++ -s )
endif()
