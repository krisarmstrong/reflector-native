/*
 * dataplane.go - CGO bindings to C dataplane
 *
 * This package provides a Go interface to the high-performance C dataplane.
 * The actual packet processing happens in C for maximum performance.
 */

package dataplane

/*
#cgo CFLAGS: -I${SRCDIR}/../../include
#cgo LDFLAGS: -L${SRCDIR}/../../build -lreflector

#include "reflector.h"
#include <stdlib.h>
#include <string.h>

// Helper to create config
static reflector_config_t make_config(
    const char *ifname,
    uint16_t ito_port,
    int filter_oui,
    uint8_t oui0, uint8_t oui1, uint8_t oui2,
    int reflect_mode,
    int use_dpdk,
    const char *dpdk_args
) {
    reflector_config_t config = {0};
    config.ito_port = ito_port;
    config.filter_oui = filter_oui ? true : false;
    config.oui[0] = oui0;
    config.oui[1] = oui1;
    config.oui[2] = oui2;
    config.reflect_mode = (reflect_mode_t)reflect_mode;
#if HAVE_DPDK
    config.use_dpdk = use_dpdk ? true : false;
    config.dpdk_args = (char *)dpdk_args;
#endif
    return config;
}
*/
import "C"

import (
	"fmt"
	"sync"
	"unsafe"

	"github.com/krisarmstrong/reflector-native/pkg/config"
)

// Stats holds dataplane statistics
type Stats struct {
	PacketsReceived  uint64
	PacketsReflected uint64
	BytesReceived    uint64
	BytesReflected   uint64
	TxErrors         uint64
	RxInvalid        uint64
	SigProbeOT       uint64
	SigDataOT        uint64
	SigLatency       uint64
	LatencyMin       float64
	LatencyAvg       float64
	LatencyMax       float64
	LatencyCount     uint64
}

// Dataplane wraps the C reflector context
type Dataplane struct {
	ctx     C.reflector_ctx_t
	cfg     *config.Config
	running bool
	mu      sync.RWMutex
}

// New creates a new dataplane instance
func New(cfg *config.Config) (*Dataplane, error) {
	dp := &Dataplane{
		cfg: cfg,
	}

	// Parse OUI
	oui, err := cfg.ParseOUI()
	if err != nil {
		return nil, fmt.Errorf("failed to parse OUI: %w", err)
	}

	// Create C config
	ifname := C.CString(cfg.Interface)
	defer C.free(unsafe.Pointer(ifname))

	var dpdkArgs *C.char
	if cfg.Platform.DPDKArgs != "" {
		dpdkArgs = C.CString(cfg.Platform.DPDKArgs)
		defer C.free(unsafe.Pointer(dpdkArgs))
	}

	filterOUI := 0
	if cfg.Filtering.FilterOUI {
		filterOUI = 1
	}

	useDPDK := 0
	if cfg.Platform.UseDPDK {
		useDPDK = 1
	}

	cConfig := C.make_config(
		ifname,
		C.uint16_t(cfg.Filtering.Port),
		C.int(filterOUI),
		C.uint8_t(oui[0]), C.uint8_t(oui[1]), C.uint8_t(oui[2]),
		C.int(cfg.ReflectModeInt()),
		C.int(useDPDK),
		dpdkArgs,
	)

	// Initialize reflector
	if C.reflector_init(&dp.ctx, ifname) < 0 {
		return nil, fmt.Errorf("failed to initialize reflector on %s", cfg.Interface)
	}

	// Apply config
	dp.ctx.config = cConfig

	return dp, nil
}

// Start begins packet processing
func (dp *Dataplane) Start() error {
	dp.mu.Lock()
	defer dp.mu.Unlock()

	if dp.running {
		return fmt.Errorf("dataplane already running")
	}

	if C.reflector_start(&dp.ctx) < 0 {
		return fmt.Errorf("failed to start reflector")
	}

	dp.running = true
	return nil
}

// Stop halts packet processing
func (dp *Dataplane) Stop() {
	dp.mu.Lock()
	defer dp.mu.Unlock()

	if !dp.running {
		return
	}

	// The C code handles stopping via the running flag
	dp.running = false
}

// Close cleans up dataplane resources
func (dp *Dataplane) Close() {
	dp.Stop()
	C.reflector_cleanup(&dp.ctx)
}

// GetStats returns current statistics
func (dp *Dataplane) GetStats() Stats {
	var cStats C.reflector_stats_t
	C.reflector_get_stats(&dp.ctx, &cStats)

	return Stats{
		PacketsReceived:  uint64(cStats.packets_received),
		PacketsReflected: uint64(cStats.packets_reflected),
		BytesReceived:    uint64(cStats.bytes_received),
		BytesReflected:   uint64(cStats.bytes_reflected),
		TxErrors:         uint64(cStats.tx_errors),
		RxInvalid:        uint64(cStats.rx_invalid),
		SigProbeOT:       uint64(cStats.sig_probeot_count),
		SigDataOT:        uint64(cStats.sig_dataot_count),
		SigLatency:       uint64(cStats.sig_latency_count),
		LatencyMin:       float64(cStats.latency.min_ns) / 1000.0,
		LatencyAvg:       float64(cStats.latency.avg_ns) / 1000.0,
		LatencyMax:       float64(cStats.latency.max_ns) / 1000.0,
		LatencyCount:     uint64(cStats.latency.count),
	}
}

// IsRunning returns whether the dataplane is active
func (dp *Dataplane) IsRunning() bool {
	dp.mu.RLock()
	defer dp.mu.RUnlock()
	return dp.running
}

// Interface returns the network interface name
func (dp *Dataplane) Interface() string {
	return dp.cfg.Interface
}

// Config returns the configuration
func (dp *Dataplane) Config() *config.Config {
	return dp.cfg
}
