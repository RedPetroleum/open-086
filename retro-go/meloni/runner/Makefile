# Desktop runner for Meloni games: the engine from ../components, built for macOS/Linux.
#   make            -> build/meloni-run (with a window when SDL2 is installed, else headless only)
#   make SDL=0      -> headless only
MELONI := ../components/meloni
LUA    := ../components/lua/src
BUILD  := build

SDL ?= $(shell command -v sdl2-config >/dev/null 2>&1 && echo 1 || echo 0)

CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wno-unused-function -I$(MELONI) -I$(LUA) -Ilodepng
LDLIBS  += -lm

ifeq ($(SDL),1)
CFLAGS  += -DMEL_SDL $(shell sdl2-config --cflags)
LDLIBS  += $(shell sdl2-config --libs)
endif

SRCS := runner.c lodepng/lodepng.c $(wildcard $(MELONI)/*.c) $(wildcard $(LUA)/*.c)
OBJS := $(patsubst %.c,$(BUILD)/%.o,$(subst ../,,$(SRCS)))

$(BUILD)/meloni-run: $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: %.c $(MELONI)/meloni.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/components/%.o: ../components/%.c $(MELONI)/meloni.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD)

.PHONY: clean
