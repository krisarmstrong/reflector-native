import React, { useState, useEffect } from 'react'

const styles = {
  container: {
    maxWidth: '1200px',
    margin: '0 auto',
    padding: '20px',
  },
  header: {
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: '20px',
    padding: '15px 20px',
    background: 'rgba(255,255,255,0.05)',
    borderRadius: '10px',
  },
  title: {
    color: '#00ff88',
    fontSize: '24px',
    fontWeight: 'bold',
  },
  status: {
    display: 'flex',
    alignItems: 'center',
    gap: '10px',
  },
  statusDot: {
    width: '12px',
    height: '12px',
    borderRadius: '50%',
    animation: 'pulse 2s infinite',
  },
  grid: {
    display: 'grid',
    gridTemplateColumns: 'repeat(auto-fit, minmax(300px, 1fr))',
    gap: '20px',
  },
  card: {
    background: 'rgba(255,255,255,0.05)',
    borderRadius: '10px',
    padding: '20px',
    border: '1px solid rgba(255,255,255,0.1)',
  },
  cardTitle: {
    color: '#00d4ff',
    fontSize: '14px',
    textTransform: 'uppercase',
    marginBottom: '15px',
    letterSpacing: '1px',
  },
  statRow: {
    display: 'flex',
    justifyContent: 'space-between',
    marginBottom: '10px',
  },
  statLabel: {
    color: '#888',
  },
  statValue: {
    color: '#fff',
    fontWeight: 'bold',
  },
  bigNumber: {
    fontSize: '36px',
    fontWeight: 'bold',
    color: '#00ff88',
  },
  smallText: {
    fontSize: '12px',
    color: '#666',
  },
}

function formatNumber(n) {
  if (n >= 1000000000) return (n / 1000000000).toFixed(2) + 'B'
  if (n >= 1000000) return (n / 1000000).toFixed(2) + 'M'
  if (n >= 1000) return (n / 1000).toFixed(2) + 'K'
  return n.toString()
}

function formatBytes(n) {
  if (n >= 1099511627776) return (n / 1099511627776).toFixed(2) + ' TB'
  if (n >= 1073741824) return (n / 1073741824).toFixed(2) + ' GB'
  if (n >= 1048576) return (n / 1048576).toFixed(2) + ' MB'
  if (n >= 1024) return (n / 1024).toFixed(2) + ' KB'
  return n + ' B'
}

function formatUptime(seconds) {
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  const s = Math.floor(seconds % 60)
  if (h > 0) return `${h}h ${m}m ${s}s`
  if (m > 0) return `${m}m ${s}s`
  return `${s}s`
}

function App() {
  const [stats, setStats] = useState(null)
  const [config, setConfig] = useState(null)
  const [error, setError] = useState(null)

  useEffect(() => {
    const fetchData = async () => {
      try {
        const [statsRes, configRes] = await Promise.all([
          fetch('/api/stats'),
          fetch('/api/config')
        ])
        if (statsRes.ok) setStats(await statsRes.json())
        if (configRes.ok) setConfig(await configRes.json())
        setError(null)
      } catch (err) {
        setError('Connection lost')
      }
    }

    fetchData()
    const interval = setInterval(fetchData, 1000)
    return () => clearInterval(interval)
  }, [])

  if (error) {
    return (
      <div style={styles.container}>
        <div style={{ ...styles.card, textAlign: 'center', padding: '50px' }}>
          <h2 style={{ color: '#ff4444' }}>Connection Lost</h2>
          <p style={{ color: '#888', marginTop: '10px' }}>
            Unable to reach reflector. Is it running?
          </p>
        </div>
      </div>
    )
  }

  if (!stats) {
    return (
      <div style={styles.container}>
        <div style={{ ...styles.card, textAlign: 'center', padding: '50px' }}>
          <h2>Loading...</h2>
        </div>
      </div>
    )
  }

  return (
    <div style={styles.container}>
      <style>{`
        @keyframes pulse {
          0%, 100% { opacity: 1; }
          50% { opacity: 0.5; }
        }
      `}</style>

      {/* Header */}
      <div style={styles.header}>
        <div>
          <span style={styles.title}>Reflector 2.0</span>
          <span style={{ marginLeft: '15px', color: '#666' }}>
            {config?.interface || stats.interface}
          </span>
        </div>
        <div style={styles.status}>
          <div style={{
            ...styles.statusDot,
            background: stats.running ? '#00ff88' : '#ff4444'
          }} />
          <span>{stats.running ? 'Running' : 'Stopped'}</span>
          <span style={{ color: '#666', marginLeft: '10px' }}>
            Uptime: {formatUptime(stats.uptime_seconds)}
          </span>
        </div>
      </div>

      {/* Stats Grid */}
      <div style={styles.grid}>
        {/* Throughput Card */}
        <div style={styles.card}>
          <div style={styles.cardTitle}>Throughput</div>
          <div style={styles.bigNumber}>{stats.rate_mbps.toFixed(2)} Mbps</div>
          <div style={styles.smallText}>{formatNumber(stats.rate_pps)} packets/sec</div>
        </div>

        {/* Packets Card */}
        <div style={styles.card}>
          <div style={styles.cardTitle}>Packets</div>
          <div style={styles.statRow}>
            <span style={styles.statLabel}>Received</span>
            <span style={styles.statValue}>{formatNumber(stats.packets_received)}</span>
          </div>
          <div style={styles.statRow}>
            <span style={styles.statLabel}>Reflected</span>
            <span style={styles.statValue}>{formatNumber(stats.packets_reflected)}</span>
          </div>
          <div style={styles.statRow}>
            <span style={styles.statLabel}>Errors</span>
            <span style={{ ...styles.statValue, color: stats.tx_errors > 0 ? '#ff4444' : '#00ff88' }}>
              {stats.tx_errors}
            </span>
          </div>
        </div>

        {/* Bytes Card */}
        <div style={styles.card}>
          <div style={styles.cardTitle}>Data Transfer</div>
          <div style={styles.statRow}>
            <span style={styles.statLabel}>RX</span>
            <span style={styles.statValue}>{formatBytes(stats.bytes_received)}</span>
          </div>
          <div style={styles.statRow}>
            <span style={styles.statLabel}>TX</span>
            <span style={styles.statValue}>{formatBytes(stats.bytes_reflected)}</span>
          </div>
        </div>

        {/* Signatures Card */}
        <div style={styles.card}>
          <div style={styles.cardTitle}>ITO Signatures</div>
          <div style={styles.statRow}>
            <span style={styles.statLabel}>PROBEOT</span>
            <span style={styles.statValue}>{formatNumber(stats.signatures.probeot)}</span>
          </div>
          <div style={styles.statRow}>
            <span style={styles.statLabel}>DATA:OT</span>
            <span style={styles.statValue}>{formatNumber(stats.signatures.dataot)}</span>
          </div>
          <div style={styles.statRow}>
            <span style={styles.statLabel}>LATENCY</span>
            <span style={styles.statValue}>{formatNumber(stats.signatures.latency)}</span>
          </div>
        </div>

        {/* Latency Card */}
        <div style={styles.card}>
          <div style={styles.cardTitle}>Latency</div>
          {stats.latency.enabled ? (
            <>
              <div style={styles.statRow}>
                <span style={styles.statLabel}>Min</span>
                <span style={styles.statValue}>{stats.latency.min_us.toFixed(2)} µs</span>
              </div>
              <div style={styles.statRow}>
                <span style={styles.statLabel}>Avg</span>
                <span style={styles.statValue}>{stats.latency.avg_us.toFixed(2)} µs</span>
              </div>
              <div style={styles.statRow}>
                <span style={styles.statLabel}>Max</span>
                <span style={styles.statValue}>{stats.latency.max_us.toFixed(2)} µs</span>
              </div>
            </>
          ) : (
            <div style={{ color: '#666', fontStyle: 'italic' }}>
              Not enabled (use --latency)
            </div>
          )}
        </div>

        {/* Config Card */}
        {config && (
          <div style={styles.card}>
            <div style={styles.cardTitle}>Configuration</div>
            <div style={styles.statRow}>
              <span style={styles.statLabel}>Platform</span>
              <span style={styles.statValue}>{config.platform.type}</span>
            </div>
            <div style={styles.statRow}>
              <span style={styles.statLabel}>Mode</span>
              <span style={styles.statValue}>{config.reflection.mode}</span>
            </div>
            <div style={styles.statRow}>
              <span style={styles.statLabel}>Port Filter</span>
              <span style={styles.statValue}>{config.filtering.port}</span>
            </div>
            <div style={styles.statRow}>
              <span style={styles.statLabel}>OUI Filter</span>
              <span style={styles.statValue}>
                {config.filtering.filter_oui ? config.filtering.oui : 'Disabled'}
              </span>
            </div>
          </div>
        )}
      </div>
    </div>
  )
}

export default App
