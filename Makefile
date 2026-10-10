# math-glsl -- build the native GLSL video renderer (macOS / Linux)
#
#   make            build ./render_glsl
#   make demo       build, then render demo/wave.mp4 (5 s, 1080p60)
#   make clean
#
# On Windows use build.bat instead.
#
# Requires a C compiler, the OS graphics headers, and ffmpeg on PATH.

BIN    := render_glsl
SRC    := src/render_glsl.c
CC     ?= cc
CFLAGS  = -O2 -Wall -Wextra
UNAME  := $(shell uname -s)

ifeq ($(UNAME),Darwin)
  LDLIBS := -framework OpenGL
else
  CFLAGS  += -D_GNU_SOURCE
  LDLIBS  := -lEGL -lGL
endif

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

demo: $(BIN)
	./$(BIN) demo/wave.mp4 1920 1080 60 5

clean:
	rm -f $(BIN)

.PHONY: all demo clean
