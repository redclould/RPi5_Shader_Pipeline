CFLAGS := -Wall -Wextra -O2 $(shell pkg-config --cflags libdrm)
LDLIBS := $(shell pkg-config --libs libdrm)

drm_info: drm_info.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f drm_info

.PHONY: clean