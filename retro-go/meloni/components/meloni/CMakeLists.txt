# Meloni engine, platform independent (the device side is in ../../main, the desktop runner in ../../runner).
# lodepng comes from the retro-go component.
idf_component_register(SRCS "engine.c" "gfx.c" "audio.c" "prelude.c" "font8x8.c"
                       INCLUDE_DIRS "."
                       REQUIRES lua retro-go)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
