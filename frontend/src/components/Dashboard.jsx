import React, { useState, useEffect, useRef } from 'react';
import { Activity, ShieldCheck, ShieldAlert, ArrowUpRight } from 'lucide-react';
import { AreaChart, Area, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer } from 'recharts';

export default function Dashboard() {
  const [stats, setStats] = useState({
    total_requests: 0,
    blocked_requests: 0,
    allowed_requests: 0,
    system_stats: {}
  });
  
  const [logs, setLogs] = useState([]);
  const [chartData, setChartData] = useState([]);
  const [loading, setLoading] = useState(true);
  const [wsStatus, setWsStatus] = useState('connecting');
  const ws = useRef(null);

  useEffect(() => {
    // 1. Initial HTTP Fetch for baseline data sequentially 
    const fetchInitialData = async () => {
      try {
        const [statsRes, logsRes, chartRes] = await Promise.all([
          fetch(`/api/traffic/stats`),
          fetch(`/api/traffic/logs?limit=50`),
          fetch(`/api/traffic/chart`)
        ]);
        
        if (statsRes.ok) setStats(await statsRes.json());
        if (logsRes.ok) setLogs(await logsRes.json());
        if (chartRes.ok) setChartData(await chartRes.json());
        
      } catch (error) {
        console.error("Failed to fetch initial baseline data:", error);
      } finally {
        setLoading(false);
      }
    };

    fetchInitialData();

    // 2. Setup WebSocket dynamically mapping host network
    const connectWebSocket = () => {
      setWsStatus('connecting');
      const wsProto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      ws.current = new WebSocket(`${wsProto}//${window.location.host}/api/traffic/live`);
      
      ws.current.onopen = () => setWsStatus('active');

      ws.current.onmessage = (event) => {
        const payload = JSON.parse(event.data);
        if (payload.type === 'new_log') {
            setLogs(prevLogs => {
                const updatedLogs = [payload.data, ...prevLogs];
                return updatedLogs.slice(0, 50);
            });
        } else if (payload.type === 'update_stats') {
            setStats(payload.data);
            // Refresh chart slightly delayed to absorb backend aggregation hooks
            setTimeout(async () => {
                const res = await fetch(`/api/traffic/chart`);
                if(res.ok) setChartData(await res.json());
            }, 500); 
        }
      };

      ws.current.onclose = () => {
          setWsStatus('disconnected');
          setTimeout(connectWebSocket, 3000);
      };
      
      ws.current.onerror = (err) => {
          ws.current.close();
      };
    };

    connectWebSocket();

    return () => {
      if (ws.current) ws.current.close();
    };
  }, []);

  return (
    <div className="dashboard-wrapper">
      <div className="header">
        <h1>Network Traffic Analytics</h1>
        <p>Real-time gateway monitoring and threat detection.</p>
        <span className="stat-indicator" style={{marginTop: '8px', color: wsStatus === 'active' ? 'var(--success)' : 'var(--warning)'}}>
           {wsStatus === 'active' ? '● Live Connection Active' : '○ Synchronizing Stream...'}
        </span>
      </div>

      <div className="stats-grid">
        <div className="stat-card glass-panel">
          <div className="stat-header">
            <Activity size={20} color="var(--accent-blue)" />
            Total Requests
          </div>
          <div className="stat-value">{stats.total_requests.toLocaleString()}</div>
          <div className="stat-indicator">
            <ArrowUpRight size={16} /> Live Data
          </div>
        </div>
        
        <div className="stat-card glass-panel">
          <div className="stat-header">
            <ShieldCheck size={20} color="var(--success)" />
            Allowed Traffic
          </div>
          <div className="stat-value">{stats.allowed_requests.toLocaleString()}</div>
        </div>

        <div className="stat-card glass-panel">
          <div className="stat-header">
            <ShieldAlert size={20} color="var(--danger)" />
            Threats Blocked
          </div>
          <div className="stat-value">{stats.blocked_requests.toLocaleString()}</div>
          <div className="stat-indicator danger">
            <ArrowUpRight size={16} /> Active monitoring
          </div>
        </div>
      </div>

      {/* 24 HOUR AGGREGATION CHART */}
      <div className="glass-panel" style={{ padding: '24px', marginBottom: '24px' }}>
         <h3 style={{ marginBottom: '16px', color: 'var(--text-main)', display: 'flex', alignItems: 'center', gap: '8px' }}>
           24-Hour Traffic Aggregation Filter
         </h3>
         <div style={{ width: '100%', height: 300 }}>
           <ResponsiveContainer>
              <AreaChart data={chartData} margin={{ top: 10, right: 30, left: 0, bottom: 0 }}>
                  <defs>
                    <linearGradient id="colorAllowed" x1="0" y1="0" x2="0" y2="1">
                      <stop offset="5%" stopColor="var(--success)" stopOpacity={0.3}/>
                      <stop offset="95%" stopColor="var(--success)" stopOpacity={0}/>
                    </linearGradient>
                    <linearGradient id="colorBlocked" x1="0" y1="0" x2="0" y2="1">
                      <stop offset="5%" stopColor="var(--danger)" stopOpacity={0.3}/>
                      <stop offset="95%" stopColor="var(--danger)" stopOpacity={0}/>
                    </linearGradient>
                  </defs>
                  <CartesianGrid strokeDasharray="3 3" stroke="rgba(255,255,255,0.05)" />
                  <XAxis dataKey="time" stroke="var(--text-muted)" tick={{ fill: 'var(--text-muted)' }} />
                  <YAxis stroke="var(--text-muted)" tick={{ fill: 'var(--text-muted)' }} />
                  <Tooltip contentStyle={{ backgroundColor: '#0f1117', borderColor: 'rgba(255,255,255,0.1)' }} />
                  <Area type="monotone" dataKey="allowed" stroke="var(--success)" fillOpacity={1} fill="url(#colorAllowed)" />
                  <Area type="monotone" dataKey="blocked" stroke="var(--danger)" fillOpacity={1} fill="url(#colorBlocked)" />
              </AreaChart>
           </ResponsiveContainer>
         </div>
      </div>

      <div className="data-table-container glass-panel">
        <table className="data-table">
          <thead>
            <tr>
              <th>Time</th>
              <th>Domain</th>
              <th>Client IP</th>
              <th>Status</th>
            </tr>
          </thead>
          <tbody>
            {logs.length === 0 && !loading ? (
              <tr>
                <td colSpan="4" style={{ textAlign: 'center', padding: '32px', color: 'var(--text-muted)'}}>
                  No traffic data available. Waiting for proxy requests...
                </td>
              </tr>
            ) : null}
            {logs.map(log => {
              const d = new Date(log.timestamp.endsWith('Z') ? log.timestamp : log.timestamp + 'Z');
              return (
                <tr key={log.id}>
                  <td>{d.toLocaleTimeString()}</td>
                  <td className="domain-cell">{log.domain}</td>
                  <td>{log.client_ip || 'N/A'}</td>
                  <td>
                    <span className={`badge ${log.blocked ? 'danger' : 'success'}`}>
                      {log.blocked ? 'Blocked' : 'Allowed'}
                    </span>
                  </td>
                </tr>
              )
            })}
          </tbody>
        </table>
      </div>
    </div>
  );
}
