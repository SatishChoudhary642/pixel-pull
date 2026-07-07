import React, { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import { Upload, Folder, CheckCircle, Clock, XCircle, AlertCircle } from 'lucide-react';

export default function Dashboard() {
  const [batches, setBatches] = useState([]);
  const [files, setFiles] = useState([]);
  const [uploading, setUploading] = useState(false);
  const navigate = useNavigate();

  useEffect(() => {
    const token = localStorage.getItem('pp_token');
    if (!token) {
      navigate('/login');
      return;
    }
    fetchBatches();
  }, [navigate]);

  const fetchBatches = async () => {
    try {
      const res = await fetch('/api/photos/my-batches', {
        headers: { 'Authorization': `Bearer ${localStorage.getItem('pp_token')}` }
      });
      if (res.ok) {
        const data = await res.json();
        setBatches(data);
      }
    } catch (e) {
      console.error('Failed to fetch batches', e);
    }
  };

  const handleFileChange = (e) => {
    if (e.target.files) {
      setFiles(Array.from(e.target.files));
    }
  };

  const handleUpload = async () => {
    if (files.length === 0) return;
    setUploading(true);
    
    const form = new FormData();
    files.forEach(f => form.append('files', f));

    try {
      const res = await fetch('/api/photos/upload', {
        method: 'POST',
        headers: { 'Authorization': `Bearer ${localStorage.getItem('pp_token')}` },
        body: form
      });
      
      if (res.ok) {
        setFiles([]);
        fetchBatches(); // Refresh list
      } else {
        alert('Upload failed');
      }
    } catch (e) {
      console.error(e);
      alert('Upload error');
    } finally {
      setUploading(false);
    }
  };

  const getStatusIcon = (status) => {
    switch (status) {
      case 'PROCESSED': return <CheckCircle size={16} color="var(--green)" />;
      case 'PENDING': 
      case 'PROCESSING': return <Clock size={16} color="#eab308" />;
      case 'FAILED': return <XCircle size={16} color="var(--red)" />;
      default: return <AlertCircle size={16} color="var(--text-secondary)" />;
    }
  };

  return (
    <div className="animate-fade-in">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '2rem' }}>
        <h2>Dashboard</h2>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: '1fr 2fr', gap: '2rem' }}>
        
        {/* Upload Column */}
        <div>
          <div className="glass-panel" style={{ padding: '2rem', textAlign: 'center', position: 'relative' }}>
            <input 
              type="file" 
              multiple 
              accept="image/jpeg,image/png" 
              onChange={handleFileChange}
              style={{ position: 'absolute', inset: 0, opacity: 0, cursor: 'pointer', zIndex: 10 }}
              disabled={uploading}
            />
            <div style={{ width: 60, height: 60, borderRadius: '50%', background: 'rgba(255,255,255,0.05)', display: 'flex', alignItems: 'center', justifyContent: 'center', margin: '0 auto 1.5rem', border: '1px solid var(--border)' }}>
              <Upload size={24} color="var(--accent)" />
            </div>
            <h3 style={{ marginBottom: '0.5rem' }}>Upload Photos</h3>
            <p style={{ fontSize: '0.9rem', color: 'var(--text-secondary)' }}>
              Drag & drop or click to select event photos.
            </p>

            {files.length > 0 && (
              <div style={{ marginTop: '2rem', padding: '1rem', background: 'var(--bg-dark)', borderRadius: 8, textAlign: 'left', zIndex: 20, position: 'relative' }}>
                <p style={{ fontSize: '0.85rem', marginBottom: '1rem' }}>{files.length} files selected</p>
                <button 
                  onClick={handleUpload} 
                  className="btn btn-primary" 
                  style={{ width: '100%' }}
                  disabled={uploading}
                >
                  {uploading ? 'Uploading...' : 'Start Upload'}
                </button>
              </div>
            )}
          </div>
        </div>

        {/* Batches Column */}
        <div className="glass-panel" style={{ padding: '2rem' }}>
          <h3 style={{ marginBottom: '1.5rem', display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
            <Folder size={18} /> My Event Batches
          </h3>
          
          {batches.length === 0 ? (
            <div style={{ textAlign: 'center', padding: '3rem 0', color: 'var(--text-secondary)' }}>
              <Folder size={40} style={{ opacity: 0.2, margin: '0 auto 1rem' }} />
              <p>No batches uploaded yet.</p>
            </div>
          ) : (
            <div style={{ display: 'flex', flexDirection: 'column', gap: '0.5rem' }}>
              {batches.map((batch, i) => (
                <div key={i} style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '1rem', background: 'rgba(0,0,0,0.2)', borderRadius: 8, border: '1px solid var(--border)' }}>
                  <div>
                    <div style={{ fontFamily: 'Space Mono', fontSize: '1.1rem', color: 'var(--accent)', fontWeight: 700, letterSpacing: '0.1em' }}>
                      {batch.accessCode}
                    </div>
                    <div style={{ fontSize: '0.8rem', color: 'var(--text-secondary)', marginTop: '0.2rem' }}>
                      {new Date(batch.uploadTime).toLocaleString()}
                    </div>
                  </div>
                  <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', fontSize: '0.85rem' }}>
                    {getStatusIcon(batch.status)} {batch.status}
                  </div>
                </div>
              ))}
            </div>
          )}
        </div>

      </div>
    </div>
  );
}
