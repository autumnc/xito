# editor - Minimalist X11 text editor with XIM support
#
# Dependencies: X11, Xft, Pango, Cairo
#   Debian/Ubuntu: sudo apt-get install libx11-dev libxft-dev libpango1.0-dev libcairo2-dev
#   Fedora/RHEL:   sudo dnf install libX11-devel libXft-devel pango-devel cairo-devel
#   Arch Linux:    sudo pacman -S libx11 libxft pango cairo

CC      ?= gcc
CFLAGS  := -Wall -Wextra -O2 -std=c11
LDFLAGS :=

# pkg-config for dependencies
PKG_CFLAGS := $(shell pkg-config --cflags x11 xft pangocairo cairo-xlib 2>/dev/null)
PKG_LDFLAGS := $(shell pkg-config --libs x11 xft pangocairo cairo-xlib 2>/dev/null)

CFLAGS  += $(PKG_CFLAGS)
LDFLAGS += $(PKG_LDFLAGS)

TARGET  = xito
SRC     = xito.c

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

# Optional: install to /usr/local/bin
install: $(TARGET)
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/

uninstall:
	rm -f $(DESTDIR)/usr/local/bin/$(TARGET)
