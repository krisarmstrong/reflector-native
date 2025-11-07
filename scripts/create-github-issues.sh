#!/bin/bash
# Create GitHub issues for code review findings
# Uses gh CLI: https://cli.github.com/

set -e

echo "Creating GitHub issues for code review findings..."
echo ""

# Critical Issues

gh issue create \
  --title "[CRITICAL] C-4: Race condition in CPU feature detection" \
  --label "bug,priority:critical,area:performance" \
  --body "## Problem
Multiple threads can call \`detect_cpu_features()\` simultaneously, causing a data race and undefined behavior.

## Location
- File: \`src/dataplane/common/packet.c\`
- Lines: 23-24, 374-383

## Impact
- Data race (undefined behavior)
- Possible crashes
- Incorrect CPU feature detection

## Fix
Use \`pthread_once\` for thread-safe initialization:
\`\`\`c
static pthread_once_t cpu_detect_once = PTHREAD_ONCE_INIT;
pthread_once(&cpu_detect_once, detect_cpu_features);
\`\`\`

## References
- Code Review Report: docs/CODE_REVIEW_REPORT.md
- Patch: docs/CRITICAL_FIXES_PATCH.txt

## Priority
🚨 **CRITICAL** - Must fix before production deployment"

gh issue create \
  --title "[CRITICAL] C-2: Missing clock_gettime return value checks" \
  --label "bug,priority:critical,area:reliability" \
  --body "## Problem
\`clock_gettime()\` can fail but return value not checked, could use uninitialized timespec.

## Location
- File: \`src/dataplane/common/util.c\`
- Lines: 60, 224

## Impact
- Could crash on clock_gettime failure
- Uninitialized timespec usage
- Incorrect timestamps

## Fix
\`\`\`c
if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
    return 0;  /* Fallback on error */
}
\`\`\`

## References
- Code Review Report: docs/CODE_REVIEW_REPORT.md
- Patch: docs/CRITICAL_FIXES_PATCH.txt

## Priority
🚨 **CRITICAL** - Must fix before production"

gh issue create \
  --title "[HIGH] H-6: Unaligned pointer casts (undefined behavior)" \
  --label "bug,priority:high,area:portability" \
  --body "## Problem
Casting unaligned buffer to \`uint32_t*\` violates strict aliasing and causes undefined behavior.

## Location
- File: \`src/dataplane/common/packet.c\`
- Lines: 344-356

## Impact
- Undefined behavior (C standard violation)
- Possible crashes on ARM architecture
- Misaligned memory access penalties

## Fix
Use \`memcpy\` for unaligned access:
\`\`\`c
uint32_t ip_src_val, ip_dst_val;
memcpy(&ip_src_val, &data[ip_offset + IP_SRC_OFFSET], 4);
memcpy(&ip_dst_val, &data[ip_offset + IP_DST_OFFSET], 4);
memcpy(&data[ip_offset + IP_SRC_OFFSET], &ip_dst_val, 4);
memcpy(&data[ip_offset + IP_DST_OFFSET], &ip_src_val, 4);
\`\`\`

## References
- Code Review Report: docs/CODE_REVIEW_REPORT.md
- Patch: docs/CRITICAL_FIXES_PATCH.txt

## Priority
⚠️ **HIGH** - Fix before release"

gh issue create \
  --title "[HIGH] H-4: sleep(1) unreliable for thread synchronization" \
  --label "bug,priority:high,area:concurrency" \
  --body "## Problem
Using \`sleep(1)\` doesn't guarantee worker threads have exited, could cleanup resources while threads still running.

## Location
- File: \`src/dataplane/common/core.c\`
- Line: 408

## Impact
- Race condition on cleanup
- Possible use-after-free
- Resource cleanup while threads running

## Fix
Use \`pthread_join\` instead:
\`\`\`c
for (int i = 0; i < rctx->num_workers; i++) {
    pthread_join(rctx->worker_tids[i], NULL);
}
\`\`\`

Requires storing pthread_t handles in reflector_ctx_t.

## References
- Code Review Report: docs/CODE_REVIEW_REPORT.md
- Patch: docs/CRITICAL_FIXES_PATCH.txt

## Priority
⚠️ **HIGH** - Fix before release"

gh issue create \
  --title "[HIGH] H-3: debug_count not thread-safe" \
  --label "bug,priority:high,area:concurrency" \
  --body "## Problem
Static variable \`debug_count\` shared across threads without synchronization.

## Location
- File: \`src/dataplane/common/packet.c\`
- Line: 67

## Impact
- Race condition
- Could print more than 3 debug messages
- Thread safety violation

## Fix
Use thread-local storage:
\`\`\`c
static _Thread_local int debug_count = 0;
\`\`\`

## References
- Code Review Report: docs/CODE_REVIEW_REPORT.md
- Patch: docs/CRITICAL_FIXES_PATCH.txt

## Priority
⚠️ **HIGH** - Fix before release"

gh issue create \
  --title "[HIGH] H-1: Unsafe atoi() without validation" \
  --label "bug,priority:high,area:input-validation" \
  --body "## Problem
\`atoi()\` doesn't detect errors, returns 0 on invalid input (potential DOS vector).

## Location
- File: \`src/dataplane/common/main.c\`
- Line: 102

## Impact
- No error detection (\"abc\" becomes 0)
- Potential denial of service
- Silent failures

## Fix
Use \`strtol()\` with error checking:
\`\`\`c
char *endptr;
long val = strtol(argv[i], &endptr, 10);
if (*endptr != '\\0' || val <= 0 || val > INT_MAX) {
    fprintf(stderr, \"Invalid stats interval: %s\\n\", argv[i]);
    return 1;
}
g_stats_interval = (int)val;
\`\`\`

## References
- Code Review Report: docs/CODE_REVIEW_REPORT.md

## Priority
⚠️ **HIGH** - Fix before release"

gh issue create \
  --title "[HIGH] H-2: Missing null termination with strncpy" \
  --label "bug,priority:high,area:string-handling" \
  --body "## Problem
\`strncpy\` doesn't guarantee null termination if source is MAX_IFNAME_LEN-1 chars.

## Location
- File: \`src/dataplane/common/core.c\`
- Line: 219

## Impact
- String buffer overflow
- Missing null terminator
- Possible crashes

## Fix
Explicitly null terminate:
\`\`\`c
strncpy(rctx->config.ifname, ifname, MAX_IFNAME_LEN - 1);
rctx->config.ifname[MAX_IFNAME_LEN - 1] = '\\0';
\`\`\`

## References
- Code Review Report: docs/CODE_REVIEW_REPORT.md

## Priority
⚠️ **HIGH** - Fix before release"

gh issue create \
  --title "[HIGH] H-7: Integer overflow in PPS/MBPS calculation" \
  --label "bug,priority:high,area:statistics" \
  --body "## Problem
Division by zero if elapsed time is 0, results in NaN/Inf values.

## Location
- File: \`src/dataplane/common/main.c\`
- Lines: 27-28

## Impact
- NaN or Inf in statistics output
- Division by zero

## Fix
Guard against zero:
\`\`\`c
double pps = (elapsed > 0) ? stats->packets_reflected / elapsed : 0.0;
double mbps = (elapsed > 0) ? (stats->bytes_reflected * 8.0) / (elapsed * 1000000.0) : 0.0;
\`\`\`

## References
- Code Review Report: docs/CODE_REVIEW_REPORT.md

## Priority
⚠️ **HIGH** - Fix before release"

echo ""
echo "✅ GitHub issues created successfully!"
echo ""
echo "View issues at: https://github.com/krisarmstrong/reflector-native/issues"
