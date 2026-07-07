import React from 'react';
import { Link } from 'react-router-dom';
import { ArrowRight, UploadCloud, ScanLine, Image } from 'lucide-react';

export default function Home() {
  return (
    <div className="animate-fade-in" style={{ padding: '2rem 0' }}>
      {/* Hero Section */}
      <div style={{ maxWidth: 700, marginTop: '3rem', marginBottom: '5rem' }}>
        <div style={{ display: 'inline-flex', alignItems: 'center', gap: '0.5rem', marginBottom: '1.5rem', padding: '0.3rem 0.8rem', background: 'rgba(255,255,255,0.05)', borderRadius: 20, border: '1px solid var(--border)' }}>
          <span style={{ width: 8, height: 8, borderRadius: '50%', background: 'var(--accent)', boxShadow: '0 0 10px var(--accent)' }}></span>
          <span style={{ fontSize: '0.75rem', textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-secondary)' }}>Face recognition • Event photos</span>
        </div>
        
        <h1 style={{ fontSize: 'clamp(2.5rem, 6vw, 4.5rem)', lineHeight: 1.1, marginBottom: '1.5rem' }}>
          Photos find <br />
          <span className="text-gradient">their people.</span>
        </h1>
        
        <p style={{ fontSize: '1.1rem', color: 'var(--text-secondary)', marginBottom: '2.5rem', maxWidth: 500, lineHeight: 1.7 }}>
          Upload your event album once. Every attendee gets exactly the photos they're in — found by their face, delivered in under two seconds.
        </p>
        
        <div style={{ display: 'flex', gap: '1rem', flexWrap: 'wrap' }}>
          <Link to="/login" className="btn btn-primary" style={{ padding: '0.8rem 1.5rem' }}>
            Upload Photos <ArrowRight size={18} />
          </Link>
          <Link to="/search" className="btn btn-ghost" style={{ padding: '0.8rem 1.5rem' }}>
            Find My Photos
          </Link>
        </div>
      </div>




      {/* How it works */}
      <div>
        <h3 style={{ fontSize: '0.8rem', textTransform: 'uppercase', letterSpacing: '0.1em', color: 'var(--text-tertiary)', marginBottom: '2rem' }}>How it works</h3>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(240px, 1fr))', gap: '1px', background: 'var(--border)', border: '1px solid var(--border)', borderRadius: 16, overflow: 'hidden' }}>
          {[
            { step: '01', title: 'Upload event photos', desc: 'Sign in and upload your album. Every batch gets a unique 6-character access code.', icon: UploadCloud },
            { step: '02', title: 'Faces are extracted', desc: 'Each photo is scanned automatically in the background to find and map every face.', icon: ScanLine },
            { step: '03', title: 'Enter code + selfie', desc: 'Attendees enter the access code and upload a selfie. No account needed.', icon: Image },
            { step: '04', title: 'Get your photos', desc: 'Only the photos you appear in are returned. Download them instantly.', icon: ArrowRight }
          ].map((item, i) => (
            <div key={i} className="glass-panel" style={{ borderRadius: 0, padding: '2rem', border: 'none', background: 'var(--bg-surface)' }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '1.5rem' }}>
                <span className="mono" style={{ fontSize: '0.8rem', color: 'var(--text-tertiary)' }}>{item.step}</span>
                <item.icon size={20} color="var(--accent)" style={{ opacity: 0.8 }} />
              </div>
              <h4 style={{ marginBottom: '0.5rem', fontSize: '1.1rem' }}>{item.title}</h4>
              <p style={{ fontSize: '0.9rem', color: 'var(--text-secondary)' }}>{item.desc}</p>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
