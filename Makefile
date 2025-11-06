# Makefile for Network Reflector

CC := gcc
CLANG := clang
CFLAGS := -Wall -Wextra -O3 -march=native -pthread
INCLUDES := -Iinclude
LDFLAGS := -pthread

# Platform detection
UNAME_S := $(shell uname -s)

# Common source files
COMMON_SRCS := src/dataplane/common/packet.c \
               src/dataplane/common/util.c \
               src/dataplane/common/core.c \
               src/dataplane/common/main.c

COMMON_OBJS := $(COMMON_SRCS:.c=.o)

# Platform-specific configuration
ifeq ($(UNAME_S),Linux)
    TARGET := reflector-linux
    # Check for AF_XDP support (libxdp headers - try both locations)
    HAS_XDP := $(shell echo '\#include <xdp/xsk.h>' | $(CC) -E - >/dev/null 2>&1 && echo 1 || echo 0)

    ifeq ($(HAS_XDP),1)
        PLATFORM_SRCS := src/dataplane/linux_xdp/xdp_platform.c \
                         src/dataplane/linux_packet/packet_platform.c
        LDFLAGS += -lxdp -lbpf -lelf -lz
        XDP_PROG := src/xdp/filter.bpf.o
        $(info Building with AF_XDP support)
    else
        PLATFORM_SRCS := src/dataplane/linux_packet/packet_platform.c
        XDP_PROG :=
        $(info AF_XDP headers not found - building AF_PACKET only)
        $(info Install libxdp-dev for AF_XDP support: sudo apt install libxdp-dev)
    endif

    PLATFORM_OBJS := $(PLATFORM_SRCS:.c=.o)
else ifeq ($(UNAME_S),Darwin)
    TARGET := reflector-macos
    PLATFORM_SRCS := src/dataplane/macos_bpf/bpf_platform.c
    PLATFORM_OBJS := $(PLATFORM_SRCS:.c=.o)
    XDP_PROG :=
else
    $(error Unsupported platform: $(UNAME_S))
endif

ALL_OBJS := $(COMMON_OBJS) $(PLATFORM_OBJS)

# Default target
all: $(TARGET) $(XDP_PROG)

# Link executable
$(TARGET): $(ALL_OBJS)
	@echo "Linking $@..."
	$(CC) $(ALL_OBJS) -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

# Compile C source files
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Compile eBPF program (Linux only)
src/xdp/filter.bpf.o: src/xdp/filter.bpf.c
	@echo "Compiling eBPF program..."
	$(CLANG) -O2 -g -target bpf \
		-I/usr/include/$(shell uname -m)-linux-gnu \
		-I/usr/src/linux-headers-$(shell uname -r)/include \
		-I/usr/src/linux-headers-$(shell uname -r)/arch/$(shell uname -m)/include \
		-c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning..."
	rm -f src/dataplane/common/*.o
	rm -f src/dataplane/linux_xdp/*.o
	rm -f src/dataplane/linux_packet/*.o
	rm -f src/dataplane/macos_bpf/*.o
	rm -f src/xdp/*.o
	rm -f $(TARGET) reflector-linux reflector-macos
	@echo "Clean complete"

# Install (requires root)
install: $(TARGET)
	@echo "Installing to /usr/local/bin..."
	install -m 755 $(TARGET) /usr/local/bin/reflector
ifeq ($(UNAME_S),Linux)
	@echo "Installing XDP program..."
	install -d /usr/local/lib/reflector
	install -m 644 $(XDP_PROG) /usr/local/lib/reflector/
endif
	@echo "Install complete"

# Uninstall
uninstall:
	@echo "Uninstalling..."
	rm -f /usr/local/bin/reflector
	rm -rf /usr/local/lib/reflector
	@echo "Uninstall complete"

# Help
help:
	@echo "Network Reflector Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build reflector for current platform"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install to /usr/local/bin (requires sudo)"
	@echo "  uninstall - Remove installed files"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Platform: $(UNAME_S)"
	@echo "Target:   $(TARGET)"

.PHONY: all clean install uninstall help
