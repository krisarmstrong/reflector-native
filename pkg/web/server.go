/*
 * server.go - Embedded web server for Reflector 2.0
 *
 * Serves the React UI and provides JSON API endpoints.
 */

package web

import (
	"embed"
	"encoding/json"
	"fmt"
	"io/fs"
	"net/http"
	"time"

	"github.com/krisarmstrong/reflector-native/pkg/dataplane"
)

//go:embed dist/*
var reactApp embed.FS

// Server holds the web server state
type Server struct {
	dp        *dataplane.Dataplane
	port      int
	startTime time.Time
	mux       *http.ServeMux
}

// StatsResponse is the JSON structure for stats API
type StatsResponse struct {
	Timestamp        string  `json:"timestamp"`
	Uptime           float64 `json:"uptime_seconds"`
	Interface        string  `json:"interface"`
	Running          bool    `json:"running"`
	PacketsReceived  uint64  `json:"packets_received"`
	PacketsReflected uint64  `json:"packets_reflected"`
	BytesReceived    uint64  `json:"bytes_received"`
	BytesReflected   uint64  `json:"bytes_reflected"`
	TxErrors         uint64  `json:"tx_errors"`
	RxInvalid        uint64  `json:"rx_invalid"`
	RatePackets      float64 `json:"rate_pps"`
	RateMbps         float64 `json:"rate_mbps"`
	Signatures       struct {
		ProbeOT uint64 `json:"probeot"`
		DataOT  uint64 `json:"dataot"`
		Latency uint64 `json:"latency"`
	} `json:"signatures"`
	Latency struct {
		MinUs   float64 `json:"min_us"`
		AvgUs   float64 `json:"avg_us"`
		MaxUs   float64 `json:"max_us"`
		Count   uint64  `json:"count"`
		Enabled bool    `json:"enabled"`
	} `json:"latency"`
}

// ConfigResponse is the JSON structure for config API
type ConfigResponse struct {
	Interface string `json:"interface"`
	Filtering struct {
		Port      uint16 `json:"port"`
		FilterOUI bool   `json:"filter_oui"`
		OUI       string `json:"oui"`
	} `json:"filtering"`
	Reflection struct {
		Mode string `json:"mode"`
	} `json:"reflection"`
	Platform struct {
		Type string `json:"type"`
	} `json:"platform"`
}

// New creates a new web server
func New(dp *dataplane.Dataplane, port int) *Server {
	s := &Server{
		dp:        dp,
		port:      port,
		startTime: time.Now(),
		mux:       http.NewServeMux(),
	}

	// API routes
	s.mux.HandleFunc("/api/stats", s.handleStats)
	s.mux.HandleFunc("/api/config", s.handleConfig)
	s.mux.HandleFunc("/api/health", s.handleHealth)

	// Serve embedded React app
	distFS, err := fs.Sub(reactApp, "dist")
	if err != nil {
		// If no dist folder, serve a simple status page
		s.mux.HandleFunc("/", s.handleFallback)
	} else {
		s.mux.Handle("/", http.FileServer(http.FS(distFS)))
	}

	return s
}

// Start begins serving HTTP requests
func (s *Server) Start() error {
	addr := fmt.Sprintf(":%d", s.port)
	return http.ListenAndServe(addr, s.mux)
}

// handleStats returns current statistics as JSON
func (s *Server) handleStats(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	stats := s.dp.GetStats()
	elapsed := time.Since(s.startTime).Seconds()

	pps := float64(0)
	mbps := float64(0)
	if elapsed > 0 {
		pps = float64(stats.PacketsReflected) / elapsed
		mbps = float64(stats.BytesReflected) * 8.0 / (elapsed * 1000000.0)
	}

	resp := StatsResponse{
		Timestamp:        time.Now().UTC().Format(time.RFC3339),
		Uptime:           elapsed,
		Interface:        s.dp.Interface(),
		Running:          s.dp.IsRunning(),
		PacketsReceived:  stats.PacketsReceived,
		PacketsReflected: stats.PacketsReflected,
		BytesReceived:    stats.BytesReceived,
		BytesReflected:   stats.BytesReflected,
		TxErrors:         stats.TxErrors,
		RxInvalid:        stats.RxInvalid,
		RatePackets:      pps,
		RateMbps:         mbps,
	}
	resp.Signatures.ProbeOT = stats.SigProbeOT
	resp.Signatures.DataOT = stats.SigDataOT
	resp.Signatures.Latency = stats.SigLatency
	resp.Latency.MinUs = stats.LatencyMin
	resp.Latency.AvgUs = stats.LatencyAvg
	resp.Latency.MaxUs = stats.LatencyMax
	resp.Latency.Count = stats.LatencyCount
	resp.Latency.Enabled = stats.LatencyCount > 0

	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	json.NewEncoder(w).Encode(resp)
}

// handleConfig returns current configuration
func (s *Server) handleConfig(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	cfg := s.dp.Config()
	resp := ConfigResponse{
		Interface: cfg.Interface,
	}
	resp.Filtering.Port = cfg.Filtering.Port
	resp.Filtering.FilterOUI = cfg.Filtering.FilterOUI
	resp.Filtering.OUI = cfg.Filtering.OUI
	resp.Reflection.Mode = cfg.Reflection.Mode

	if cfg.Platform.UseDPDK {
		resp.Platform.Type = "DPDK"
	} else {
		resp.Platform.Type = "AF_XDP"
	}

	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	json.NewEncoder(w).Encode(resp)
}

// handleHealth returns a simple health check
func (s *Server) handleHealth(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"status":  "ok",
		"running": s.dp.IsRunning(),
		"uptime":  time.Since(s.startTime).Seconds(),
	})
}

// handleFallback serves a simple HTML page when React app isn't built
func (s *Server) handleFallback(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}

	stats := s.dp.GetStats()
	html := fmt.Sprintf(`<!DOCTYPE html>
<html>
<head>
    <title>Reflector 2.0</title>
    <meta http-equiv="refresh" content="5">
    <style>
        body { font-family: monospace; background: #1a1a2e; color: #eee; padding: 20px; }
        .stat { margin: 5px 0; }
        .label { color: #00d4ff; }
        h1 { color: #00ff88; }
    </style>
</head>
<body>
    <h1>Reflector 2.0</h1>
    <p>Interface: %s | Status: %s</p>
    <div class="stat"><span class="label">RX Packets:</span> %d</div>
    <div class="stat"><span class="label">TX Packets:</span> %d</div>
    <div class="stat"><span class="label">RX Bytes:</span> %d</div>
    <div class="stat"><span class="label">TX Bytes:</span> %d</div>
    <hr>
    <p><small>Auto-refresh every 5 seconds. Build React UI for full dashboard.</small></p>
</body>
</html>`,
		s.dp.Interface(),
		func() string {
			if s.dp.IsRunning() {
				return "Running"
			}
			return "Stopped"
		}(),
		stats.PacketsReceived,
		stats.PacketsReflected,
		stats.BytesReceived,
		stats.BytesReflected,
	)

	w.Header().Set("Content-Type", "text/html")
	w.Write([]byte(html))
}
