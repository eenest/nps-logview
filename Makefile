# NPS Log Viewer Makefile
# Supports three app versions: Linux GTK GUI, Windows 64-bit GUI,
# and Windows 32-bit GUI. The Linux CLI is an optional terminal helper.

CC_LINUX  = gcc
CC_WIN    = x86_64-w64-mingw32-gcc
CC_WIN32  = i686-w64-mingw32-gcc

CFLAGS    = -Wall -Wextra -O2
CFLAGS_WIN = -Wall -Wextra -O2 -mwindows
LDFLAGS_WIN = -lcomctl32 -lcomdlg32 -lshell32
RC_WIN = x86_64-w64-mingw32-windres
RC_WIN32 = i686-w64-mingw32-windres

SRC_CORE  = src/radius_dict.c src/parser.c src/config.c
SRC_GUI   = src/nps_gui.c
SRC_GUI_LINUX = src/nps_gui_linux.c
SRC_CLI   = src/cli_test.c

TARGET_WIN = nps-logview.exe
TARGET_WIN32 = nps-logview-x86.exe
TARGET_CLI = nps-logview-cli
TARGET_GUI_LINUX = nps-logview

.PHONY: all linux windows win32 clean test linux-gui cli

all: linux windows win32

linux: $(TARGET_GUI_LINUX)

linux-gui: $(TARGET_GUI_LINUX)

cli: $(TARGET_CLI)

windows: $(TARGET_WIN)

win32: $(TARGET_WIN32)

$(TARGET_CLI): $(SRC_CORE) $(SRC_CLI)
	$(CC_LINUX) $(CFLAGS) -o $@ $^

$(TARGET_GUI_LINUX): $(SRC_CORE) $(SRC_GUI_LINUX)
	$(CC_LINUX) $(CFLAGS) `pkg-config --cflags gtk+-3.0` -o $@ $^ `pkg-config --libs gtk+-3.0`

src/nps-logview.res: src/nps-logview.rc src/nps-logview.manifest icons/nps-logview.ico
	$(RC_WIN) -O coff -i $< -o $@

src/nps-logview-x86.res: src/nps-logview.rc src/nps-logview.manifest icons/nps-logview.ico
	$(RC_WIN32) -O coff -i $< -o $@

$(TARGET_WIN): $(SRC_CORE) $(SRC_GUI) src/nps-logview.res
	$(CC_WIN) $(CFLAGS_WIN) -o $@ $(SRC_CORE) $(SRC_GUI) src/nps-logview.res $(LDFLAGS_WIN)

$(TARGET_WIN32): $(SRC_CORE) $(SRC_GUI) src/nps-logview-x86.res
	$(CC_WIN32) $(CFLAGS_WIN) -o $@ $(SRC_CORE) $(SRC_GUI) src/nps-logview-x86.res $(LDFLAGS_WIN)

test: $(TARGET_CLI)
	@if [ -f logs/sample-nps.log ]; then \
		./$(TARGET_CLI) logs/sample-nps.log; \
	else \
		echo "No logs/sample-nps.log found. Create a sample log file to test."; \
	fi

clean:
	rm -f $(TARGET_CLI) $(TARGET_WIN) $(TARGET_WIN32) $(TARGET_GUI_LINUX)
	rm -f src/nps-logview.res src/nps-logview-x86.res

help:
	@echo "NPS Log Viewer Build"
	@echo "===================="
	@echo "make all        - Build all three supported app versions"
	@echo "make linux      - Build Linux GTK3 GUI app version"
	@echo "make linux-gui  - Build Linux GTK3 GUI app version (alias)"
	@echo "make windows    - Build Windows 64-bit GUI app version (requires mingw-w64)"
	@echo "make win32      - Build Windows 32-bit GUI app version (requires mingw-w64)"
	@echo "make cli        - Build optional Linux CLI terminal helper"
	@echo "make test       - Run CLI terminal helper against sample log"
	@echo "make clean      - Remove build artifacts"
