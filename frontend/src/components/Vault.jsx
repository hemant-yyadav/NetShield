import React, { useState, useEffect, useRef } from 'react';
import { motion, AnimatePresence } from 'framer-motion';
import { UploadCloud, FileLock, Shield, Download, Trash2, Link as LinkIcon, Copy, Fingerprint, Lock, Key, Globe } from 'lucide-react';

export default function Vault() {
  const [files, setFiles] = useState([]);
  const [isDragging, setIsDragging] = useState(false);
  const [uploading, setUploading] = useState(false);
  const [expiryHours, setExpiryHours] = useState(1);
  const [lastUploadToken, setLastUploadToken] = useState(null);
  const fileInputRef = useRef(null);

  const [shareMode, setShareMode] = useState(false);

  // Password Modal State
  const [passwordModal, setPasswordModal] = useState({ isOpen: false, mode: '', file: null });
  const [passwordInput, setPasswordInput] = useState('');

  const fetchFiles = async () => {
    try {
      const res = await fetch('/api/vault/files');
      if (res.ok) {
        const data = await res.json();
        setFiles(data);
      }
    } catch (err) {
      console.error("Failed to fetch files", err);
    }
  };

  useEffect(() => {
    fetchFiles();
  }, []);

  const handleDrop = async (e) => {
    e.preventDefault();
    setIsDragging(false);
    const droppedFile = e.dataTransfer.files[0];
    if (droppedFile) triggerUploadFlow(droppedFile);
  };

  const handleFileSelect = (e) => {
    const selectedFile = e.target.files[0];
    if (selectedFile) triggerUploadFlow(selectedFile);
    if (fileInputRef.current) fileInputRef.current.value = "";
  };

  const triggerUploadFlow = (file) => {
    if (shareMode) {
      processShareUpload(file);
    } else {
      setPasswordModal({ isOpen: true, mode: 'upload', file: file });
    }
  };

  const submitPassword = () => {
    if (!passwordInput.trim()) return;
    if (passwordModal.mode === 'upload') {
        processPersonalUpload(passwordModal.file, passwordInput);
    } else if (passwordModal.mode === 'decrypt') {
        processDecrypt(passwordModal.file, passwordInput);
    }
    setPasswordModal({ isOpen: false, mode: '', file: null });
    setPasswordInput('');
  };

  const processShareUpload = async (file) => {
    setUploading(true);
    setLastUploadToken(null);
    const formData = new FormData();
    formData.append('file', file);
    formData.append('expiry_hours', expiryHours);

    try {
      const res = await fetch('/api/vault/share/generate', {
        method: 'POST',
        body: formData
      });
      if (res.ok) {
        const data = await res.json();
        setLastUploadToken(data);
        fetchFiles();
      } else {
        alert("Share Generation Failed.");
      }
    } catch (err) {
      console.error("Share error", err);
    } finally {
      setUploading(false);
    }
  };

  const processPersonalUpload = async (file, password) => {
    setUploading(true);
    setLastUploadToken(null);
    const formData = new FormData();
    formData.append('file', file);
    formData.append('password', password);

    try {
      const res = await fetch('/api/vault/upload', {
        method: 'POST',
        body: formData
      });
      if (res.ok) {
        fetchFiles();
      } else {
        alert("Personal Upload Failed.");
      }
    } catch (err) {
      console.error("Upload error", err);
    } finally {
      setUploading(false);
    }
  };

  const handleDownloadInit = (fileRec) => {
    if (fileRec.is_retrieved || new Date() > new Date(fileRec.token_expiry + 'Z')) return;
    
    if (fileRec.download_token === "personal_vault_item") {
        setPasswordModal({ isOpen: true, mode: 'decrypt', file: fileRec });
    } else {
        // If it's a shared link, we can just download it via the decoupled endpoint natively
        window.location.href = `/api/vault/share/download/${fileRec.download_token}`;
        setTimeout(fetchFiles, 2000);
    }
  };

  const processDecrypt = async (fileRec, password) => {
    try {
      const formData = new FormData();
      formData.append('file_id', fileRec.id);
      formData.append('password', password);

      const res = await fetch(`/api/vault/decrypt`, {
        method: 'POST',
        body: formData
      });

      if (!res.ok) {
        const errTx = await res.json();
        alert("Decryption rejected: " + (errTx.detail || "Authentication Error"));
        fetchFiles();
        return;
      }
      
      const blob = await res.blob();
      const url = window.URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.style.display = 'none';
      a.href = url;
      a.download = fileRec.original_filename;
      document.body.appendChild(a);
      a.click();
      window.URL.revokeObjectURL(url);
      fetchFiles(); 
    } catch (err) {
      console.error("Download fail", err);
    }
  };

  const containerVariants = {
    hidden: { opacity: 0 },
    show: { opacity: 1, transition: { staggerChildren: 0.1 } }
  };

  const cardVariants = {
    hidden: { y: 20, opacity: 0 },
    show: { y: 0, opacity: 1 }
  };

  return (
    <div className="vault-wrapper">
      <div className="header vault-header">
        <h1>Secure Vault</h1>
        <p>AES-256 Crypto Engine with One-Time Single-Access Delivery.</p>
      </div>

      <div className="vault-split">
        {/* LEFT PANEL */}
        <div className="vault-left">
        
          <div className="mode-toggle-box glass-panel" onClick={() => setShareMode(!shareMode)}>
              <div className={`mode-switch ${shareMode ? 'active' : ''}`}>
                 <div className="mode-knob"></div>
              </div>
              <span style={{ fontWeight: 600, color: shareMode ? 'var(--success)' : 'var(--text-main)' }}>
                 {shareMode ? <><Globe size={14} style={{ position: 'relative', top: '2px', marginRight: '4px' }}/> Public Share Mode</> : <><Lock size={14} style={{ position: 'relative', top: '2px', marginRight: '4px' }}/> Personal Vault</>}
              </span>
          </div>

          <motion.div 
            className={`drop-zone glass-panel ${isDragging ? 'active' : ''}`}
            onDragOver={(e) => { e.preventDefault(); setIsDragging(true); }}
            onDragLeave={() => setIsDragging(false)}
            onDrop={handleDrop}
            animate={{ scale: isDragging ? 1.02 : 1, boxShadow: isDragging ? "0 0 20px rgba(99, 102, 241, 0.4)" : "none", borderColor: shareMode ? "rgba(34, 197, 94, 0.4)" : "rgba(99, 102, 241, 0.4)" }}
            onClick={() => fileInputRef.current?.click()}
          >
            <input type="file" ref={fileInputRef} style={{display: 'none'}} onChange={handleFileSelect} />
            
            <motion.div 
              animate={{ y: uploading ? [0, -10, 0] : 0 }} 
              transition={{ repeat: uploading ? Infinity : 0, duration: 1.5 }}
            >
              {uploading ? <Shield size={48} color={shareMode ? "var(--success)" : "var(--accent-blue)"} /> : <UploadCloud size={48} color="var(--text-muted)" />}
            </motion.div>
            
            <h3>{uploading ? "Encrypting Payload..." : "Drop payload here"}</h3>
            <p className="subtitle">{shareMode ? "Generates an anonymous JWT download link" : "Locks natively behind your Personal Password"}</p>
          </motion.div>

          <AnimatePresence>
            {shareMode && (
              <motion.div initial={{ opacity: 0, height: 0 }} animate={{ opacity: 1, height: 'auto' }} exit={{ opacity: 0, height: 0 }} className="vault-controls glass-panel">
                <label>Expiry Window (Hours)</label>
                <div className="slider-row">
                  <input type="range" min="1" max="24" value={expiryHours} onChange={(e) => setExpiryHours(e.target.value)} />
                  <span className="value-cap">{expiryHours}h</span>
                </div>
              </motion.div>
            )}
          </AnimatePresence>

          <AnimatePresence>
            {lastUploadToken && shareMode && (
              <motion.div 
                className="success-card glass-panel"
                initial={{ opacity: 0, y: 20 }}
                animate={{ opacity: 1, y: 0 }}
                exit={{ opacity: 0, scale: 0.9 }}
              >
                <div className="success-header">
                  <Fingerprint size={20} color="var(--success)" />
                  <span>Link Secured [JWT]</span>
                </div>
                <div className="copy-box">
                  <input type="text" readOnly value={`https://eqsaj-103-46-202-70.run.pinggy-free.link${lastUploadToken.share_url}`} />
                  <button onClick={() => navigator.clipboard.writeText(`https://eqsaj-103-46-202-70.run.pinggy-free.link${lastUploadToken.share_url}`)}>
                    <Copy size={16} />
                  </button>
                </div>
                <p className="warning-text" style={{ marginTop: '12px' }}>This link strictly requires no password.</p>
              </motion.div>
            )}
          </AnimatePresence>
        </div>

        {/* RIGHT PANEL */}
        <div className="vault-right">
          <motion.div 
            className="vault-grid"
            variants={containerVariants}
            initial="hidden"
            animate="show"
          >
            {files.length === 0 ? (
              <div className="empty-vault">
                 <Shield size={48} opacity={0.3} />
                 <h3>Your vault is empty</h3>
                 <p>Drop a payload to secure it.</p>
              </div>
            ) : files.map((f) => {
              const isShared = f.download_token !== "personal_vault_item";
              const isLocked = f.is_retrieved || (isShared && new Date() > new Date(f.token_expiry + 'Z'));
              
              return (
                <motion.div 
                  key={f.id} 
                  className={`vault-card glass-panel ${isLocked ? 'locked' : ''}`}
                  variants={cardVariants}
                  whileHover={{ scale: isLocked ? 1 : 1.02 }}
                  style={{ borderLeft: isShared ? "3px solid var(--success)" : "3px solid transparent" }}
                >
                  <div className="card-top">
                    {isShared ? <Globe size={24} color={isLocked ? "var(--danger)" : "var(--success)"} /> : <FileLock size={24} color={isLocked ? "var(--danger)" : "var(--accent-blue)"} />}
                    {isLocked && <span className="badge danger"><Lock size={12}/> LOCKED</span>}
                    {!isLocked && <span className="badge success">{isShared ? 'JWT LINK' : 'ACTIVE'}</span>}
                  </div>
                  
                  <h4>{f.original_filename}</h4>
                  <div className="card-meta">
                    <span>Uploaded: {new Date(f.uploaded_at + 'Z').toLocaleTimeString()}</span>
                  </div>

                  <div className="card-actions">
                     <button 
                       className="action-btn download-btn" 
                       disabled={isLocked}
                       onClick={() => handleDownloadInit(f)}
                       style={{ background: isShared ? "var(--success)" : undefined }}
                     >
                       {isShared ? <><Download size={16} /> Download</> : <><Download size={16} /> Decrypt</>}
                     </button>
                  </div>
                </motion.div>
              );
            })}
          </motion.div>
        </div>
      </div>

      {/* Password Overlay Modal */}
      <AnimatePresence>
        {passwordModal.isOpen && (
          <motion.div 
            className="modal-overlay"
            initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}
          >
            <motion.div 
              className="modal-content glass-panel"
              initial={{ scale: 0.9, y: 20 }} animate={{ scale: 1, y: 0 }} exit={{ scale: 0.9, y: 20 }}
            >
              <div className="modal-icon"><Key size={32} color="var(--accent-blue)" /></div>
              <h2>{passwordModal.mode === 'upload' ? 'Encrypt Payload' : 'Decrypt Payload'}</h2>
              <p>Enter your Personal Password to authenticate this purely manual operation.</p>
              
              <input 
                type="password" 
                placeholder="Personal Password..." 
                value={passwordInput}
                onChange={(e) => setPasswordInput(e.target.value)}
                onKeyDown={(e) => { if (e.key === 'Enter') submitPassword(); }}
                autoFocus
              />

              <div className="modal-actions">
                <button className="btn-secondary" onClick={() => { setPasswordModal({isOpen: false}); setPasswordInput(''); }}>Cancel</button>
                <button className="btn-primary" onClick={submitPassword}>Confirm</button>
              </div>
            </motion.div>
          </motion.div>
        )}
      </AnimatePresence>

    </div>
  );
}
