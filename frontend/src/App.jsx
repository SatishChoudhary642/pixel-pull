import React from 'react';
import { Routes, Route, Link, useNavigate, useLocation } from 'react-router-dom';
import { Camera, ScanFace, LogIn, LogOut, LayoutDashboard } from 'lucide-react';
import Home from './pages/Home';
import Login from './pages/Login';
import Dashboard from './pages/Dashboard';
import Search from './pages/Search';

function App() {
  const navigate = useNavigate();
  const location = useLocation();
  const token = localStorage.getItem('pp_token');
  const user = localStorage.getItem('pp_user');

  const handleLogout = () => {
    localStorage.removeItem('pp_token');
    localStorage.removeItem('pp_user');
    navigate('/');
  };

  return (
    <>
      <nav style={{
        position: 'sticky', top: 0, zIndex: 100,
        background: 'rgba(5, 5, 5, 0.8)',
        backdropFilter: 'blur(12px)',
        borderBottom: '1px solid var(--border)',
        padding: '1rem 2rem'
      }}>
        <div style={{ maxWidth: 1200, margin: '0 auto', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
          <Link to="/" style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', fontWeight: 600, fontSize: '1.2rem' }}>
            <div style={{ width: 10, height: 10, borderRadius: '50%', background: 'var(--accent)', boxShadow: '0 0 10px var(--accent-glow)' }}></div>
            PixelPull
          </Link>
          
          <div style={{ display: 'flex', alignItems: 'center', gap: '1.5rem' }}>
            {location.pathname !== '/search' && (
              <Link to="/search" style={{ color: 'var(--text-secondary)', fontSize: '0.9rem', display: 'flex', alignItems: 'center', gap: '0.4rem' }}>
                <ScanFace size={16} /> Find Photos
              </Link>
            )}
            
            {token ? (
              <div style={{ display: 'flex', alignItems: 'center', gap: '1rem' }}>
                <Link to="/dashboard" style={{ color: 'var(--text-primary)', fontSize: '0.9rem', display: 'flex', alignItems: 'center', gap: '0.4rem' }}>
                  <LayoutDashboard size={16} /> Dashboard
                </Link>
                <div style={{ width: 1, height: 16, background: 'var(--border)' }}></div>
                <span style={{ fontSize: '0.85rem', color: 'var(--text-tertiary)' }}>{user}</span>
                <button onClick={handleLogout} style={{ background: 'none', border: 'none', color: 'var(--text-secondary)', cursor: 'pointer', display: 'flex', alignItems: 'center' }}>
                  <LogOut size={16} />
                </button>
              </div>
            ) : (
              location.pathname !== '/login' && (
                <Link to="/login" className="btn btn-ghost" style={{ padding: '0.4rem 0.8rem', fontSize: '0.85rem' }}>
                  <LogIn size={16} /> Sign In
                </Link>
              )
            )}
          </div>
        </div>
      </nav>

      <main style={{ maxWidth: 1200, margin: '0 auto', padding: '2rem' }}>
        <Routes>
          <Route path="/" element={<Home />} />
          <Route path="/login" element={<Login />} />
          <Route path="/dashboard" element={<Dashboard />} />
          <Route path="/search" element={<Search />} />
        </Routes>
      </main>
    </>
  );
}

export default App;
