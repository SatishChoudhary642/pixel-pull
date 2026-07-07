import React, { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import { Upload, Folder, CheckCircle, Clock, XCircle, AlertCircle, Copy, Check } from 'lucide-react';

export default function Dashboard() {
  const [batches, setBatches] = useState([]);
  const [files, setFiles] = useState([]);
  const [uploading, setUploading] = useState(false);
  const [uploadResult, setUploadResult] = useState(null); // { accessCode, count }
  const [uploadError, setUploadError] = useState('');
  const [copied, setCopied] = useState(false);
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
        // API returns array — if it's an error object, treat as empty
        setBatches(Array.isArray(data) ? data : []);
      } else {
        const err = await res.json().catch(() => ({}));
        console.error('Failed to fetch batches:', err.message || res.status);
      }
    } catch (e) {
      console.error('Failed to fetch batches', e);
    }
  };

  const handleFileChange = (e) => {
    if (e.target.files) {
      setFiles(Array.from(e.target.files));
      setUploadResult(null);
      setUploadError('');
    }
  };

  const handleUpload = async () => {
    if (files.length === 0) return;
    setUploading(true);
    setUploadResult(null);
    setUploadError('');

    const form = new FormData();
    files.forEach(f => form.append('files', f));

    try {
      const res = await fetch('/api/photos/upload', {
        method: 'POST',
        headers: { 'Authorization': `Bearer ${localStorage.getItem('pp_token')}` },
        body: form
      });

      const data = await res.json();

      if (!res.ok) {
        setUploadError(data.message || 'Upload failed. Please try again.');
      } else {
        setFiles([]);
        setUploadResult({ accessCode: data.accessCode, count: data.successfulUploads });
        fetchBatches();
      }
    } catch (e) {
      console.error(e);
      setUploadError('Network error. Is the server running?');
    } finally {
      setUploading(false);
    }
  };

  const copyCode = (code) => {
    navigator.clipboard.writeText(code);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  const getStatusIcon = (status) => {
    switch (status) {
      case 'PROCESSED': return <CheckCircle size={14} color="var(--green)" />;
      case 'FAILED':    return <XCircle size={14} color="var(--red)" />;
      default:          return <Clock size={14} color="#eab308" />;
    }
  };

  const getStatusLabel = (batch) => {
    if (batch.status === 'PROCESSED') return 'All processed';
    if (batch.status === 'FAILED') return 'Failed';
    return `${batch.processedCount ?? 0} / ${batch.photoCount} processed`;
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
              Drag &amp; drop or click to select event photos.
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

            {/* Upload error */}
            {uploadError && (
              <div style={{ marginTop: '1.5rem', padding: '0.8rem', background: 'rgba(239,68,68,0.1)', color: 'var(--red)', borderRadius: 8, fontSize: '0.85rem', border: '1px solid var(--red)', textAlign: 'left', zIndex: 20, position: 'relative' }}>
                {uploadError}
              </div>
            )}

            {/* Upload success — show access code */}
            {uploadResult && (
              <div style={{ marginTop: '1.5rem', padding: '1rem', background: 'rgba(16,185,129,0.08)', borderRadius: 8, border: '1px solid var(--green)', textAlign: 'left', zIndex: 20, position: 'relative' }}>
                <p style={{ fontSize: '0.8rem', color: 'var(--green)', marginBottom: '0.5rem' }}>
                  ✓ {uploadResult.count} photo{uploadResult.count !== 1 ? 's' : ''} uploaded — share this code:
                </p>
                <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
                  <span style={{ fontFamily: 'Space Mono', fontSize: '1.4rem', fontWeight: 700, color: 'var(--accent)', letterSpacing: '0.2em' }}>
                    {uploadResult.accessCode}
                  </span>
                  <button
                    onClick={() => copyCode(uploadResult.accessCode)}
                    style={{ background: 'none', border: 'none', color: 'var(--text-secondary)', cursor: 'pointer', padding: '0.2rem', display: 'flex', alignItems: 'center' }}
                  >
                    {copied ? <Check size={16} color="var(--green)" /> : <Copy size={16} />}
                  </button>
                </div>
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
                      {new Date(batch.uploadTime).toLocaleString()} · {batch.photoCount} photo{batch.photoCount !== 1 ? 's' : ''}
                    </div>
                  </div>
                  <div style={{ display: 'flex', alignItems: 'center', gap: '0.4rem', fontSize: '0.8rem', color: 'var(--text-secondary)' }}>
                    {getStatusIcon(batch.status)}
                    {getStatusLabel(batch)}
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
