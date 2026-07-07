import React, { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { LogIn, UserPlus } from 'lucide-react';

export default function Login() {
  const [isRegister, setIsRegister] = useState(false);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  const navigate = useNavigate();

  const handleSubmit = async (e) => {
    e.preventDefault();
    setError('');
    setLoading(true);

    const form = new FormData(e.target);
    const data = Object.fromEntries(form.entries());
    
    const endpoint = isRegister ? '/api/auth/register' : '/api/auth/login';

    try {
      const res = await fetch(endpoint, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data)
      });
      
      const json = await res.json();
      
      if (!res.ok) {
        throw new Error(json.message || json.error || 'Authentication failed');
      }

      if (!isRegister) {
        // Logged in
        localStorage.setItem('pp_token', json.token);
        localStorage.setItem('pp_user', data.username);
        window.dispatchEvent(new Event('storage')); // trigger nav update
        navigate('/dashboard');
      } else {
        // Registered
        setIsRegister(false);
        setError('Registration successful! Please sign in.');
      }
    } catch (err) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="animate-fade-in" style={{ display: 'flex', justifyContent: 'center', marginTop: '4rem' }}>
      <div className="glass-panel" style={{ width: '100%', maxWidth: 400, padding: '2.5rem' }}>
        
        <div style={{ textAlign: 'center', marginBottom: '2rem' }}>
          <h2 style={{ fontSize: '1.8rem', marginBottom: '0.5rem' }}>
            {isRegister ? 'Create Account' : 'Welcome Back'}
          </h2>
          <p style={{ color: 'var(--text-secondary)', fontSize: '0.9rem' }}>
            {isRegister ? 'Sign up to start uploading event photos.' : 'Sign in to manage your events.'}
          </p>
        </div>

        {error && (
          <div style={{ padding: '0.8rem', background: error.includes('successful') ? 'rgba(16, 185, 129, 0.1)' : 'rgba(239, 68, 68, 0.1)', color: error.includes('successful') ? 'var(--green)' : 'var(--red)', borderRadius: 8, fontSize: '0.85rem', marginBottom: '1.5rem', border: `1px solid ${error.includes('successful') ? 'var(--green)' : 'var(--red)'}` }}>
            {error}
          </div>
        )}

        <form onSubmit={handleSubmit}>
          <div className="input-group">
            <label>Username</label>
            <input type="text" name="username" required placeholder="photographer_xyz" />
          </div>

          {isRegister && (
            <div className="input-group">
              <label>Email</label>
              <input type="email" name="email" required placeholder="you@example.com" />
            </div>
          )}

          <div className="input-group">
            <label>Password</label>
            <input type="password" name="password" required placeholder="••••••••" />
          </div>

          <button type="submit" className="btn btn-primary" style={{ width: '100%', marginTop: '1rem' }} disabled={loading}>
            {loading ? 'Processing...' : (isRegister ? <><UserPlus size={18} /> Sign Up</> : <><LogIn size={18} /> Sign In</>)}
          </button>
        </form>

        <div style={{ textAlign: 'center', marginTop: '2rem', fontSize: '0.85rem', color: 'var(--text-secondary)' }}>
          {isRegister ? 'Already have an account?' : 'Need an account?'}
          <button 
            onClick={() => { setIsRegister(!isRegister); setError(''); }} 
            style={{ background: 'none', border: 'none', color: 'var(--accent)', cursor: 'pointer', marginLeft: '0.4rem', fontWeight: 500 }}
          >
            {isRegister ? 'Sign In' : 'Sign Up'}
          </button>
        </div>
      </div>
    </div>
  );
}
