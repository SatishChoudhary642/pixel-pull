// Shared API layer — all calls go through here
const API = {

  async register(username, email, password) {
    const res = await fetch('/api/auth/register', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username, email, password })
    });
    return { ok: res.ok, data: await res.json() };
  },

  async login(username, password) {
    const res = await fetch('/api/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username, password })
    });
    return { ok: res.ok, data: await res.json() };
  },

  async uploadPhotos(files) {
    const token = getToken();
    const form = new FormData();
    files.forEach(f => form.append('files', f));
    const res = await fetch('/api/photos/upload', {
      method: 'POST',
      headers: { 'Authorization': `Bearer ${token}` },
      body: form
    });
    return { ok: res.ok, data: await res.json() };
  },

  async getMyBatches() {
    const token = getToken();
    const res = await fetch('/api/photos/my-batches', {
      headers: { 'Authorization': `Bearer ${token}` }
    });
    return { ok: res.ok, data: await res.json() };
  },

  async search(selfie, accessCode) {
    const form = new FormData();
    form.append('selfie', selfie);
    form.append('accessCode', accessCode.trim().toUpperCase());
    const res = await fetch('/api/photos/search', {
      method: 'POST',
      body: form
    });
    return { ok: res.ok, data: await res.json() };
  }
};

// ── Auth helpers ──────────────────────────────

function getToken()    { return localStorage.getItem('pp_token'); }
function getUsername() { return localStorage.getItem('pp_user'); }
function isLoggedIn()  { return !!getToken(); }

function saveAuth(token, username) {
  localStorage.setItem('pp_token', token);
  localStorage.setItem('pp_user', username);
}

function clearAuth() {
  localStorage.removeItem('pp_token');
  localStorage.removeItem('pp_user');
}

function requireAuth() {
  if (!isLoggedIn()) { window.location.href = '/login.html'; }
}

// ── Toast ────────────────────────────────────

function showToast(msg, type = '') {
  let el = document.getElementById('toast');
  if (!el) {
    el = document.createElement('div');
    el.id = 'toast';
    document.body.appendChild(el);
  }
  el.textContent = msg;
  el.className = type ? `show ${type}` : 'show';
  clearTimeout(el._timer);
  el._timer = setTimeout(() => { el.className = ''; }, 3000);
}

// ── Date format ───────────────────────────────

function fmtDate(str) {
  if (!str) return '—';
  return new Date(str).toLocaleDateString('en-IN', {
    day: '2-digit', month: 'short', year: 'numeric',
    hour: '2-digit', minute: '2-digit'
  });
}

// ── Nav active user ───────────────────────────

function updateNav() {
  const el = document.getElementById('nav-user');
  if (!el) return;
  if (isLoggedIn()) {
    el.innerHTML = `
      <span class="text-sm text-muted">${getUsername()}</span>
      <button class="btn btn-ghost btn-sm" onclick="logout()">Sign out</button>
    `;
  }
}

function logout() {
  clearAuth();
  window.location.href = '/';
}
