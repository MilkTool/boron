project: "boron-gl"

options [
    -debug: false   "Compile for debugging"
    audio:  true    "Enable OpenAL audio"
    gles:   true    "On desktop systems use the OpenGL Embedded Systems API"
]

default [
    either -debug [debug] [release]
    objdir %obj

    include_from [
        %.
        %../include
        %../urlan
        %../eval
    ]
    linux [include_from %/usr/include/GL]   ; For glv.h
    win32 [include_from %../../glv/win32]
    ;macx [universal]
]

shlib [%boron-gl 2,0,2] [
    cflags {-DIMAGE_BOTTOM_AT_0}
    if gles [cflags "-DUSE_GLES"]
    linux [
        cflags {-std=gnu99}
        cflags {-DUSE_XF86VMODE}
        include_from %../unix
        include_from %/usr/include/freetype2
       ;sources [%joystick.c]
    ]
    macx [
        cflags {-std=c99}
        include_from %../unix
        include_from %glv/mac
        sources [%glv/mac/glv.c]

        include_from %/usr/local/include/freetype2
    ]
    win32 [
        sources [
            %../../glv/win32/glv.c
            %../win32/win32console.c
        ]
        either msvc [
            lib-path: %"C:/cygwin/home/karl/osrc/"

            include_from %../win32

            include_from join lib-path %freetype2/include
            libs_from join lib-path %freetype2/objs [%freetype]

            include_from join lib-path %lpng128
            include_from join lib-path %zlib
            libs_from join lib-path %lpng128 [%libpng]
            libs_from join lib-path %zlib [%zlib]

            if audio [
              include_from %"C:/Program Files/OpenAL 1.1 SDK/include"
              libs_from %"C:/Program Files/OpenAL 1.1 SDK/libs/Win32" %OpenAL32
            ]
        ][
            cflags {-DGLEW_BUILD -DGLEW_NO_GLU}
            include_from [
                %/usr/x86_64-w64-mingw32/sys-root/mingw/include/freetype2
                %../win32
            ]
            libs_from %.. %boron
            libs "vorbis vorbisfile OpenAL32 glew32 opengl32 gdi32 freetype png z"
           ;libs %ws2_32   ; Needed if boron.lib is static.
        ]
    ]

    either audio [
        sources [%audio.c]
    ][
        cflags "-DNO_AUDIO"
        sources [%audio_stub.c]
    ]

    sources [
        %boron-gl.c
        %anim.c
        %draw_prog.c
        %es_compat.c
        %geo.c
        %glid.c
        %gui.c
       ;%particles.c
        %png_load.c
        %png_save.c
        %quat.c
        %raster.c
        %rfont.c
        %shader.c
        %TexFont.c
        %port_joystick.c
        %widget_button.c
        %widget_choice.c
        %widget_label.c
        %widget_lineedit.c
        %widget_list.c
        %widget_menu.c
        %widget_slider.c
        %widget_itemview.c
    ]
]

exe %boron-gl [
    include_from %../support
    cflags {-DBORON_GL}
    libs_from %. %boron-gl
    libs_from %.. %boron
    linux [
        ;libs_from %/usr/X11R6/lib [%X11 %Xxf86vm]
        libs [%X11 %Xxf86vm]
        libs [%freetype %png %glv %GL %m]
        if audio [
            libs [%openal %vorbis %vorbisfile %pthread]
        ]
    ]
    macx [
        libs [%freetype %png]
        lflags {-framework OpenGL}
        lflags {-framework AGL}
        lflags {-framework Carbon}
        if audio [
            libs [%vorbis %vorbisfile]
            lflags {-framework OpenAL}
        ]
    ]
    win32 [
        sources [%../../glv/win32/glv_main.c]
        libs %ws2_32
    ]
    sources [
        %../eval/main.c
    ]
]
