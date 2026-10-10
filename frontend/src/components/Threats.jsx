import React, { useState, useEffect } from 'react';
import { motion, AnimatePresence } from 'framer-motion';
import { ShieldAlert, RefreshCw, CheckCircle, Search, AlertOctagon, Plus, Activity, AlertTriangle, Link as LinkIcon } from 'lucide-react';

export default function Threats() {
  const [stats, setStats] = useState({ total_domains: 0, sources: {}, last_updated: null, last_added_count: 0 });
  const [syncStatus, setSyncStatus] = useState({ status: 'idle', message: 'Ready.' });
  const [recent, setRecent] = useState([]);
  
  const [query, setQuery] = useState('');
  const [queryResult, setQueryResult] = useState(null);
  const [queryError, setQueryError] = useState(null);
  
  const [customDomain, setCustomDomain] = useState('');
  const [customStatus, setCustomStatus] = useState(null);

  const fetchStats = async () => {
    try {
      const res = await fetch('/api/threats/stats');
      if (res.ok) setStats(await res.json());
      const res2 = await fetch('/api/threats/recent?limit=5');
      if (res2.ok) setRecent(await res2.json());
    } catch (e) {
      console.error(e);
    }
  };

  const fetchStatus = async () => {
    try {
      const res = await fetch('/api/threats/status');
      if (res.ok) {
        const data = await res.json();
        setSyncStatus(data);
        if (data.status === 'idle') {
           fetchStats();
        }
      }
    } catch (e) {
      console.error(e);
    }
  };

  useEffect(() => {
    fetchStats();
    fetchStatus();
    const interval = setInterval(fetchStatus, 2000);
    return () => clearInterval(interval);
  }, []);

  const triggerSync = async () => {
    try {
      await fetch('/api/threats/refresh', { method: 'POST' });
      fetchStatus();
    } catch (e) {
      console.error(e);
    }
  };

  const checkDomain = async (e) => {
    e.preventDefault();
    setQueryResult(null);
    setQueryError(null);
    if (!query) return;
    try {
      const res = await fetch(`/api/threats/check?domain=${encodeURIComponent(query)}`);
      const data = await res.json();
      if (!res.ok) {
        setQueryError(data.detail);
      } else {
        setQueryResult(data);
      }
    } catch (e) {
      console.error(e);
    }
  };

  const addCustomDomain = async (e) => {
    e.preventDefault();
    setCustomStatus(null);
    if (!customDomain) return;
    try {
      const res = await fetch('/api/threats/add', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ domain: customDomain })
      });
      const data = await res.json();
      if (!res.ok) {
        setCustomStatus({ error: true, msg: data.detail });
      } else {
        setCustomStatus({ error: false, msg: 'Successfully appended to proxy blocklist.' });
        setCustomDomain('');
        fetchStats();
      }
    } catch (e) {
      console.error(e);
    }
  };

  const isSyncing = syncStatus.status === 'updating';

  return (
    <div className="threats-wrapper">
      <div className="header threats-header">
        <h1>Threat Intelligence</h1>
        <p>Live proxy blocklist routing mapped logically to network intercepts.</p>
        <div style={{ marginTop: '10px', display: 'flex', alignItems: 'center', gap: '8px', color: 'var(--success)' }}>
          <CheckCircle size={16} /> <span>Proxy actively using latest blocklist</span>
        </div>
      </div>

      <div className="stats-grid" style={{ gridTemplateColumns: 'repeat(3, 1fr)' }}>
        
        {/* STATS CARD */}
        <motion.div className="stat-card glass-panel" whileHover={{ y: -5 }}>
          <div className="stat-header" title="Domains that will be blocked by proxy in real-time">
            <ShieldAlert size={20} color="var(--danger)" /> Total Blocked Domains
          </div>
          <div className="stat-value" style={{ fontSize: '2.5rem' }}>{stats.total_domains.toLocaleString()}</div>
          
          <div style={{ marginTop: '15px', borderTop: '1px solid rgba(255,255,255,0.1)', paddingTop: '10px' }}>
             <div style={{ fontSize: '0.85rem', color: 'var(--text-muted)' }}>Sources:</div>
             <ul style={{ listStyle: 'none', padding: 0, margin: '5px 0', fontSize: '0.85rem' }}>
               <li>URLHaus: {stats.sources.urlhaus ? stats.sources.urlhaus.toLocaleString() : 0}</li>
               <li>StevenBlack: {stats.sources.steven_black ? stats.sources.steven_black.toLocaleString() : 0}</li>
               <li>Custom/Manual: {stats.sources.custom ? stats.sources.custom.toLocaleString() : 0}</li>
             </ul>
             <div style={{ fontSize: '0.8rem', color: 'var(--text-muted)', marginTop: '10px' }}>
                Last Updated: {stats.last_updated ? new Date(stats.last_updated + 'Z').toLocaleTimeString() : 'Never'}
             </div>
          </div>
        </motion.div>
        
        {/* PIPELINE CARD */}
        <motion.div className="stat-card glass-panel" whileHover={{ y: -5 }}>
           <div className="stat-header">
             <Activity size={20} className={isSyncing ? "spin-icon" : ""} color="var(--accent-blue)"/>
             Pipeline Status
           </div>
           
           <div style={{ marginTop: '15px' }}>
             <strong style={{ color: isSyncing ? 'var(--warning)' : syncStatus.status === 'failed' ? 'var(--danger)' : 'var(--success)' }}>
               {isSyncing ? '🔄 Updating intelligence...' : syncStatus.status === 'failed' ? 'Failed' : 'Active'}
             </strong>
             <div style={{ fontSize: '0.9rem', color: 'var(--text-muted)', marginTop: '10px' }}>{syncStatus.message}</div>
             <div style={{ fontSize: '0.85rem', color: 'var(--text-muted)', marginTop: '15px' }}>
               Domains Added: <span style={{ color: 'var(--success)'}}>+{stats.last_added_count.toLocaleString()}</span>
             </div>
           </div>

           <button 
             className="action-btn download-btn" 
             style={{ marginTop: '20px', width: '100%' }}
             onClick={triggerSync}
             disabled={isSyncing}
           >
             {isSyncing ? "Syncing..." : "Force Intelligence Refresh"}
           </button>
        </motion.div>

        {/* CUSTOM BLOCKLIST CARD */}
        <motion.div className="stat-card glass-panel" whileHover={{ y: -5 }}>
           <div className="stat-header">
             <Plus size={20} color="var(--success)" /> Custom Blocklist
           </div>
           <p style={{ fontSize: '0.85rem', color: 'var(--text-muted)', marginTop: '10px' }}>
             Forcefully add a domain directly to the live proxy block stack.
           </p>

           <form onSubmit={addCustomDomain} style={{ marginTop: '15px' }}>
              <input 
                value={customDomain} 
                onChange={e => setCustomDomain(e.target.value)} 
                placeholder="e.g. tracker.evil.net" 
                style={{ width: '100%', padding: '10px', borderRadius: '6px', backgroundColor: 'rgba(0,0,0,0.3)', color: 'white', border: '1px solid rgba(255,255,255,0.2)' }}
              />
              <button type="submit" className="action-btn download-btn" style={{ marginTop: '10px', width: '100%', backgroundColor: 'var(--danger)' }}>
                Add to Blocklist
              </button>
           </form>
           {customStatus && (
             <div style={{ marginTop: '10px', fontSize: '0.85rem', color: customStatus.error ? 'var(--danger)' : 'var(--success)' }}>
               {customStatus.msg}
             </div>
           )}
        </motion.div>

      </div>

      <div className="threats-split" style={{ marginTop: '25px', display: 'grid', gridTemplateColumns: 'minmax(400px, 1fr) 1fr', gap: '25px' }}>
        
        {/* LEFT: Checker */}
        <div className="glass-panel p-6 threats-check-panel" style={{ height: 'fit-content' }}>
           <h3>Inspect Domain Validation</h3>
           <p className="subtitle mb-4">Validate if arbitrary domains belong to the global intelligence arrays.</p>
           
           <form onSubmit={checkDomain} className="query-box" style={{ display: 'flex', gap: '10px' }}>
             <input value={query} onChange={e => setQuery(e.target.value)} placeholder="e.g. doubleclick.net" style={{ flex: 1, padding: '12px', borderRadius: '8px', border: '1px solid var(--glass-border)', background: 'rgba(0,0,0,0.4)', color: 'white' }} />
             <button type="submit" style={{ padding: '0 20px', background: 'var(--accent-blue)', borderRadius: '8px', cursor: 'pointer', border: 'none', color: 'white' }}><Search size={18} /></button>
           </form>

           <AnimatePresence>
             {queryError && (
                <motion.div className="query-result danger-box" initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0 }} style={{ marginTop: '15px' }}>
                  <AlertTriangle size={24} color="var(--warning)" /> <strong>INVALID FORMAT:</strong> {queryError}
                </motion.div>
             )}
             
             {queryResult && !queryError && (
               <motion.div 
                 className={`query-result ${queryResult.blocked ? 'danger-box' : 'success-box'}`}
                 style={{ marginTop: '15px', display: 'flex', flexDirection: 'column', alignItems: 'flex-start' }}
                 initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0 }}
               >
                 <div style={{ display: 'flex', alignItems: 'center', gap: '10px', fontSize: '1.2rem', marginBottom: '10px' }}>
                   {queryResult.blocked ? <AlertOctagon size={24} color="var(--danger)" /> : <CheckCircle size={24} color="var(--success)" />}
                   <strong>{queryResult.domain}</strong>
                 </div>
                 
                 {queryResult.blocked ? (
                   <>
                     <div><strong>Status:</strong> <span style={{ color: 'var(--danger)' }}>BLOCKED</span></div>
                     <div><strong>Category:</strong> {queryResult.category.toUpperCase()}</div>
                     <div><strong>Source:</strong> {queryResult.source}</div>
                   </>
                 ) : (
                   <>
                     <div><strong>Status:</strong> <span style={{ color: 'var(--success)' }}>CLEAN</span></div>
                     <div style={{ color: 'var(--text-muted)' }}>Not found in threat databases.</div>
                   </>
                 )}
               </motion.div>
             )}
           </AnimatePresence>
        </div>

        {/* RIGHT: Recent Activity */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: '25px' }}>
          
          <div className="glass-panel p-6 threats-recent-panel">
            <h3>Recent Threats Added</h3>
            {recent.length === 0 ? (
              <p style={{ color: 'var(--text-muted)', marginTop: '20px' }}>No recent threats tracked.</p>
            ) : (
              <table className="data-table mt-4" style={{ width: '100%', textAlign: 'left', borderCollapse: 'collapse' }}>
                <thead>
                  <tr style={{ borderBottom: '1px solid rgba(255,255,255,0.1)' }}>
                    <th style={{ padding: '10px' }}>Domain</th>
                    <th>Category</th>
                    <th>Source</th>
                  </tr>
                </thead>
                <tbody>
                  {recent.map((n, i) => (
                    <tr key={i} style={{ borderBottom: '1px solid rgba(255,255,255,0.05)' }}>
                      <td className="domain-cell" style={{ padding: '10px', fontFamily: 'monospace' }}>{n.domain}</td>
                      <td><span className="badge danger">{n.category.toUpperCase()}</span></td>
                      <td style={{ fontSize: '0.85rem', color: 'var(--text-muted)' }}>{n.source}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </div>

          <div className="glass-panel p-6">
            <h3>Threat Sources</h3>
            <ul style={{ listStyle: 'none', padding: 0, marginTop: '15px', color: 'var(--text-muted)' }}>
              <li style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '10px' }}>
                <CheckCircle size={16} color="var(--success)" /> 
                <span><strong>URLHaus</strong> (Malware Feeds) <a href="https://urlhaus.abuse.ch" target="_blank" rel="noreferrer" style={{color: 'var(--accent-blue)', marginLeft:'5px'}}><LinkIcon size={12}/></a></span>
              </li>
              <li style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                <CheckCircle size={16} color="var(--success)" /> 
                <span><strong>StevenBlack</strong> (Ads & Trackers) <a href="https://github.com/StevenBlack/hosts" target="_blank" rel="noreferrer" style={{color: 'var(--accent-blue)', marginLeft:'5px'}}><LinkIcon size={12}/></a></span>
              </li>
            </ul>
          </div>

        </div>
      </div>
    </div>
  );
}
