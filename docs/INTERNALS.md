# Reflector Internals

This document describes the internal architecture, data structures, and implementation details of the Network Reflector.

## Table of Contents
- [Architecture Overview](#architecture-overview)
- [Threading Model](#threading-model)
- [Platform Abstraction](#platform-abstraction)
- [Buffer Management](#buffer-management)
- [Hot Path Optimization](#hot-path-optimization)
- [Statistics Collection](#statistics-collection)
- [Memory Layout](#memory-layout)

---

## Architecture Overview

### High-Level Flow

```
┌──────────────────────────────────────────────────────────────┐
│                      User Application                         │
└────────────────────┬─────────────────────────────────────────┘
                     │ reflector_init()
                     │ reflector_start()
                     │ reflector_stop()
                     v
┌──────────────────────────────────────────────────────────────┐
│                    Core Engine (core.c)                       │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  reflector_ctx_t                                        │ │
│  │   - config                                              │ │
│  │   - workers[]    (per-worker contexts)                  │ │
│  │   - worker_tids[] or worker_queues[] (platform-specific)│ │
│  └─────────────────────────────────────────────────────────┘ │
└────────────────┬─────────────────────────┬───────────────────┘
                 │                         │
      ┌──────────┴──────────┐   ┌─────────┴────────────┐
      │   Worker Thread 0   │   │   Worker Thread N    │
      │   (pthread/GCD)     │   │   (pthread/GCD)      │
      └──────────┬──────────┘   └─────────┬────────────┘
                 │                         │
      ┌──────────v──────────────────────────v────────────┐
      │       Platform Abstraction Layer                  │
      │       (platform_ops_t interface)                  │
      └────┬─────────────┬─────────────┬─────────────────┘
           │             │             │
  ┌────────v────┐ ┌─────v─────┐ ┌────v─────┐
  │  AF_XDP     │ │ AF_PACKET │ │  macOS   │
  │  (Linux)    │ │  (Linux)  │ │   BPF    │
  └─────────────┘ └───────────┘ └──────────┘
```

### Component Responsibilities

| Component | File | Purpose |
|-----------|------|---------|
| **Core Engine** | `core.c` | Worker lifecycle, statistics aggregation |
| **Packet Logic** | `packet.c` | Validation, reflection, SIMD optimization |
| **Utilities** | `util.c` | Interface queries, logging, timestamps |
| **Platform Layer** | `*_platform.c` | OS-specific packet I/O |
| **CLI** | `main.c` | Argument parsing, statistics display |

---

## Threading Model

### Linux (pthreads)

```c
reflector_ctx_t {
    pthread_t *worker_tids;  // Array of thread IDs
}

// Creation
pthread_create(&worker_tids[i], NULL, worker_thread, &workers[i]);

// Synchronization
pthread_join(worker_tids[i], NULL);
```

### macOS (GCD)

```c
reflector_ctx_t {
    dispatch_group_t worker_group;      // Synchronization primitive
    dispatch_queue_t *worker_queues;    // Per-worker serial queues
}

// Creation
dispatch_queue_t queue = dispatch_queue_create(
    "com.reflector.worker0",
    DISPATCH_QUEUE_SERIAL  // Serial queue
);
dispatch_queue_attr_t attr = dispatch_queue_attr_make_with_qos_class(
    DISPATCH_QUEUE_SERIAL,
    QOS_CLASS_USER_INTERACTIVE,  // Highest priority
    0
);

// Launch
dispatch_group_enter(worker_group);
dispatch_async(worker_queue, ^{
    worker_loop(wctx);
    dispatch_group_leave(worker_group);
});

// Synchronization
dispatch_group_wait(worker_group, DISPATCH_TIME_FOREVER);
```

**Key Differences**:
- **pthreads**: Manual thread management, explicit TID storage
- **GCD**: Automatic thread pool, system manages thread lifecycle
- **QoS**: GCD provides built-in priority management
- **Foundation**: GCD is native to Apple frameworks (Network Extension)

### Worker Lifecycle

```
┌─────────────────────────────────────────────────────────────┐
│                   reflector_start()                          │
└────────────┬────────────────────────────────────────────────┘
             │
             v
    ┌────────────────────┐
    │ Allocate workers[] │
    │ Allocate contexts  │
    └────────┬───────────┘
             │
             v
    ┌────────────────────────────┐
    │ FOR each worker:           │
    │  - platform_ops->init()    │
    │  - Set CPU affinity        │
    │  - Create thread/queue     │
    └────────┬───────────────────┘
             │
             v
    ┌────────────────────┐
    │  drop_privileges() │ (Linux only, after all sockets open)
    └────────┬───────────┘
             │
             v
┌────────────────────────────────────────────────────────────┐
│                      Worker Loop                            │
│                                                             │
│  while (running):                                           │
│    1. recv_batch() -> pkts_rx[]                             │
│    2. FOR each packet:                                      │
│         if is_ito_packet():                                 │
│           - reflect_packet_inplace()                        │
│           - pkts_tx[num_tx++] = pkt                         │
│    3. send_batch(pkts_tx, num_tx)                           │
│    4. release_batch(pkts_tx, num_tx)  // Return buffers     │
│    5. flush_stats_batch() every N batches                   │
│                                                             │
└────────────────────────────────────────────────────────────┘
             │
             v (running = false)
    ┌────────────────────┐
    │ flush_stats_batch()│ (final flush)
    └────────┬───────────┘
             │
             v
    ┌────────────────────┐
    │   Thread exits     │
    └────────────────────┘
```

---

## Platform Abstraction

### Interface Design

```c
typedef struct {
    const char *name;  // "AF_XDP", "AF_PACKET", "macOS BPF"

    int (*init)(reflector_ctx_t *rctx, worker_ctx_t *wctx);
    void (*cleanup)(worker_ctx_t *wctx);
    int (*recv_batch)(worker_ctx_t *wctx, packet_t *pkts, int max_pkts);
    int (*send_batch)(worker_ctx_t *wctx, packet_t *pkts, int num_pkts);
    void (*release_batch)(worker_ctx_t *wctx, packet_t *pkts, int num_pkts);
} platform_ops_t;
```

**Key Design Decisions**:
- **Function pointers**: Zero overhead (resolved at init)
- **Batch-oriented**: Amortizes overhead (64 packets/batch)
- **Optional release_batch**: NULL check before call (some platforms don't need it)
- **Opaque context**: `platform_ctx_t*` hides platform details

### Platform-Specific Contexts

Each platform has its own internal context structure:

**AF_XDP (Linux)**:
```c
struct platform_ctx {
    struct xsk_socket_info xsk_info;    // libxdp socket
    struct xsk_umem_info umem;          // User memory region
    uint64_t *umem_area;                // UMEM buffer
    int sig_map_fd;                     // eBPF hash map FD
    int outstanding_tx;                 // TX buffers in flight
};
```

**AF_PACKET (Linux)**:
```c
struct platform_ctx {
    int sock_fd;                  // Raw socket
    void *rx_ring;                // mmap'd RX ring
    void *tx_ring;                // mmap'd TX ring
    unsigned int rx_frame_idx;    // Current RX frame
    unsigned int tx_frame_idx;    // Current TX frame
};
```

**macOS BPF**:
```c
struct platform_ctx {
    int bpf_fd;                // /dev/bpfN file descriptor
    uint8_t *read_buffer;      // Read buffer (4MB)
    uint8_t *write_buffer;     // Write buffer
    size_t read_offset;        // Current offset in buffer
};
```

---

## Buffer Management

### Zero-Copy vs Copy Semantics

| Platform | RX Path | TX Path | Buffer Ownership |
|----------|---------|---------|------------------|
| **AF_XDP** | Zero-copy (UMEM) | Zero-copy (UMEM) | Complex (FQ/CQ rings) |
| **AF_PACKET** | Zero-copy (mmap) | Copy (mmap ring) | Kernel-managed |
| **macOS BPF** | Copy (read()) | Copy (write()) | Simple (malloc) |

### AF_XDP Buffer Lifecycle

```
┌──────────────────────────────────────────────────────────────┐
│                     UMEM (User Memory)                        │
│  ┌──────────┬──────────┬──────────┬──────────┬───────────┐  │
│  │ Frame 0  │ Frame 1  │ Frame 2  │  ...     │ Frame N   │  │
│  └──────────┴──────────┴──────────┴──────────┴───────────┘  │
└──────────────────────────────────────────────────────────────┘
         │              │              │
         │ RX           │ TX           │ Available
         v              v              v
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│  RX Ring     │ │  TX Ring     │ │  Fill Queue  │
│  (kernel →   │ │  (user →     │ │  (user →     │
│   user)      │ │   kernel)    │ │   kernel)    │
└──────────────┘ └──────────────┘ └──────────────┘
                        │
                        v
                 ┌──────────────┐
                 │ Completion   │
                 │ Queue        │
                 │ (kernel →    │
                 │  user)       │
                 └──────────────┘
```

**Buffer Flow**:
1. **Fill Queue (FQ)**: User provides available frame addresses to kernel
2. **RX Ring**: Kernel fills frames with received packets
3. **User Processing**: `recv_batch()` returns pointers into UMEM
4. **TX Ring**: User queues frames for transmission
5. **Completion Queue (CQ)**: Kernel returns completed TX frames
6. **Recycling**: `release_batch()` polls CQ and returns frames to FQ

**Critical Bug Fixed in v1.3.1**:
- **Before**: CQ was only polled when TX ring full → UMEM exhaustion
- **After**: Eager CQ polling after every `send_batch()` → proper recycling

### AF_PACKET Ring Buffer

```
┌──────────────────────────────────────────────────────────────┐
│                  mmap'd Ring Buffer                           │
│  ┌──────────┬──────────┬──────────┬──────────┬───────────┐  │
│  │ Block 0  │ Block 1  │ Block 2  │  ...     │ Block N   │  │
│  └────┬─────┴────┬─────┴────┬─────┴──────────┴───────────┘  │
│       │          │          │                                 │
│  ┌────v─────┐┌───v──────┐┌─v───────┐                         │
│  │ Frame 0  ││ Frame 1  ││ Frame 2 │  (128 frames/block)     │
│  └──────────┘└──────────┘└─────────┘                         │
└──────────────────────────────────────────────────────────────┘
```

**Status Flags**:
- `TP_STATUS_KERNEL`: Frame owned by kernel
- `TP_STATUS_USER`: Frame ready for user consumption
- `TP_STATUS_AVAILABLE`: TX frame ready for use
- `TP_STATUS_SEND_REQUEST`: TX frame queued for send

---

## Hot Path Optimization

### Batched Statistics

**Problem**: Atomic increments cause cache line bouncing across CPUs

**Solution**: Local batch accumulation, periodic flush

```c
// Per-worker local batch (stack-allocated)
typedef struct {
    uint64_t packets_received;
    uint64_t packets_reflected;
    uint64_t bytes_received;
    uint64_t bytes_reflected;
    // ... more counters
    int batch_count;  // Flush when reaches STATS_FLUSH_BATCHES
} stats_batch_t;

// Flush to worker stats (shared memory)
void flush_stats_batch(reflector_stats_t *stats, stats_batch_t *batch) {
    stats->packets_received += batch->packets_received;  // Single write
    stats->packets_reflected += batch->packets_reflected;
    // ... more
    memset(batch, 0, sizeof(*batch));  // Reset
}
```

**Impact**: Reduces 512 atomic operations to 1 per flush

### SIMD Packet Reflection

**x86_64 (SSE2)**:
```c
__m128i src_dst = _mm_loadu_si128((__m128i *)&data[0]);  // Load 16 bytes
__m128i mask = _mm_set_epi8(/* shuffle indices */);
__m128i swapped = _mm_shuffle_epi8(src_dst, mask);       // Swap in parallel
_mm_storeu_si128((__m128i *)&data[0], swapped);          // Store 16 bytes
```

**ARM64 (NEON)**:
```c
uint8x16_t src_dst = vld1q_u8(&data[0]);                 // Load 16 bytes
uint8x16_t mask = (uint8x16_t){/* shuffle indices */};
uint8x16_t swapped = vqtbl1q_u8(src_dst, mask);          // Table lookup
vst1q_u8(&data[0], swapped);                             // Store 16 bytes
```

**Fallback (portable)**:
```c
// Simple byte swapping for MAC (6+6 bytes)
uint8_t tmp[6];
memcpy(tmp, &data[0], 6);
memcpy(&data[0], &data[6], 6);
memcpy(&data[6], tmp, 6);
// Similar for IPs and ports
```

### Prefetch Optimization (v1.8.1)

```c
for (int i = 0; i < rcvd; i++) {
    /* Prefetch next packet while processing current */
    if (i + 1 < rcvd) {
        PREFETCH_READ(pkts_rx[i + 1].data);  // L1 cache hint
    }
    // Process pkts_rx[i]
}
```

**Impact**: Hides 50-100ns memory latency by overlapping prefetch with processing

---

## Statistics Collection

### Two-Level Aggregation

```
┌──────────────────────────────────────────────────────────┐
│                   Worker Threads                          │
│  ┌───────────────┐  ┌───────────────┐  ┌──────────────┐ │
│  │ Worker 0      │  │ Worker 1      │  │ Worker N     │ │
│  │               │  │               │  │              │ │
│  │ stats_batch_t │  │ stats_batch_t │  │stats_batch_t │ │
│  │ (stack)       │  │ (stack)       │  │ (stack)      │ │
│  └───────┬───────┘  └───────┬───────┘  └──────┬───────┘ │
│          │                  │                  │          │
│          v flush            v flush            v flush    │
│  ┌───────────────┐  ┌───────────────┐  ┌──────────────┐ │
│  │ worker.stats  │  │ worker.stats  │  │ worker.stats │ │
│  │ (per-worker)  │  │ (per-worker)  │  │ (per-worker) │ │
│  └───────┬───────┘  └───────┬───────┘  └──────┬───────┘ │
└──────────┼──────────────────┼──────────────────┼─────────┘
           │                  │                  │
           └──────────┬───────┴──────────────────┘
                      v aggregate (on demand)
              ┌──────────────────┐
              │ global_stats     │
              │ (user-visible)   │
              └──────────────────┘
```

**Aggregation** (in `reflector_get_stats()`):
```c
void reflector_get_stats(const reflector_ctx_t *rctx, reflector_stats_t *stats) {
    memset(stats, 0, sizeof(*stats));

    for (int i = 0; i < rctx->num_workers; i++) {
        stats->packets_received += rctx->workers[i].stats.packets_received;
        stats->packets_reflected += rctx->workers[i].stats.packets_reflected;
        // ... sum all counters
    }

    // Calculate derived metrics
    double elapsed = (now - stats->start_time_ns) / 1e9;
    stats->pps = stats->packets_reflected / elapsed;
    stats->mbps = (stats->bytes_reflected * 8.0) / (elapsed * 1e6);
}
```

---

## Memory Layout

### Typical Memory Usage

| Platform | UMEM/Ring Size | Per-Worker Memory | Total (4 workers) |
|----------|----------------|-------------------|-------------------|
| **AF_XDP** | 16MB UMEM | ~16MB | ~64MB |
| **AF_PACKET** | 8MB ring | ~8MB | ~32MB |
| **macOS BPF** | 4MB buffers | ~8MB | ~32MB |

### Context Structures

**Size Estimates**:
```
sizeof(reflector_ctx_t)  ~256 bytes
sizeof(worker_ctx_t)     ~1KB (includes stats)
sizeof(packet_t)         24 bytes
BATCH_SIZE * packet_t    ~1.5KB per batch
```

### Stack vs Heap

| Allocation | Where | Lifetime |
|------------|-------|----------|
| `reflector_ctx_t` | User-provided (often stack) | Application lifetime |
| `workers[]` | Heap (malloc) | Between start/stop |
| `platform_ctx_t` | Heap (malloc) | Worker lifetime |
| `stats_batch_t` | Stack (worker thread) | Per loop iteration |
| `pkts_rx[BATCH_SIZE]` | Stack (worker thread) | Per loop iteration |

---

## Error Handling

### Errno Preservation (v1.8.1)

**Pattern**:
```c
int some_function() {
    if (syscall(...) < 0) {
        int saved_errno = errno;  // Save immediately
        reflector_log(LOG_ERROR, "...", strerror(saved_errno));
        close(fd);  // Cleanup may clobber errno
        errno = saved_errno;  // Restore for caller
        return -1;
    }
}
```

### Graceful Degradation

**Linux Platform Fallback**:
```
1. Try AF_XDP initialization
   ├─ Success → Use zero-copy AF_XDP
   └─ Failure → Log warning, try AF_PACKET
       ├─ Success → Use optimized AF_PACKET (TPACKET_V3)
       └─ Failure → Critical error, exit
```

**macOS** (no fallback):
```
1. Try BPF initialization
   └─ Failure → Critical error, exit
```

---

## Security

### Privilege Dropping (v1.8.1)

**When**: After all sockets are created and bound

**Linux Implementation**:
```c
int drop_privileges(void) {
    if (getuid() != 0) return 0;  // Not root, nothing to drop

    setgroups(0, NULL);       // Drop supplementary groups
    setgid(65534);            // nobody group
    setuid(65534);            // nobody user

    // Can no longer regain root privileges
}
```

**macOS**: No-op (BPF requires root or `/dev/bpf` group membership throughout runtime)

### Input Validation

All packet processing includes:
1. **Length checks**: `len < MIN_ITO_PACKET_LEN`
2. **Bounds checks**: Before array access
3. **MAC validation**: Byte-by-byte comparison
4. **EtherType/Protocol checks**: IPv4 and UDP only

---

## Future Architecture (v2.0.0)

### Network Extension Framework (macOS)

Planned architectural change for v2.0.0:

```
Current:               Future:
┌──────────┐           ┌─────────────────────────┐
│ User App │           │ User App                │
└────┬─────┘           └────┬────────────────────┘
     │                      │ XPC
     v                      v
┌────────────┐         ┌─────────────────────────┐
│ BPF (/dev) │         │ Network Extension       │
└────┬───────┘         │ (Kernel-level sandbox)  │
     │ copy            │ ┌─────────────────────┐ │
     v                 │ │ NEFilterDataProvider│ │
┌────────────┐         │ │ - Packet filtering  │ │
│  Kernel    │         │ │ - Zero-copy capable │ │
└────────────┘         │ └─────────────────────┘ │
                       └──────────┬──────────────┘
                                  │ DMA
                                  v
                              ┌────────────┐
                              │   Kernel   │
                              └────────────┘
```

**Benefits**:
- XDP-like performance on macOS
- Native Apple integration
- Proper security sandbox
- Foundation for iOS support

**Challenges**:
- Requires code signing
- System extension approval
- Major architectural change
- XPC communication overhead

---

## Performance Tuning

### CPU Affinity

**Linux** (automatic):
```c
int get_queue_cpu_affinity(const char *ifname, int queue_id) {
    // Round-robin assignment: queue_id % num_cpus
    // In production: Parse /proc/irq/<irq>/smp_affinity
}
```

**Manual Override**:
```c
rctx.config.cpu_affinity = 3;  // Pin all workers to CPU 3
```

### Huge Pages

**Linux Only**:
```c
// mmap with MAP_HUGETLB
int flags = MAP_SHARED | MAP_LOCKED | MAP_POPULATE | MAP_HUGETLB | (21 << MAP_HUGE_SHIFT);
void *umem = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, -1, 0);
```

**Impact**: Reduces TLB misses, ~2-5% throughput improvement

---

## Debugging

### Hot-Path Debug Logging

**Disabled by default** (zero overhead):
```c
#define DEBUG_LOG(fmt, ...) ((void)0)  // Optimized away
```

**Enable with**:
```bash
make CFLAGS="-DENABLE_HOT_PATH_DEBUG ..."
```

### Sanitizers

**Address Sanitizer**:
```bash
make test-asan
```

**Undefined Behavior Sanitizer**:
```bash
make test-ubsan
```

---

## References

- **AF_XDP**: https://www.kernel.org/doc/html/latest/networking/af_xdp.html
- **AF_PACKET**: https://man7.org/linux/man-pages/man7/packet.7.html
- **Berkeley Packet Filter**: https://www.freebsd.org/cgi/man.cgi?query=bpf
- **Grand Central Dispatch**: https://developer.apple.com/documentation/dispatch
- **SIMD Intrinsics**: https://www.intel.com/content/www/us/en/docs/intrinsics-guide
