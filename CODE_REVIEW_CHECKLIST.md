# Code Review Checklist for Reflector-Native

This document provides a comprehensive checklist for code reviews to ensure consistent quality across all contributions.

## Pre-Review Checks

- [ ] Code builds without warnings (`make clean && make`)
- [ ] All existing tests pass (`make test`)
- [ ] Code formatting is consistent (`clang-format` applied)
- [ ] No merge conflicts with main branch

## Code Quality

### General

- [ ] Code follows existing patterns and conventions
- [ ] Functions are focused and do one thing well
- [ ] No dead code or commented-out code blocks
- [ ] No magic numbers (use constants/defines)
- [ ] Variable and function names are descriptive
- [ ] Code is self-documenting where possible

### C/C++ Specific

- [ ] All memory allocations are matched with frees
- [ ] No buffer overflows (bounds checking in place)
- [ ] All array accesses are bounds-checked
- [ ] Proper null pointer checks before dereferencing
- [ ] No use of unsafe functions (strcpy, sprintf, gets)
- [ ] Use strncpy, snprintf, fgets instead
- [ ] Integer overflow checks for size calculations
- [ ] Proper error handling (check return values)
- [ ] All switch statements have default cases
- [ ] No implicit type conversions that lose precision

### Packet Processing

- [ ] Minimum packet length validated before access
- [ ] Ethernet header (14 bytes) validated
- [ ] IP header length (IHL) validated (>= 5)
- [ ] Protocol field checked before accessing protocol-specific headers
- [ ] UDP header (8 bytes) validated before access
- [ ] Payload bounds checked before reading signature
- [ ] No trusting packet-supplied length fields without validation
- [ ] IPv6 vs IPv4 correctly distinguished
- [ ] Broadcast/multicast MAC handling correct

### Concurrency

- [ ] Thread-safe access to shared data
- [ ] Proper mutex acquisition and release
- [ ] No potential deadlocks (consistent lock ordering)
- [ ] Atomic operations for counters where needed
- [ ] Volatile used correctly for shared variables

### Platform Compatibility

- [ ] Code compiles on both Linux and macOS
- [ ] Platform-specific code properly guarded with #ifdef
- [ ] Correct header includes for each platform
- [ ] No Linux-only APIs used on macOS (and vice versa)
- [ ] Loopback interface name handled (lo vs lo0)

## Security

### Input Validation

- [ ] All external input validated before use
- [ ] Network packet contents not trusted
- [ ] Configuration values bounds-checked
- [ ] Interface names validated
- [ ] Port numbers in valid range (0-65535)

### Memory Safety

- [ ] No use-after-free
- [ ] No double-free
- [ ] No uninitialized memory reads
- [ ] Stack buffers appropriately sized
- [ ] Heap allocations have reasonable size limits

### Network Security

- [ ] Only reflect packets addressed to our MAC
- [ ] Drop malformed packets silently
- [ ] No amplification attack vectors
- [ ] Signature validation required before reflection

## Performance

- [ ] Hot paths are optimized
- [ ] No unnecessary memory allocations in packet path
- [ ] Batch operations used where applicable
- [ ] Cache-friendly data structures
- [ ] No blocking calls in fast path
- [ ] Statistics updates use atomic operations

## Testing

- [ ] Unit tests added for new functionality
- [ ] Edge cases covered in tests
- [ ] Error paths tested
- [ ] Tests are deterministic (no race conditions)
- [ ] Tests clean up after themselves

## Documentation

- [ ] Public APIs have doc comments
- [ ] Complex algorithms have explanatory comments
- [ ] Non-obvious code has inline comments
- [ ] README updated if user-facing changes
- [ ] CHANGELOG updated with notable changes

## Error Handling

- [ ] All error cases return appropriate error codes
- [ ] Errors logged at appropriate level
- [ ] Resources cleaned up on error paths
- [ ] Graceful degradation where possible

## Commit Message

- [ ] Clear, concise summary line (< 72 chars)
- [ ] Body explains "why" not just "what"
- [ ] References issue numbers if applicable
- [ ] Signed-off-by present if required

## Additional Platform-Specific Checks

### Linux

- [ ] AF_XDP code has proper fallback to AF_PACKET
- [ ] XDP program loaded/unloaded correctly
- [ ] UMEM handling correct
- [ ] Ring buffer sizes appropriate

### macOS

- [ ] BPF device opened with correct permissions
- [ ] BPF buffer size configured correctly
- [ ] BIOCIMMEDIATE set for low latency
- [ ] Proper cleanup of BPF resources

## Review Process

1. **Self-review first** - Author should review their own code before requesting review
2. **CI passes** - All automated checks must pass
3. **One approval minimum** - At least one maintainer approval required
4. **Address all comments** - All review comments must be resolved
5. **Squash commits** - Clean up commit history before merge

## Common Issues to Watch For

1. **Off-by-one errors** - Especially in packet length calculations
2. **Integer overflow** - When multiplying sizes or counts
3. **Missing validation** - Packet fields used without checking
4. **Resource leaks** - Sockets, memory, file descriptors
5. **Race conditions** - Shared state access without synchronization
6. **Platform assumptions** - Endianness, struct packing, type sizes
