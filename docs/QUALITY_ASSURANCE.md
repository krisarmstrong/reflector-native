# Quality Assurance & Testing

This document describes the comprehensive quality assurance infrastructure for reflector-native.

## Overview

The project implements a multi-layered quality assurance strategy to ensure code is "damn near perfect":

1. **Code Formatting** - Automated formatting with clang-format
2. **Static Analysis** - Multiple static analysis tools (clang-tidy, cppcheck)
3. **Unit Testing** - Comprehensive unit test suite
4. **Integration Testing** - Full workflow testing
5. **Performance Benchmarking** - Continuous performance monitoring
6. **Memory Safety** - Sanitizers and valgrind checks
7. **Code Coverage** - Line and branch coverage tracking
8. **Continuous Integration** - Automated checks on every commit

## Quick Start

```bash
# Run all quality checks
make check-all

# Fast pre-commit checks
make pre-commit

# Individual checks
make format-check    # Check code formatting
make lint            # Run static analysis
make cppcheck        # Run security analysis
make test-all        # Run all tests
make test-asan       # Run with Address Sanitizer
make coverage        # Generate coverage report
```

## Test Suite

### Unit Tests

#### Packet Validation Tests (`tests/test_packet_validation.c`)
- ITO packet signature detection (PROBEOT, DATA:OT, LATENCY)
- MAC address validation
- Protocol validation (IPv4, UDP)
- Packet length validation
- Packet header reflection logic

```bash
make test
```

**Coverage**: 6 test cases covering all packet validation paths

#### Utility Tests (`tests/test_utils.c`)
- Timestamp monotonicity
- Signature type detection
- Statistics tracking (latency, signatures, errors)
- Helper function validation

```bash
make test-utils
```

**Coverage**: 8 test cases covering utility functions

### Performance Benchmarks (`tests/test_benchmark.c`)

Continuous performance monitoring to detect regressions:

```bash
make test-benchmark
```

**Metrics tracked**:
- Packet reflection throughput (ops/sec)
- Packet validation latency (ns/op)
- Signature detection performance

**Expected performance (macOS ARM64 with NEON SIMD)**:
- Packet reflection: ~80 M ops/sec (12 ns/op)
- Packet validation: ~0.5 M ops/sec (2000 ns/op)
- Signature detection: ~2000 M ops/sec (<1 ns/op)

## Code Quality

### Formatting

**Tool**: clang-format
**Config**: `.clang-format`
**Style**: Linux kernel style with 100-column limit

```bash
# Check formatting
make format-check

# Auto-format all code
make format
```

**CI Integration**: Formatting is checked on every PR. PRs with formatting issues will fail CI.

### Static Analysis

#### clang-tidy

Comprehensive static analysis checking for:
- Bug-prone patterns
- Security issues
- Performance anti-patterns
- Concurrency issues
- Code readability

**Config**: `.clang-tidy`

```bash
make lint
```

**Checks enabled**:
- `bugprone-*` - Common programming errors
- `clang-analyzer-*` - Deep static analysis
- `concurrency-*` - Thread safety issues
- `performance-*` - Performance anti-patterns
- `security-*` - Security vulnerabilities
- `cert-*` - CERT secure coding standards

#### cppcheck

Security-focused static analysis:
- Buffer overflows
- Memory leaks
- Null pointer dereferences
- Integer overflows
- Use-after-free
- Security vulnerabilities

```bash
make cppcheck
```

**Severity levels**:
- **Error**: Build fails
- **Warning (security)**: Build fails
- **Warning (other)**: Logged but doesn't fail build

## Memory Safety

### Address Sanitizer (ASAN)

Detects:
- Buffer overflows (heap and stack)
- Use-after-free
- Use-after-return
- Use-after-scope
- Double-free
- Memory leaks

```bash
make test-asan
```

**Performance impact**: ~2x slowdown
**CI Integration**: Run on every PR

### Undefined Behavior Sanitizer (UBSAN)

Detects:
- Integer overflow
- Divide by zero
- Null pointer dereference
- Misaligned pointer access
- Invalid shift operations

```bash
make test-ubsan
```

**Performance impact**: ~1.5x slowdown
**CI Integration**: Run on every PR

### Valgrind (Linux only)

Deep memory analysis:
- Memory leaks
- Invalid memory access
- Uninitialized memory use
- Invalid frees

```bash
make test-valgrind
```

**Performance impact**: ~10-50x slowdown
**CI Integration**: Run on Linux builds

## Code Coverage

Tracks line and branch coverage across the test suite.

```bash
make coverage
```

**Tools**:
- `gcov` - Coverage data collection
- `lcov` - Report generation

**Output**:
- `*.gcov` files - Per-file coverage
- `coverage.info` - Combined coverage data

**CI Integration**: Coverage reports uploaded as artifacts

## Continuous Integration

### Build Matrix

**Platforms**:
- Ubuntu Latest (AF_PACKET)
- Ubuntu Latest (AF_XDP with eBPF)
- macOS Latest (BPF)

**Jobs**:
1. **build-linux** - Standard Linux build + all tests
2. **build-linux-xdp** - AF_XDP build with libxdp/libbpf
3. **build-macos** - macOS build + all tests
4. **code-quality** - Formatting and static analysis
5. **memory-safety** - Sanitizers and valgrind
6. **coverage** - Code coverage reporting
7. **benchmark** - Performance regression detection

### Quality Gates

All PRs must pass:
- ✅ Build on Linux (AF_PACKET)
- ✅ Build on Linux (AF_XDP)
- ✅ Build on macOS
- ✅ All unit tests
- ✅ Code formatting check
- ✅ Static analysis (cppcheck)
- ✅ Address Sanitizer
- ✅ Undefined Behavior Sanitizer
- ✅ Valgrind (Linux)

### Workflow Files

- `.github/workflows/ci.yml` - Main CI pipeline
- `.github/workflows/security.yml` - Security scanning (CodeQL, Gitleaks)
- `.github/workflows/release.yml` - Release automation

## Version Management

### Semantic Versioning

Format: `MAJOR.MINOR.PATCH`

**Bump rules**:
- `MAJOR` - Breaking API changes
- `MINOR` - New features, backward compatible
- `PATCH` - Bug fixes, backward compatible

### Version Management Script

```bash
# Show current version
./scripts/version.sh current

# Bump patch version (1.3.0 -> 1.3.1)
./scripts/version.sh bump patch

# Bump minor version (1.3.0 -> 1.4.0)
./scripts/version.sh bump minor

# Bump major version (1.3.0 -> 2.0.0)
./scripts/version.sh bump major

# Set specific version
./scripts/version.sh set 1.4.0
```

**Files updated**:
- `VERSION` - Version file
- `include/reflector.h` - Version macros

### Release Process

1. Complete feature work
2. Update CHANGELOG.md
3. Bump version: `./scripts/version.sh bump <type>`
4. Commit changes
5. Create git tag: `git tag -a v1.4.0 -m "Release v1.4.0"`
6. Push with tags: `git push origin main --tags`
7. GitHub Actions automatically creates release

## Pre-commit Hooks

### Installation

```bash
./scripts/install-hooks.sh
```

### Checks Performed

Pre-commit hook (`.githooks/pre-commit`) runs:
1. **Secret detection** - Gitleaks scan
2. **Large file check** - Prevents accidentally committing large files
3. **Test validation** - Runs tests if source files modified

## Makefile Targets Reference

### Testing
- `make test` - Basic packet validation tests
- `make test-utils` - Utility function tests
- `make test-benchmark` - Performance benchmarks
- `make test-all` - All tests

### Memory Safety
- `make test-asan` - Address Sanitizer
- `make test-ubsan` - Undefined Behavior Sanitizer
- `make test-valgrind` - Valgrind analysis
- `make coverage` - Code coverage

### Code Quality
- `make format` - Auto-format code
- `make format-check` - Check formatting
- `make lint` - clang-tidy analysis
- `make cppcheck` - cppcheck analysis

### Comprehensive Checks
- `make quality` - All quality checks
- `make pre-commit` - Fast pre-commit checks
- `make ci-check` - CI pipeline checks
- `make check-all` - Complete quality suite

## Best Practices

### Before Committing

1. Run pre-commit checks:
   ```bash
   make pre-commit
   ```

2. If making significant changes, run full suite:
   ```bash
   make check-all
   ```

### Before Releasing

1. Run complete quality suite:
   ```bash
   make check-all
   ```

2. Review coverage report:
   ```bash
   make coverage
   ```

3. Run benchmarks and compare to previous:
   ```bash
   make test-benchmark
   ```

4. Update version and changelog
5. Create release tag

### Code Review Checklist

- [ ] All tests pass
- [ ] Code is properly formatted
- [ ] No static analysis warnings
- [ ] Memory safety checks pass
- [ ] Performance benchmarks comparable or better
- [ ] Documentation updated
- [ ] CHANGELOG.md updated

## Troubleshooting

### Formatting Issues

If `make format-check` fails:
```bash
make format
git add -u
git commit --amend --no-edit
```

### Sanitizer Failures

1. Review sanitizer output carefully
2. Use `ASAN_OPTIONS=symbolize=1` for better stack traces
3. Run under debugger if needed:
   ```bash
   make test-asan
   lldb ./tests/test_packet
   ```

### Coverage Too Low

1. Identify uncovered code:
   ```bash
   make coverage
   grep -A 5 "#####" *.gcov
   ```

2. Add tests for uncovered paths
3. Re-run coverage

## CI/CD Debugging

### View CI Logs

GitHub Actions logs available at:
```
https://github.com/krisarmstrong/reflector-native/actions
```

### Download Artifacts

CI uploads artifacts for:
- Test results
- Coverage reports
- Benchmark results
- Static analysis reports

### Reproduce CI Failures Locally

```bash
# Same checks as CI
make ci-check

# Full checks including sanitizers
make check-all
```

## Metrics & Goals

### Current Status (v1.3.0)

- **Test Coverage**: 6 packet validation tests + 8 utility tests
- **Code Coverage**: ~85% (estimated)
- **Static Analysis**: 0 critical warnings
- **Memory Safety**: 0 ASAN/UBSAN violations
- **Performance**: Meeting all targets

### Goals

- **Test Coverage**: 90%+ line coverage
- **Performance**: No regressions >5%
- **Security**: 0 critical vulnerabilities
- **Quality**: 0 high-severity static analysis warnings

---

**Maintained by**: Kris Armstrong
**Last Updated**: 2025-01-06 (v1.3.0)
