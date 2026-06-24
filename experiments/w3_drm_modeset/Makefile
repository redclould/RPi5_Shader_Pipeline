CFLAGS := -Wall -Wextra -O2 $(shell pkg-config --cflags libdrm)
LDLIBS := $(shell pkg-config --libs libdrm)

modeset: modeset.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f modeset

.PHONY: clean
