/*
 * config.go - YAML configuration support for Reflector 2.0
 */

package config

import (
	"fmt"
	"os"
	"regexp"

	"gopkg.in/yaml.v3"
)

// Config holds all reflector configuration
type Config struct {
	Interface  string         `yaml:"interface"`
	Verbose    bool           `yaml:"verbose"`
	WebUI      WebUIConfig    `yaml:"web_ui"`
	TUI        TUIConfig      `yaml:"tui"`
	Filtering  FilterConfig   `yaml:"filtering"`
	Reflection ReflectConfig  `yaml:"reflection"`
	Platform   PlatformConfig `yaml:"platform"`
	Stats      StatsConfig    `yaml:"stats"`
}

// WebUIConfig holds web UI settings
type WebUIConfig struct {
	Enabled bool `yaml:"enabled"`
	Port    int  `yaml:"port"`
}

// TUIConfig holds TUI settings
type TUIConfig struct {
	Enabled bool `yaml:"enabled"`
}

// FilterConfig holds packet filtering settings
type FilterConfig struct {
	Port      uint16 `yaml:"port"`       // ITO UDP port (0 = any)
	FilterOUI bool   `yaml:"filter_oui"` // Enable OUI filtering
	OUI       string `yaml:"oui"`        // Source OUI (XX:XX:XX)
}

// ReflectConfig holds reflection mode settings
type ReflectConfig struct {
	Mode string `yaml:"mode"` // mac, mac-ip, or all
}

// PlatformConfig holds platform-specific settings
type PlatformConfig struct {
	UseDPDK  bool   `yaml:"use_dpdk"`
	DPDKArgs string `yaml:"dpdk_args"`
}

// StatsConfig holds statistics settings
type StatsConfig struct {
	Format   string `yaml:"format"`   // text, json, csv
	Interval int    `yaml:"interval"` // seconds
}

// LoadFile loads configuration from a YAML file
func LoadFile(path string) (*Config, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read config file: %w", err)
	}

	cfg := &Config{}
	if err := yaml.Unmarshal(data, cfg); err != nil {
		return nil, fmt.Errorf("failed to parse config file: %w", err)
	}

	// Apply defaults
	cfg.applyDefaults()

	return cfg, nil
}

// applyDefaults sets default values for unspecified fields
func (c *Config) applyDefaults() {
	if c.WebUI.Port == 0 {
		c.WebUI.Port = 8080
	}
	if c.Filtering.Port == 0 {
		c.Filtering.Port = 3842
	}
	if c.Filtering.OUI == "" {
		c.Filtering.OUI = "00:c0:17"
	}
	if c.Reflection.Mode == "" {
		c.Reflection.Mode = "all"
	}
	if c.Stats.Format == "" {
		c.Stats.Format = "text"
	}
	if c.Stats.Interval == 0 {
		c.Stats.Interval = 10
	}
	// TUI enabled by default
	if !c.TUI.Enabled && c.Interface != "" {
		c.TUI.Enabled = true
	}
}

// Validate checks the configuration for errors
func (c *Config) Validate() error {
	if c.Interface == "" {
		return fmt.Errorf("interface is required")
	}

	// Validate OUI format (XX:XX:XX)
	ouiPattern := regexp.MustCompile(`^[0-9a-fA-F]{2}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}$`)
	if c.Filtering.FilterOUI && !ouiPattern.MatchString(c.Filtering.OUI) {
		return fmt.Errorf("invalid OUI format: %s (expected XX:XX:XX)", c.Filtering.OUI)
	}

	// Validate reflection mode
	switch c.Reflection.Mode {
	case "mac", "mac-ip", "all":
		// Valid
	default:
		return fmt.Errorf("invalid reflection mode: %s (expected mac, mac-ip, or all)", c.Reflection.Mode)
	}

	// Validate stats format
	switch c.Stats.Format {
	case "text", "json", "csv":
		// Valid
	default:
		return fmt.Errorf("invalid stats format: %s (expected text, json, or csv)", c.Stats.Format)
	}

	if c.WebUI.Port < 1 || c.WebUI.Port > 65535 {
		return fmt.Errorf("invalid web port: %d", c.WebUI.Port)
	}

	return nil
}

// ParseOUI parses the OUI string into bytes
func (c *Config) ParseOUI() ([3]byte, error) {
	var oui [3]byte
	_, err := fmt.Sscanf(c.Filtering.OUI, "%02x:%02x:%02x", &oui[0], &oui[1], &oui[2])
	if err != nil {
		return oui, fmt.Errorf("failed to parse OUI: %w", err)
	}
	return oui, nil
}

// ReflectModeInt converts the mode string to an int for C
func (c *Config) ReflectModeInt() int {
	switch c.Reflection.Mode {
	case "mac":
		return 0
	case "mac-ip":
		return 1
	case "all":
		return 2
	default:
		return 2
	}
}
