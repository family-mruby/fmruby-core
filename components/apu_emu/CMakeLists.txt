# APU emulator (nofrendo NES APU + NSF/FMSQ players) for local audio
# synthesis on the ESP32-P4 (Modern/Tab5) target. Ported from
# fmruby-graphics-audio; hardware output is delegated to the audio driver
# in main/ via apuif_set_output_writer().
# Other targets register an empty component: audio runs on the WROVER
# child chip (retro) or in the SDL2 host process (linux).
if(IDF_TARGET STREQUAL "esp32p4")
    idf_component_register(
        SRCS "src/apu_if_p4.c"
             "src/apu_helper.c"
             "src/nofrendo/nes_apu.c"
             "src/nes_cpu.c"
             "src/nsf_player.c"
             "src/fmsq_player.c"
        INCLUDE_DIRS "include"
        PRIV_INCLUDE_DIRS
            "src"
            "src/nofrendo"
        PRIV_REQUIRES heap esp_timer
    )

    target_compile_options(${COMPONENT_LIB} PRIVATE
        -w
        -Wno-error
        -Wno-implicit-fallthrough
        -Wno-attributes
        -Wno-missing-field-initializers
    )
else()
    idf_component_register()
endif()
