import React from 'react';
import { Shield, LayoutDashboard, ShieldAlert, FolderLock, Settings } from 'lucide-react';

export default function Sidebar({ activeTab, setActiveTab }) {
  const navItems = [
    { id: 'dashboard', label: 'Dashboard', icon: LayoutDashboard },
    { id: 'threats', label: 'Threats', icon: ShieldAlert },
    { id: 'vault', label: 'Secure Vault', icon: FolderLock },
    { id: 'settings', label: 'Settings', icon: Settings },
  ];

  return (
    <aside className="sidebar">
      <div className="sidebar-logo">
        <Shield size={32} />
        <span>NetShield</span>
      </div>
      
      <nav className="nav-menu">
        {navItems.map(item => {
          const Icon = item.icon;
          return (
            <div 
              key={item.id}
              className={`nav-item ${activeTab === item.id ? 'active' : ''}`}
              onClick={() => setActiveTab(item.id)}
            >
              <Icon size={20} />
              <span>{item.label}</span>
            </div>
          );
        })}
      </nav>
    </aside>
  );
}
