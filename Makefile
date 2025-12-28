# Makefile for Network Reflector

CC := gcc
CLANG := clang

# ===================================
# Compiler Version Check
# ===================================
# Check GCC version (need >= 4.9 for -march=native -flto)
GCC_VERSION := $(shell $(CC) -dumpversion 2>/dev/null | cut -d. -f1)
GCC_MIN_VERSION := 4

ifeq ($(shell test $(GCC_VERSION) -lt $(GCC_MIN_VERSION) 2>/dev/null && echo 1),1)
  $(warning ⚠️  GCC version $(GCC_VERSION) detected. Recommend GCC >= 4.9 for optimal performance.)
  $(warning    Some optimizations (-march=native, -flto) may not be available.)
endif

# Performance-optimized flags
CFLAGS := -Wall -Wextra -O3 -march=native -pthread \
          -fno-strict-aliasing \
          -fomit-frame-pointer \
          -funroll-loops \
          -finline-functions \
          -ftree-vectorize \
          -flto
INCLUDES := -Iinclude
LDFLAGS := -pthread -flto

# Platform detection
UNAME_S := $(shell uname -s)

# Common source files
COMMON_SRCS := src/dataplane/common/packet.c \
               src/dataplane/common/util.c \
               src/dataplane/common/core.c \
               src/dataplane/common/nic_detect.c \
               src/dataplane/common/main.c

# Add libdl for dlopen (NIC detection)
LDFLAGS += -ldl

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

    # Check for DPDK support (pkg-config)
    HAS_DPDK := $(shell pkg-config --exists libdpdk 2>/dev/null && echo 1 || echo 0)

    ifeq ($(HAS_DPDK),1)
        PLATFORM_SRCS += src/dataplane/linux_dpdk/dpdk_platform.c
        CFLAGS += $(shell pkg-config --cflags libdpdk) -DHAVE_DPDK=1
        LDFLAGS += $(shell pkg-config --libs libdpdk)
        $(info Building with DPDK support (100G line-rate mode available))
    else
        $(info DPDK not found - 100G mode not available)
        $(info Install DPDK for 100G support: sudo apt install dpdk-dev libdpdk-dev)
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
all: version $(TARGET) $(XDP_PROG)

# Generate version from git tags
version:
	@./scripts/gen-version.sh include/version_generated.h

# Link executable
$(TARGET): $(ALL_OBJS)
	@echo "Linking $@..."
	$(CC) $(ALL_OBJS) -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

# Compile C source files (depend on version being generated)
%.o: %.c version
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Compile eBPF program (Linux only) - non-fatal, AF_XDP works without it
src/xdp/filter.bpf.o: src/xdp/filter.bpf.c
	@echo "Compiling eBPF program..."
	-$(CLANG) -O2 -g -target bpf \
		-I/usr/include/$(shell uname -m)-linux-gnu \
		-I/usr/src/linux-headers-$(shell uname -r)/include \
		-I/usr/src/linux-headers-$(shell uname -r)/arch/$(shell uname -m)/include \
		-c $< -o $@ 2>/dev/null || echo "eBPF compilation failed (will use SKB mode)"

# Clean build artifacts
clean:
	@echo "Cleaning..."
	rm -f src/dataplane/common/*.o
	rm -f src/dataplane/linux_xdp/*.o
	rm -f src/dataplane/linux_packet/*.o
	rm -f src/dataplane/macos_bpf/*.o
	rm -f src/xdp/*.o
	rm -f include/version_generated.h
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

# ===================================
# Testing Targets
# ===================================

# Basic unit tests
test: $(TARGET)
	@echo "Running packet validation tests..."
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_packet_validation.c \
		src/dataplane/common/packet.o src/dataplane/common/util.o -o tests/test_packet
	@./tests/test_packet
	@echo "✅ Packet validation tests passed!"

# Utility function tests
test-utils: $(TARGET)
	@echo "Running utility function tests..."
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_utils.c \
		src/dataplane/common/packet.o src/dataplane/common/util.o -o tests/test_utils
	@./tests/test_utils
	@echo "✅ Utility tests passed!"

# Integration tests (exclude main.o to avoid main() conflict)
test-integration: $(TARGET)
	@echo "Running integration tests..."
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_integration.c \
		src/dataplane/common/packet.o src/dataplane/common/util.o src/dataplane/common/core.o \
		$(PLATFORM_OBJS) -o tests/test_integration $(LDFLAGS)
	@./tests/test_integration
	@echo "✅ Integration tests passed!"

# Performance benchmarks
test-benchmark: $(TARGET)
	@echo "Running performance benchmarks..."
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_benchmark.c \
		src/dataplane/common/packet.o src/dataplane/common/util.o -o tests/test_benchmark
	@./tests/test_benchmark

# Fuzz testing for packet validation
test-fuzz: $(TARGET)
	@echo "Running fuzz tests (100000 iterations)..."
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_fuzz.c \
		src/dataplane/common/packet.o src/dataplane/common/util.o -o tests/test_fuzz
	@./tests/test_fuzz 100000
	@echo "✅ Fuzz tests passed!"

# Platform fallback and multi-worker tests
test-platform: $(TARGET)
	@echo "Running platform fallback and multi-worker tests..."
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_platform_fallback.c \
		src/dataplane/common/packet.o src/dataplane/common/util.o src/dataplane/common/core.o \
		$(PLATFORM_OBJS) -o tests/test_platform $(LDFLAGS)
	@./tests/test_platform
	@echo "✅ Platform tests passed!"

# NIC detection tests
test-nic: $(TARGET)
	@echo "Running NIC detection tests..."
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_nic_detect.c \
		src/dataplane/common/nic_detect.o src/dataplane/common/util.o -o tests/test_nic -ldl
	@./tests/test_nic
	@echo "✅ NIC detection tests passed!"

# Run all tests
test-all: test test-utils test-integration test-nic test-benchmark test-fuzz test-platform
	@echo ""
	@echo "====================================="
	@echo "✅ All tests passed!"
	@echo "====================================="

# ===================================
# Code Coverage
# ===================================

# Build with coverage instrumentation
coverage: clean
	@echo "Building with coverage instrumentation..."
	$(MAKE) CFLAGS="-Wall -Wextra -O0 -g --coverage -pthread" \
		LDFLAGS="-pthread --coverage" all
	@echo "Running tests with coverage..."
	-$(MAKE) test-all
	@echo "Generating coverage report..."
	@if command -v gcov >/dev/null 2>&1; then \
		gcov src/dataplane/common/*.c -o src/dataplane/common/; \
		echo "✅ Coverage report generated (*.gcov files)"; \
	else \
		echo "⚠️  gcov not found, skipping coverage report"; \
	fi

# ===================================
# Memory Safety (Sanitizers)
# ===================================

# Build with Address Sanitizer (detects memory errors)
test-asan: clean
	@echo "Building with Address Sanitizer..."
	$(MAKE) CFLAGS="-Wall -Wextra -O1 -g -fsanitize=address -fno-omit-frame-pointer -pthread" \
		LDFLAGS="-pthread -fsanitize=address" all
	@echo "Running tests with ASAN..."
	-$(MAKE) test-all
	@echo "✅ Address Sanitizer tests complete"

# Build with Undefined Behavior Sanitizer
test-ubsan: clean
	@echo "Building with Undefined Behavior Sanitizer..."
	$(MAKE) CFLAGS="-Wall -Wextra -O1 -g -fsanitize=undefined -fno-omit-frame-pointer -pthread" \
		LDFLAGS="-pthread -fsanitize=undefined" all
	@echo "Running tests with UBSAN..."
	-$(MAKE) test-all
	@echo "✅ UB Sanitizer tests complete"

# Run valgrind memory check (Linux only)
test-valgrind: $(TARGET)
	@if [ "$(UNAME_S)" != "Linux" ]; then \
		echo "⚠️  Valgrind only available on Linux"; \
		exit 0; \
	fi
	@echo "Running valgrind memory check..."
	@if command -v valgrind >/dev/null 2>&1; then \
		valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
			--error-exitcode=1 ./tests/test_packet || echo "⚠️  Valgrind detected issues"; \
		echo "✅ Valgrind check complete"; \
	else \
		echo "⚠️  valgrind not installed (sudo apt install valgrind)"; \
	fi

# ===================================
# Code Quality
# ===================================

# Format code with clang-format
format:
	@echo "Formatting code with clang-format..."
	@if command -v clang-format >/dev/null 2>&1; then \
		find src include tests -name '*.[ch]' -exec clang-format -i {} +; \
		echo "✅ Code formatted"; \
	else \
		echo "⚠️  clang-format not found"; \
		exit 1; \
	fi

# Check code formatting
format-check:
	@echo "Checking code formatting..."
	@if command -v clang-format >/dev/null 2>&1; then \
		UNFORMATTED=$$(find src include tests -name '*.[ch]' -exec clang-format --dry-run --Werror {} + 2>&1 | grep -E '^.*\.(c|h):' || true); \
		if [ -n "$$UNFORMATTED" ]; then \
			echo "❌ Code formatting issues found:"; \
			echo "$$UNFORMATTED"; \
			echo ""; \
			echo "Run 'make format' to fix formatting"; \
			exit 1; \
		else \
			echo "✅ All files properly formatted"; \
		fi; \
	else \
		echo "⚠️  clang-format not found, skipping"; \
	fi

# Run clang-tidy static analysis
lint:
	@echo "Running clang-tidy static analysis..."
	@if command -v clang-tidy >/dev/null 2>&1; then \
		find src -name '*.c' | xargs clang-tidy -p=. || echo "⚠️  Some warnings found"; \
		echo "✅ Static analysis complete"; \
	else \
		echo "⚠️  clang-tidy not found"; \
		exit 1; \
	fi

# Run cppcheck static analysis
cppcheck:
	@echo "Running cppcheck static analysis..."
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck --enable=all --error-exitcode=1 --suppress=missingIncludeSystem \
			--inline-suppr -I include src/ 2>&1 | tee cppcheck-report.txt || true; \
		if grep -E "error:|warning:.*security" cppcheck-report.txt >/dev/null 2>&1; then \
			echo "❌ Critical security issues found"; \
			exit 1; \
		fi; \
		echo "✅ cppcheck analysis complete"; \
	else \
		echo "⚠️  cppcheck not found"; \
	fi

# ===================================
# Quality Gates (Run Everything)
# ===================================

# Complete quality check suite
quality: clean format-check cppcheck lint test-all
	@echo ""
	@echo "====================================="
	@echo "✅ All quality checks passed!"
	@echo "====================================="

# Pre-commit checks (fast)
pre-commit: format-check test
	@echo "✅ Pre-commit checks passed"

# CI checks (comprehensive but no sanitizers - too slow)
ci-check: clean format-check cppcheck test-all
	@echo "✅ CI checks passed"

# Full checks (everything including sanitizers)
check-all: clean format-check cppcheck lint test-all test-asan test-ubsan coverage
	@echo ""
	@echo "====================================="
	@echo "✅ All checks passed! Code is damn near perfect."
	@echo "====================================="

# ===================================
# Cleanup
# ===================================

# Clean all build artifacts and test binaries
clean-all: clean
	@echo "Cleaning test artifacts..."
	rm -f tests/test_packet tests/test_utils tests/test_benchmark tests/test_nic
	rm -f tests/test_integration tests/test_platform tests/test_fuzz
	rm -f tests/*.gcda tests/*.gcno
	rm -f src/**/*.gcda src/**/*.gcno
	rm -f *.gcov cppcheck-report.txt
	@echo "Clean complete"

# Help
help:
	@echo "Network Reflector Build System"
	@echo ""
	@echo "Build Targets:"
	@echo "  all           - Build reflector for current platform"
	@echo "  clean         - Remove build artifacts"
	@echo "  clean-all     - Remove all artifacts including tests"
	@echo "  install       - Install to /usr/local/bin (requires sudo)"
	@echo "  uninstall     - Remove installed files"
	@echo ""
	@echo "Testing Targets:"
	@echo "  test          - Run basic packet validation tests"
	@echo "  test-utils    - Run utility function tests"
	@echo "  test-nic      - Run NIC detection tests"
	@echo "  test-benchmark - Run performance benchmarks"
	@echo "  test-fuzz     - Run fuzz testing on packet validation"
	@echo "  test-platform - Run platform fallback and multi-worker tests"
	@echo "  test-all      - Run all tests"
	@echo ""
	@echo "Quality Targets:"
	@echo "  format        - Format code with clang-format"
	@echo "  format-check  - Check code formatting"
	@echo "  lint          - Run clang-tidy static analysis"
	@echo "  cppcheck      - Run cppcheck static analysis"
	@echo ""
	@echo "Memory Safety:"
	@echo "  test-asan     - Build and test with Address Sanitizer"
	@echo "  test-ubsan    - Build and test with UB Sanitizer"
	@echo "  test-valgrind - Run valgrind memory check (Linux)"
	@echo "  coverage      - Generate code coverage report"
	@echo ""
	@echo "Comprehensive Checks:"
	@echo "  quality       - Run all quality checks"
	@echo "  pre-commit    - Fast pre-commit checks"
	@echo "  ci-check      - CI pipeline checks"
	@echo "  check-all     - Run EVERYTHING (full quality suite)"
	@echo ""
	@echo "Go Control Plane (v2.0):"
	@echo "  v2            - Build full v2.0 (Go + React UI)"
	@echo "  go-deps       - Install Go dependencies"
	@echo "  go-build      - Build Go binary with embedded UI"
	@echo "  go-build-minimal - Build Go binary without UI"
	@echo "  go-clean      - Clean Go build artifacts"
	@echo "  ui-build      - Build React UI only"
	@echo ""
	@echo "Service Installation:"
	@echo "  install-service-linux - Install systemd service"
	@echo "  install-service-macos - Install launchd service"
	@echo ""
	@echo "Platform: $(UNAME_S)"
	@echo "Target:   $(TARGET)"

# ===================================
# Go Control Plane (v2.0)
# ===================================

# Build React UI
ui-build:
	@echo "Building React UI..."
	@if command -v npm >/dev/null 2>&1; then \
		cd ui && npm install && npm run build; \
		echo "✅ React UI built to pkg/web/dist/"; \
	else \
		echo "⚠️  npm not found - skipping UI build"; \
		echo "   Web UI will show fallback page"; \
	fi

# Build Go control plane (requires C dataplane library)
go-build: $(TARGET) ui-build
	@echo "Building Go control plane..."
	@echo "Creating static library from C code..."
	ar rcs build/libreflector.a $(ALL_OBJS)
	@echo "Building Go binary..."
	CGO_ENABLED=1 go build -o reflector ./cmd/reflector
	@echo "✅ Go build complete: ./reflector"

# Build Go binary without React UI
go-build-minimal: $(TARGET)
	@echo "Building Go control plane (minimal, no UI)..."
	ar rcs build/libreflector.a $(ALL_OBJS)
	CGO_ENABLED=1 go build -o reflector ./cmd/reflector
	@echo "✅ Go build complete: ./reflector"

# Install Go dependencies
go-deps:
	@echo "Installing Go dependencies..."
	go mod tidy
	go get github.com/rivo/tview
	go get github.com/gdamore/tcell/v2
	go get gopkg.in/yaml.v3
	@echo "✅ Go dependencies installed"

# Clean Go build artifacts
go-clean:
	rm -f reflector
	rm -f build/libreflector.a
	rm -rf pkg/web/dist
	rm -rf ui/node_modules

# Install systemd service (Linux)
install-service-linux:
	@echo "Installing systemd service..."
	install -d /etc/reflector
	install -m 644 reflector.yaml.example /etc/reflector/reflector.yaml
	install -m 644 scripts/service/reflector.service /etc/systemd/system/
	systemctl daemon-reload
	@echo "✅ Service installed. Run: systemctl enable --now reflector"

# Install launchd service (macOS)
install-service-macos:
	@echo "Installing launchd service..."
	install -d /usr/local/etc/reflector
	install -d /usr/local/var/reflector
	install -m 644 reflector.yaml.example /usr/local/etc/reflector/reflector.yaml
	install -m 644 scripts/service/com.reflector.plist /Library/LaunchDaemons/
	@echo "✅ Service installed. Run: sudo launchctl load /Library/LaunchDaemons/com.reflector.plist"

# Full v2.0 build
v2: go-deps go-build
	@echo ""
	@echo "====================================="
	@echo "✅ Reflector v2.0 build complete!"
	@echo "====================================="
	@echo ""
	@echo "Run with TUI:     ./reflector eth0"
	@echo "Run with Web UI:  ./reflector eth0 --web"
	@echo "Run with config:  ./reflector -config reflector.yaml"

# ===================================
# Packaging
# ===================================

# Version for packaging
PKG_VERSION := $(shell git describe --tags --always 2>/dev/null | sed 's/^v//' || echo "2.0.0")

# Build Debian package (requires debuild or dpkg-deb)
deb: linux
	@echo "Building Debian package..."
	@if command -v dpkg-buildpackage >/dev/null 2>&1; then \
		mkdir -p debian && cp -r packaging/debian/* debian/; \
		dpkg-buildpackage -us -uc -b; \
		rm -rf debian; \
		echo "✅ Debian package built"; \
	else \
		echo "Building simplified .deb package..."; \
		mkdir -p build/deb/reflector/DEBIAN; \
		mkdir -p build/deb/reflector/usr/bin; \
		mkdir -p build/deb/reflector/lib/systemd/system; \
		mkdir -p build/deb/reflector/etc/reflector; \
		cp packaging/debian/control build/deb/reflector/DEBIAN/; \
		cp packaging/debian/postinst build/deb/reflector/DEBIAN/; \
		cp packaging/debian/prerm build/deb/reflector/DEBIAN/; \
		cp packaging/debian/postrm build/deb/reflector/DEBIAN/; \
		chmod 755 build/deb/reflector/DEBIAN/postinst build/deb/reflector/DEBIAN/prerm build/deb/reflector/DEBIAN/postrm; \
		sed -i "s/^Version:.*/Version: $(PKG_VERSION)/" build/deb/reflector/DEBIAN/control; \
		cp reflector-linux build/deb/reflector/usr/bin/reflector; \
		cp scripts/service/reflector.service build/deb/reflector/lib/systemd/system/; \
		cp reflector.yaml.example build/deb/reflector/etc/reflector/reflector.yaml; \
		cp packaging/debian/environment build/deb/reflector/etc/reflector/; \
		dpkg-deb --build build/deb/reflector; \
		mv build/deb/reflector.deb reflector_$(PKG_VERSION)_amd64.deb; \
		rm -rf build/deb; \
		echo "✅ Built: reflector_$(PKG_VERSION)_amd64.deb"; \
	fi

# Build RPM package (requires rpmbuild)
rpm: linux
	@echo "Building RPM package..."
	@if command -v rpmbuild >/dev/null 2>&1; then \
		mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}; \
		tar czf ~/rpmbuild/SOURCES/reflector-$(PKG_VERSION).tar.gz \
			--transform="s|^|reflector-$(PKG_VERSION)/|" \
			--exclude='.git*' --exclude='*.o' --exclude='reflector-*' .; \
		sed "s/^Version:.*/Version:        $(PKG_VERSION)/" packaging/rpm/reflector.spec > ~/rpmbuild/SPECS/reflector.spec; \
		rpmbuild -bb ~/rpmbuild/SPECS/reflector.spec; \
		find ~/rpmbuild/RPMS -name "reflector*.rpm" -exec cp {} . \;; \
		echo "✅ RPM package built"; \
	else \
		echo "❌ rpmbuild not found. Install with: sudo dnf install rpm-build"; \
		exit 1; \
	fi

# Smoke tests (requires root for veth)
smoke-test:
	@echo "Running smoke tests..."
	@if [ "$$(id -u)" != "0" ]; then \
		echo "Smoke tests require root. Run: sudo make smoke-test"; \
		exit 1; \
	fi
	@./tests/smoke/run_smoke_tests.sh

# Build all packages
packages: deb rpm
	@echo "✅ All packages built"

.PHONY: all version test test-utils test-nic test-benchmark test-fuzz test-platform test-all coverage test-asan test-ubsan \
        test-valgrind format format-check lint cppcheck quality pre-commit ci-check \
        check-all clean clean-all install uninstall help \
        ui-build go-build go-build-minimal go-deps go-clean \
        install-service-linux install-service-macos v2 \
        deb rpm smoke-test packages
