import React, { useState } from 'react';
import { Search as SearchIcon, Image as ImageIcon, Camera } from 'lucide-react';

export default function Search() {
  const [selfie, setSelfie] = useState(null);
  const [preview, setPreview] = useState(null);
  const [accessCode, setAccessCode] = useState('');
  const [loading, setLoading] = useState(false);
  const [results, setResults] = useState(null);
  const [error, setError] = useState('');

  const handleFileChange = (e) => {
    const file = e.target.files[0];
    if (file) {
      setSelfie(file);
      setPreview(URL.createObjectURL(file));
    }
  };

  const handleSearch = async (e) => {
    e.preventDefault();
    if (!selfie || !accessCode) return;
    
    setLoading(true);
    setError('');
    setResults(null);

    const form = new FormData();
    form.append('selfie', selfie);
    form.append('accessCode', accessCode.trim().toUpperCase());

    try {
      const res = await fetch('/api/photos/search', {
        method: 'POST',
        body: form
      });
      
      const data = await res.json();
      
      if (!res.ok) {
        throw new Error(data.message || data.error || 'Search failed');
      }
      
      setResults(data);
    } catch (err) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="animate-fade-in">
      <div style={{ textAlign: 'center', marginBottom: '3rem', marginTop: '2rem' }}>
        <h2 style={{ fontSize: '2.5rem', marginBottom: '0.5rem' }}>Find Your Photos</h2>
        <p style={{ color: 'var(--text-secondary)' }}>Enter the event code and upload a selfie to instantly find every photo you're in.</p>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'minmax(300px, 400px) 1fr', gap: '3rem', alignItems: 'start' }}>
        
        {/* Search Form */}
        <div className="glass-panel" style={{ padding: '2rem' }}>
          <form onSubmit={handleSearch}>
            <div className="input-group">
              <label>Event Access Code</label>
              <input 
                type="text" 
                value={accessCode}
                onChange={e => setAccessCode(e.target.value)}
                placeholder="e.g. A1B2C3" 
                maxLength={6}
                required 
                style={{ fontFamily: 'Space Mono', textTransform: 'uppercase', letterSpacing: '0.2em' }}
              />
            </div>

            <div className="input-group" style={{ marginTop: '1.5rem' }}>
              <label>Your Selfie</label>
              <div style={{ 
                border: '1.5px dashed var(--border)', 
                borderRadius: 8, 
                padding: '2rem 1rem', 
                textAlign: 'center', 
                position: 'relative',
                background: 'rgba(0,0,0,0.2)',
                transition: 'border 0.2s',
                height: 200,
                display: 'flex',
                flexDirection: 'column',
                alignItems: 'center',
                justifyContent: 'center'
              }}>
                <input 
                  type="file" 
                  accept="image/jpeg,image/png" 
                  onChange={handleFileChange}
                  required
                  style={{ position: 'absolute', inset: 0, opacity: 0, cursor: 'pointer', zIndex: 10 }}
                />
                
                {preview ? (
                  <div style={{ position: 'absolute', inset: 4, borderRadius: 4, overflow: 'hidden' }}>
                    <img src={preview} alt="Selfie Preview" style={{ width: '100%', height: '100%', objectFit: 'cover' }} />
                  </div>
                ) : (
                  <>
                    <Camera size={32} color="var(--text-tertiary)" style={{ marginBottom: '1rem' }} />
                    <span style={{ fontSize: '0.9rem', color: 'var(--text-secondary)' }}>Tap to take or select a selfie</span>
                  </>
                )}
              </div>
            </div>

            {error && (
              <div style={{ padding: '0.8rem', background: 'rgba(239, 68, 68, 0.1)', color: 'var(--red)', borderRadius: 8, fontSize: '0.85rem', marginTop: '1.5rem', border: '1px solid var(--red)' }}>
                {error}
              </div>
            )}

            <button type="submit" className="btn btn-primary" style={{ width: '100%', marginTop: '2rem' }} disabled={loading || !selfie || !accessCode}>
              {loading ? 'Searching Deep Learning Space...' : <><SearchIcon size={18} /> Find Me</>}
            </button>
          </form>
        </div>

        {/* Results Area */}
        <div style={{ minHeight: 400 }}>
          {!results && !loading && (
            <div style={{ height: '100%', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', color: 'var(--text-tertiary)', border: '1px dashed var(--border)', borderRadius: 16 }}>
              <ImageIcon size={48} style={{ opacity: 0.2, marginBottom: '1rem' }} />
              <p>Your photos will appear here.</p>
            </div>
          )}

          {loading && (
            <div style={{ height: '100%', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', color: 'var(--accent)' }}>
              <div style={{ width: 40, height: 40, border: '3px solid var(--border)', borderTopColor: 'var(--accent)', borderRadius: '50%', animation: 'spin 1s linear infinite' }}></div>
              <p style={{ marginTop: '1.5rem', fontFamily: 'Space Mono', fontSize: '0.9rem' }}>Extracting 128D Vector...</p>
              <style>{`@keyframes spin { to { transform: rotate(360deg); } }`}</style>
            </div>
          )}

          {results && (
            <div className="animate-fade-in">
              <h3 style={{ marginBottom: '1.5rem', display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
                Found {results.photos.length} photo{results.photos.length !== 1 ? 's' : ''}
              </h3>
              
              {results.photos.length === 0 ? (
                <div style={{ padding: '3rem', textAlign: 'center', background: 'rgba(255,255,255,0.02)', borderRadius: 16, border: '1px solid var(--border)' }}>
                  <p style={{ color: 'var(--text-secondary)' }}>We couldn't find your face in this batch.</p>
                  <p style={{ fontSize: '0.85rem', color: 'var(--text-tertiary)', marginTop: '0.5rem' }}>Make sure the access code is correct and your selfie is clear.</p>
                </div>
              ) : (
                <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(200px, 1fr))', gap: '1rem' }}>
                  {results.photos.map((photo, i) => (
                    <div key={i} className="glass-panel animate-fade-in" style={{ padding: '0.5rem', overflow: 'hidden', animationDelay: `${i * 100}ms` }}>
                      <div style={{ aspectRatio: '3/4', borderRadius: 8, overflow: 'hidden', position: 'relative' }}>
                        <img src={`http://localhost:8081${photo.imageUrl}`} alt="Found" style={{ width: '100%', height: '100%', objectFit: 'cover' }} />
                        <a href={`http://localhost:8081${photo.imageUrl}`} download target="_blank" rel="noreferrer" style={{ position: 'absolute', bottom: '0.5rem', right: '0.5rem', background: 'rgba(0,0,0,0.6)', backdropFilter: 'blur(4px)', padding: '0.3rem 0.8rem', borderRadius: 20, fontSize: '0.75rem', color: '#fff', border: '1px solid rgba(255,255,255,0.2)' }}>
                          Open Full
                        </a>
                      </div>
                    </div>
                  ))}
                </div>
              )}
            </div>
          )}
        </div>

      </div>
    </div>
  );
}
