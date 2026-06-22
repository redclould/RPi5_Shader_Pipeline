CXXFLAGS := -Wall -Wextra -O2 -std=c++17 $(shell pkg-config --cflags libcamera)
LDLIBS := $(shell pkg-config --libs libcamera)
CFLAGS := -Wall -Wextra -O2

all: capture_raw raw_to_bmp

capture_raw: capture_raw.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)

raw_to_bmp: raw_to_bmp.c
	$(CC) $(CFLAGS) -o $@ $< -lm

clean:
	rm -f capture_raw raw_to_bmp *.raw *.bmp

.PHONY: all clean
