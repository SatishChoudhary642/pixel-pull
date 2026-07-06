# PixelPull Frontend

Standalone HTML/CSS/JS frontend for the PixelPull platform.

## Pages

| File | Purpose |
|------|---------|
| `index.html` | Landing page |
| `login.html` | Photographer sign in / register |
| `dashboard.html` | Photographer upload + batch management |
| `search.html` | Attendee face search |

## Running

The backend (Spring Boot) must be running on `http://localhost:8081`.

**Option 1 — VS Code Live Server (recommended)**
1. Open the `frontend/` folder in VS Code
2. Right-click `index.html` → **Open with Live Server**
3. Opens at `http://127.0.0.1:5500`

**Option 2 — IntelliJ built-in server**
1. Open any `.html` file in IntelliJ
2. Click the browser icon in the top-right corner of the editor

**Option 3 — npx serve**
```bash
npx serve .
```

## API
All requests go to `http://localhost:8081/api/...`  
The backend has CORS configured to allow requests from localhost on common dev ports.
