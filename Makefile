CC      = gcc
TARGET  = rtspeed
SRC     = main.c
PKGS    = libadwaita-1

CFLAGS  = $(shell pkg-config --cflags $(PKGS)) -Wall -Wextra -O2 -Wno-unused-parameter
LDFLAGS = $(shell pkg-config --libs   $(PKGS))

$(TARGET): $(SRC)
	$(CC) $(SRC) $(CFLAGS) $(LDFLAGS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: clean
