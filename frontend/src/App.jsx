import React, { useState } from 'react';
import Sidebar from './components/Sidebar';
import Dashboard from './components/Dashboard';
import Vault from './components/Vault';
import Threats from './components/Threats';

function App() {
  const [activeTab, setActiveTab] = useState('dashboard');

  return (
    <div className="layout">
      <Sidebar activeTab={activeTab} setActiveTab={setActiveTab} />
      <main className="main-content">
        {activeTab === 'dashboard' && <Dashboard />}
        {activeTab === 'threats' && <Threats />}
        {activeTab === 'vault' && <Vault />}
      </main>
    </div>
  );
}

export default App;
