/*
 * tui.go - Terminal UI for Reflector 2.0
 *
 * Real-time dashboard using tview/tcell for terminal rendering.
 */

package tui

import (
	"fmt"
	"sync"
	"time"

	"github.com/gdamore/tcell/v2"
	"github.com/krisarmstrong/reflector-native/pkg/dataplane"
	"github.com/rivo/tview"
)

// App holds the TUI application state
type App struct {
	dp        *dataplane.Dataplane
	app       *tview.Application
	statsView *tview.TextView
	sigView   *tview.TextView
	latView   *tview.TextView
	helpView  *tview.TextView
	startTime time.Time
	stopChan  chan struct{}
	stopOnce  sync.Once // Prevent double-close panic
}

// New creates a new TUI application
func New(dp *dataplane.Dataplane) *App {
	return &App{
		dp:        dp,
		app:       tview.NewApplication(),
		startTime: time.Now(),
		stopChan:  make(chan struct{}),
	}
}

// Run starts the TUI
func (a *App) Run() error {
	// Create main stats panel
	a.statsView = tview.NewTextView().
		SetDynamicColors(true).
		SetTextAlign(tview.AlignLeft)
	a.statsView.SetBorder(true).SetTitle(" Statistics ")

	// Create signature breakdown panel
	a.sigView = tview.NewTextView().
		SetDynamicColors(true).
		SetTextAlign(tview.AlignLeft)
	a.sigView.SetBorder(true).SetTitle(" Signatures ")

	// Create latency panel
	a.latView = tview.NewTextView().
		SetDynamicColors(true).
		SetTextAlign(tview.AlignLeft)
	a.latView.SetBorder(true).SetTitle(" Latency ")

	// Create help panel
	a.helpView = tview.NewTextView().
		SetDynamicColors(true).
		SetTextAlign(tview.AlignCenter).
		SetText("[yellow]q[white] quit  [yellow]r[white] reset  [yellow]p[white] pause")
	a.helpView.SetBorder(false)

	// Create header
	header := tview.NewTextView().
		SetDynamicColors(true).
		SetTextAlign(tview.AlignCenter).
		SetText(fmt.Sprintf("[green]Reflector 2.0[white] | Interface: [cyan]%s[white] | Status: [green]● RUNNING",
			a.dp.Interface()))

	// Layout
	statsRow := tview.NewFlex().
		AddItem(a.statsView, 0, 2, false).
		AddItem(a.sigView, 0, 1, false).
		AddItem(a.latView, 0, 1, false)

	mainFlex := tview.NewFlex().SetDirection(tview.FlexRow).
		AddItem(header, 1, 0, false).
		AddItem(statsRow, 0, 1, false).
		AddItem(a.helpView, 1, 0, false)

	// Key bindings
	a.app.SetInputCapture(func(event *tcell.EventKey) *tcell.EventKey {
		switch event.Rune() {
		case 'q', 'Q':
			a.Stop()
			return nil
		case 'r', 'R':
			// Reset stats - would need to add this to dataplane
			return nil
		case 'p', 'P':
			// Pause - would toggle stats updates
			return nil
		}
		return event
	})

	// Start stats update goroutine
	go a.updateLoop()

	// Run the app
	return a.app.SetRoot(mainFlex, true).EnableMouse(false).Run()
}

// Stop signals the TUI to exit
func (a *App) Stop() {
	a.stopOnce.Do(func() {
		close(a.stopChan)
		a.app.Stop()
	})
}

// updateLoop periodically refreshes the display
func (a *App) updateLoop() {
	ticker := time.NewTicker(500 * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-a.stopChan:
			return
		case <-ticker.C:
			a.updateStats()
		}
	}
}

// updateStats refreshes all stat panels
func (a *App) updateStats() {
	stats := a.dp.GetStats()
	elapsed := time.Since(a.startTime).Seconds()

	// Calculate rates
	pps := float64(0)
	mbps := float64(0)
	if elapsed > 0 {
		pps = float64(stats.PacketsReflected) / elapsed
		mbps = float64(stats.BytesReflected) * 8.0 / (elapsed * 1000000.0)
	}

	// Main stats
	statsText := fmt.Sprintf(
		"[yellow]RX Packets:[white]  %s\n"+
			"[yellow]TX Packets:[white]  %s\n"+
			"[yellow]RX Bytes:[white]    %s\n"+
			"[yellow]TX Bytes:[white]    %s\n"+
			"\n"+
			"[yellow]Rate:[white]        %.0f pps\n"+
			"[yellow]Throughput:[white]  %.2f Mbps\n"+
			"\n"+
			"[yellow]Uptime:[white]      %s",
		formatNumber(stats.PacketsReceived),
		formatNumber(stats.PacketsReflected),
		formatBytes(stats.BytesReceived),
		formatBytes(stats.BytesReflected),
		pps, mbps,
		formatDuration(time.Since(a.startTime)),
	)

	// Signature breakdown
	sigText := fmt.Sprintf(
		"[cyan]PROBEOT:[white]  %s\n"+
			"[cyan]DATA:OT:[white]  %s\n"+
			"[cyan]LATENCY:[white]  %s",
		formatNumber(stats.SigProbeOT),
		formatNumber(stats.SigDataOT),
		formatNumber(stats.SigLatency),
	)

	// Latency stats
	latText := ""
	if stats.LatencyCount > 0 {
		latText = fmt.Sprintf(
			"[magenta]Min:[white]   %.2f µs\n"+
				"[magenta]Avg:[white]   %.2f µs\n"+
				"[magenta]Max:[white]   %.2f µs\n"+
				"[magenta]Count:[white] %s",
			stats.LatencyMin,
			stats.LatencyAvg,
			stats.LatencyMax,
			formatNumber(stats.LatencyCount),
		)
	} else {
		latText = "[gray]No latency data\n(use --latency)"
	}

	// Update views on main thread
	a.app.QueueUpdateDraw(func() {
		a.statsView.SetText(statsText)
		a.sigView.SetText(sigText)
		a.latView.SetText(latText)
	})
}

// Helper functions

func formatNumber(n uint64) string {
	if n >= 1000000000 {
		return fmt.Sprintf("%.2fB", float64(n)/1000000000)
	}
	if n >= 1000000 {
		return fmt.Sprintf("%.2fM", float64(n)/1000000)
	}
	if n >= 1000 {
		return fmt.Sprintf("%.2fK", float64(n)/1000)
	}
	return fmt.Sprintf("%d", n)
}

func formatBytes(n uint64) string {
	if n >= 1099511627776 {
		return fmt.Sprintf("%.2f TB", float64(n)/1099511627776)
	}
	if n >= 1073741824 {
		return fmt.Sprintf("%.2f GB", float64(n)/1073741824)
	}
	if n >= 1048576 {
		return fmt.Sprintf("%.2f MB", float64(n)/1048576)
	}
	if n >= 1024 {
		return fmt.Sprintf("%.2f KB", float64(n)/1024)
	}
	return fmt.Sprintf("%d B", n)
}

func formatDuration(d time.Duration) string {
	hours := int(d.Hours())
	minutes := int(d.Minutes()) % 60
	seconds := int(d.Seconds()) % 60

	if hours > 0 {
		return fmt.Sprintf("%dh %dm %ds", hours, minutes, seconds)
	}
	if minutes > 0 {
		return fmt.Sprintf("%dm %ds", minutes, seconds)
	}
	return fmt.Sprintf("%ds", seconds)
}
