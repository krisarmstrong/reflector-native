# Reflector Configuration Options

Complete reference for all configuration options available in the Network Reflector.

---

## Table of Contents
1. [Command-Line Options](#command-line-options)
2. [Configuration Structure](#configuration-structure)
3. [Default Values](#default-values)
4. [Runtime Configuration](#runtime-configuration)
5. [Platform-Specific Options](#platform-specific-options)

---

## Command-Line Options

### Basic Usage
```bash
./reflector-macos <interface> [options]
./reflector-linux <interface> [options]
```

### Available Flags

| Option | Type | Description | Default |
|--------|------|-------------|---------|
| `-v, --verbose` | Flag | Enable verbose logging (LOG_DEBUG level) | OFF |
| `--json` | Flag | Output statistics in JSON format | OFF |
| `--csv` | Flag | Output statistics in CSV format | OFF |
| `--latency` | Flag | Enable latency measurements | OFF |
| `--stats-interval N` | Integer | Statistics update interval in seconds | 10 |
| `-h, --help` | Flag | Show help message | - |

### Examples

**Basic usage with default settings:**
```bash
sudo ./reflector-macos en0
```

**Verbose logging with JSON output:**
```bash
sudo ./reflector-linux eth0 --verbose --json
```

**Latency measurement with 5-second stats:**
```bash
sudo ./reflector-linux eth0 --latency --stats-interval 5
```

**CSV output for logging:**
```bash
sudo ./reflector-linux eth0 --csv > reflector.log
```

---

## Configuration Structure

The `reflector_config_t` structure (defined in `include/reflector.h:191-211`) contains all runtime configuration options:

```c
typedef struct {
    char ifname[MAX_IFNAME_LEN];    /* Interface name */
    int ifindex;                     /* Interface index */
    uint8_t mac[6];                  /* Interface MAC address */
    int num_workers;                 /* Number of worker threads */
    bool enable_stats;               /* Enable statistics collection */
    bool promiscuous;                /* Enable promiscuous mode */
    bool zero_copy;                  /* Enable zero-copy mode (if supported) */
    int batch_size;                  /* Packet batch size */
    int frame_size;                  /* Frame size in UMEM */
    int num_frames;                  /* Number of frames in UMEM */
    int queue_id;                    /* RX/TX queue ID (-1 for auto) */
    bool busy_poll;                  /* Enable busy polling */
    int poll_timeout_ms;             /* Poll timeout in milliseconds */
    bool measure_latency;            /* Enable latency measurements */
    stats_format_t stats_format;     /* Statistics output format */
    int stats_interval_sec;          /* Statistics display interval (seconds) */
    int cpu_affinity;                /* CPU to pin worker thread (-1 for auto) */
    bool use_huge_pages;             /* Use huge pages for UMEM (Linux only) */
    bool software_checksum;          /* Calculate checksums in software (fallback) */
} reflector_config_t;
```

---

## Configuration Options Reference

### Network Interface

#### `ifname` (string)
- **Description**: Network interface name
- **Type**: `char[16]`
- **Default**: User-provided (required)
- **Examples**: `"eth0"`, `"en0"`, `"ens33"`
- **Location**: Set during initialization via `reflector_init()`

#### `ifindex` (int)
- **Description**: Kernel interface index
- **Type**: `int`
- **Default**: Auto-detected from interface name
- **Read-only**: Set by `get_interface_index()`

#### `mac` (uint8_t[6])
- **Description**: Interface MAC address
- **Type**: `uint8_t[6]`
- **Default**: Auto-detected from interface
- **Read-only**: Set by `get_interface_mac()`

---

### Worker Thread Configuration

#### `num_workers` (int)
- **Description**: Number of worker threads to spawn
- **Type**: `int`
- **Default**: Auto-detected
  - **Linux**: Number of RX queues from `get_num_rx_queues()`
  - **macOS**: `1` (single threaded)
- **Range**: 1-16 (MAX_WORKERS)
- **Notes**:
  - Each worker handles one RX queue
  - Multi-queue requires AF_XDP or multi-queue NIC

#### `cpu_affinity` (int)
- **Description**: CPU core to pin worker thread
- **Type**: `int`
- **Default**: `-1` (auto-detect from IRQ affinity)
- **Range**: -1 (auto) or 0 to (num_cpus - 1)
- **Platform**: Linux only
- **Location**: `core.c:257`
- **Example**:
```c
// Pin worker to CPU 2
config.cpu_affinity = 2;
```

#### `queue_id` (int)
- **Description**: RX/TX queue ID for this worker
- **Type**: `int`
- **Default**: Worker ID (0, 1, 2, ...)
- **Range**: -1 (auto) or 0 to (num_queues - 1)
- **Notes**: Multi-queue requires AF_XDP or RSS configuration

---

### Memory Management

#### `frame_size` (int)
- **Description**: Size of each frame in UMEM buffer pool
- **Type**: `int`
- **Default**: `4096` bytes (4 KB)
- **Constant**: `FRAME_SIZE` (reflector.h:71)
- **Location**: `core.c:253`
- **Notes**:
  - Must be power of 2
  - Typical MTU: 1500 bytes, jumbo frames: 9000 bytes
  - 4096 accommodates jumbo frames with headroom

#### `num_frames` (int)
- **Description**: Number of frames in UMEM buffer pool
- **Type**: `int`
- **Default**: `4096` frames
- **Constant**: `NUM_FRAMES` (reflector.h:72)
- **Location**: `core.c:254`
- **Total UMEM**: `4096 × 4096 = 16 MB`
- **Notes**: Higher values reduce packet drops under load

#### `use_huge_pages` (bool)
- **Description**: Use huge pages (2MB) for UMEM allocation
- **Type**: `bool`
- **Default**: `false`
- **Platform**: Linux only (AF_XDP)
- **Location**: `core.c:258`
- **Benefits**: Reduces TLB misses, improves performance
- **Requirements**:
  - Huge pages enabled in kernel
  - `sudo sysctl vm.nr_hugepages=16`

---

### Packet Processing

#### `batch_size` (int)
- **Description**: Number of packets to process per batch
- **Type**: `int`
- **Default**: `64`
- **Constant**: `BATCH_SIZE` (reflector.h:69)
- **Location**: `core.c:255`
- **Range**: 1-256 (recommended: 32-128)
- **Performance Impact**:
  - **Higher**: Better throughput, higher latency
  - **Lower**: Lower latency, reduced throughput

#### `zero_copy` (bool)
- **Description**: Enable zero-copy packet processing
- **Type**: `bool`
- **Default**: Platform-dependent
  - **AF_XDP**: `true` (native zero-copy)
  - **AF_PACKET/BPF**: `false` (requires copy)
- **Platform**: Linux AF_XDP with compatible NIC
- **Notes**: Requires driver support (Intel i40e, ixgbe, mlx5)

#### `software_checksum` (bool)
- **Description**: Calculate IP/UDP checksums in software
- **Type**: `bool`
- **Default**: `false` (use NIC offload)
- **Location**: `core.c:259`
- **Notes**:
  - Enable if NIC doesn't support TX checksum offload
  - Performance impact: ~100ns per packet

---

### Polling & Busy-Wait

#### `busy_poll` (bool)
- **Description**: Enable busy polling (spin without sleep)
- **Type**: `bool`
- **Default**: `false`
- **Notes**:
  - Reduces latency (~1-2 μs)
  - Burns 100% CPU per worker
  - Recommended for latency-sensitive workloads

#### `poll_timeout_ms` (int)
- **Description**: Timeout for poll() system call
- **Type**: `int`
- **Default**: `100` milliseconds
- **Location**: `core.c:256`
- **Range**: 1-1000 ms
- **Notes**:
  - Lower values: More responsive, higher CPU usage
  - Higher values: Lower CPU usage, slower shutdown

---

### Statistics & Monitoring

#### `enable_stats` (bool)
- **Description**: Enable statistics collection
- **Type**: `bool`
- **Default**: `true` (always enabled)
- **Notes**: Minimal overhead (~1% CPU)

#### `measure_latency` (bool)
- **Description**: Enable per-packet latency measurements
- **Type**: `bool`
- **Default**: `false`
- **CLI**: `--latency`
- **Location**: `main.c:141`
- **Overhead**: ~20ns per packet (timestamp capture)
- **Example**:
```c
config.measure_latency = true;
```

#### `stats_format` (stats_format_t)
- **Description**: Statistics output format
- **Type**: `enum { STATS_FORMAT_TEXT, STATS_FORMAT_JSON, STATS_FORMAT_CSV }`
- **Default**: `STATS_FORMAT_TEXT`
- **CLI**: `--json` or `--csv`
- **Location**: `main.c:17`

#### `stats_interval_sec` (int)
- **Description**: Statistics display interval
- **Type**: `int`
- **Default**: `10` seconds
- **CLI**: `--stats-interval N`
- **Range**: 1-3600 seconds
- **Location**: `main.c:143`

---

### Advanced Options

#### `promiscuous` (bool)
- **Description**: Enable promiscuous mode (receive all packets)
- **Type**: `bool`
- **Default**: `false`
- **Notes**:
  - Typically not needed (MAC-based filtering used)
  - May be required for bridging scenarios

---

## Default Values Summary

| Option | Default | Source |
|--------|---------|--------|
| `frame_size` | 4096 | `FRAME_SIZE` (reflector.h:71) |
| `num_frames` | 4096 | `NUM_FRAMES` (reflector.h:72) |
| `batch_size` | 64 | `BATCH_SIZE` (reflector.h:69) |
| `poll_timeout_ms` | 100 | `core.c:256` |
| `cpu_affinity` | -1 (auto) | `core.c:257` |
| `use_huge_pages` | false | `core.c:258` |
| `software_checksum` | false | `core.c:259` |
| `num_workers` | auto | Platform-specific |
| `stats_interval_sec` | 10 | `main.c:18` |
| `measure_latency` | false | User-specified |
| `stats_format` | TEXT | `main.c:17` |

---

## Runtime Configuration

### Using the API

Configuration can be modified between `reflector_init()` and `reflector_start()`:

```c
reflector_ctx_t rctx = {0};

// Initialize
reflector_init(&rctx, "eth0");

// Modify configuration
rctx.config.measure_latency = true;
rctx.config.stats_interval_sec = 5;
rctx.config.cpu_affinity = 2;

// Start with modified config
reflector_start(&rctx);
```

### Using reflector_set_config()

```c
reflector_ctx_t rctx = {0};
reflector_config_t config;

// Initialize with defaults
reflector_init(&rctx, "eth0");

// Get current config
reflector_get_config(&rctx, &config);

// Modify
config.measure_latency = true;
config.cpu_affinity = 2;

// Apply changes
reflector_set_config(&rctx, &config);

// Start
reflector_start(&rctx);
```

### Constraints

- ❌ **Cannot change config while running** (`rctx->running == true`)
- ❌ **Some options are read-only** (ifindex, mac, num_workers)
- ✅ **Can change most options before start**
- ✅ **Statistics can be reset anytime** (`reflector_reset_stats()`)

---

## Platform-Specific Options

### Linux-Specific

| Option | Platform | Notes |
|--------|----------|-------|
| `use_huge_pages` | Linux only | Requires kernel support |
| `cpu_affinity` | Linux only | Uses pthread affinity |
| `num_workers` | Linux | Auto-detects RX queues |
| `zero_copy` | Linux AF_XDP | Requires compatible NIC |

### macOS-Specific

| Option | Platform | Notes |
|--------|----------|-------|
| `num_workers` | macOS | Always 1 (single-threaded) |
| GCD threading | macOS | Uses QOS_CLASS_USER_INTERACTIVE |
| BPF device | macOS | Uses /dev/bpf* devices |

---

## Performance Tuning Recommendations

### Low Latency (<10 μs)
```c
config.batch_size = 16;          // Small batches
config.busy_poll = true;         // Spin instead of sleep
config.cpu_affinity = 2;         // Pin to isolated CPU
config.measure_latency = false;  // Disable overhead
```

### High Throughput (10+ Gbps)
```c
config.batch_size = 128;         // Large batches
config.use_huge_pages = true;    // Reduce TLB misses
config.zero_copy = true;         // AF_XDP zero-copy
config.num_workers = 8;          // Multi-queue RSS
```

### Balanced (Default)
```c
config.batch_size = 64;
config.poll_timeout_ms = 100;
config.cpu_affinity = -1;        // Auto-detect
```

---

## Environment Variables

Currently no environment variables are supported. All configuration is via:
1. Command-line flags
2. Direct API calls
3. Configuration structure modification

---

## Future Configuration Options

Planned for v2.0.0 (Network Extension Framework):
- `flow_filter` - Packet filtering rules
- `rate_limit` - Rate limiting configuration
- `buffer_pools` - Multiple buffer pool support
- `qos_policy` - QoS priority configuration
- `extension_mode` - NEFilterDataProvider options

---

## See Also

- [INTERNALS.md](INTERNALS.md) - Architecture and implementation details
- [PERFORMANCE.md](PERFORMANCE.md) - Performance tuning guide
- [ROADMAP.md](ROADMAP.md) - Future features and platform roadmap
- [reflector.h](../include/reflector.h) - API reference and structure definitions
