CC := gcc

CFLAGS := -O3 -march=native -mtune=native -flto -ffast-math \
          -fomit-frame-pointer -pipe -Wall -Wextra -Wpedantic

LDFLAGS := -flto
LDLIBS := $(shell pkg-config --libs freetype2)
CPPFLAGS := $(shell pkg-config --cflags freetype2)

TARGET := linewrap
SRC := src/linewrap.c

.PHONY: all clean rebuild

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(TARGET)

rebuild: clean all
