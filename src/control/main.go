package main

import (
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"strings"
	"syscall"
	"time"
)

type Stats struct {
	PacketsReceived  uint64
	PacketsReflected uint64
	BytesReceived    uint64
	BytesReflected   uint64
	PPS              float64
	Mbps             float64
}

type Reflector struct {
	ifname  string
	cmd     *exec.Cmd
	running bool
}

func NewReflector(ifname string) *Reflector {
	return &Reflector{
		ifname: ifname,
	}
}

func (r *Reflector) Start() error {
	binary := "./reflector-linux"
	if _, err := os.Stat("./reflector-macos"); err == nil {
		binary = "./reflector-macos"
	}

	r.cmd = exec.Command(binary, r.ifname)
	r.cmd.Stdout = os.Stdout
	r.cmd.Stderr = os.Stderr

	if err := r.cmd.Start(); err != nil {
		return fmt.Errorf("failed to start reflector: %w", err)
	}

	r.running = true
	return nil
}

func (r *Reflector) Stop() error {
	if !r.running || r.cmd == nil {
		return nil
	}

	if err := r.cmd.Process.Signal(syscall.SIGTERM); err != nil {
		return err
	}

	r.cmd.Wait()
	r.running = false
	return nil
}

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintf(os.Stderr, "Usage: %s <interface>\n", os.Args[0])
		os.Exit(1)
	}

	ifname := os.Args[1]

	fmt.Printf("Network Reflector Control\n")
	fmt.Printf("Interface: %s\n\n", ifname)

	r := NewReflector(ifname)

	// Handle signals
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		<-sigChan
		fmt.Println("\nShutting down...")
		r.Stop()
		os.Exit(0)
	}()

	// Start reflector
	if err := r.Start(); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}

	fmt.Println("Reflector started. Press Ctrl-C to stop.")

	// Keep running
	for r.running {
		time.Sleep(1 * time.Second)
	}
}
