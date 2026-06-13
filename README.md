# PixelPull 📸

PixelPull is a production-grade, highly optimized microservice application designed to solve a massive problem at college fests and large events: **Finding your photos out of thousands.**

Instead of scrolling through hundreds of photos, an attendee simply uploads a selfie, and PixelPull instantly returns every group photo they are in using advanced facial recognition and vectorized mathematical searching.

## 🌟 Features
* **Stateless JWT Authentication:** Secure registration and login flows for Event Photographers.
* **Batch Scoped Search:** Photographers get a 6-character `accessCode` for their events, restricting search space and dropping search times to under `O(M)`.
* **C++ Dlib Background Processing:** A native C++ worker continuously polls the database, extracting every face in an image into 128D vectors.
* **Lightning Fast Search:** Vector math calculated entirely in Java memory using Euclidean distance formulas against PostgreSQL records.
* **Static Asset Serving:** Built-in Spring Boot resource handlers to serve physical image files directly to frontends.

---

## 🏗 Architecture

PixelPull uses an asynchronous microservice architecture to decouple the heavy Machine Learning processing from the high-traffic Spring Boot REST API.

```mermaid
graph TD
    %% Define styles
    classDef client fill:#f9f,stroke:#333,stroke-width:2px;
    classDef api fill:#bbf,stroke:#333,stroke-width:2px;
    classDef db fill:#bfb,stroke:#333,stroke-width:2px;
    classDef worker fill:#fbf,stroke:#333,stroke-width:2px;

    %% Client Actors
    Photographer([Photographer 📸]):::client
    Attendee([Attendee 📱]):::client

    %% Spring Boot API
    subgraph Spring Boot Application
        AuthController[Auth Controller<br/>/api/auth/login]:::api
        UploadController[Photo Controller<br/>/api/photos/upload]:::api
        SearchController[Search Controller<br/>/api/photos/search]:::api
        PhotoService[Photo Service<br/>(Java Euclidean Math)]:::api
    end

    %% Storage
    subgraph Storage & Database
        Postgres[(PostgreSQL Database<br/>users, photos, photo_faces)]:::db
        DiskStorage[Local Disk /uploads]:::db
    end

    %% Background Worker
    subgraph Docker
        CPPWorker[C++ Worker Daemon<br/>(Polling Database)]:::worker
        CPPCLI[C++ Extractor CLI<br/>(Synchronous)]:::worker
    end

    %% --- Photographer Flow ---
    Photographer -- "1. Login (Gets JWT)" --> AuthController
    Photographer -- "2. Upload 500 Photos + JWT" --> UploadController
    UploadController -- "3. Save Images" --> DiskStorage
    UploadController -- "4. Save Metadata (Status: PENDING)" --> Postgres

    %% --- Background Processing Flow ---
    CPPWorker -- "5. Poll DB every 5s" --> Postgres
    Postgres -. "6. Returns up to 10 PENDING photos" .-> CPPWorker
    CPPWorker -- "7. Read Image File" --> DiskStorage
    CPPWorker -- "8. Detect multiple faces & compute 128D Vectors" --> CPPWorker
    CPPWorker -- "9. Update Status PROCESSED & Insert vectors" --> Postgres

    %% --- Attendee Search Flow ---
    Attendee -- "10. Upload Selfie + AccessCode" --> SearchController
    SearchController -- "11. Run face_extractor synchronously" --> CPPCLI
    CPPCLI -. "12. Return Selfie 128D Vector" .-> SearchController
    SearchController -- "13. Pass to Service" --> PhotoService
    PhotoService -- "14. Get all vectors for AccessCode" --> Postgres
    PhotoService -- "15. Calculate Math.sqrt() distances" --> PhotoService
    PhotoService -. "16. Return matching Photo URLs" .-> Attendee
```

---

## 🚀 How to Run Locally

### Prerequisites
* **Docker & Docker Compose** (for PostgreSQL and the C++ Worker container).
* **Java 17** and **Maven**.

### Step 1: Start Infrastructure & ML Worker
Open your terminal in the root project directory and run:
```bash
# This will spin up the database and build the C++ container. 
# NOTE: The first run takes 15-30 minutes for Dlib to compile natively!
docker compose up -d --build
```

### Step 2: Start the Spring Boot API
In a separate terminal, navigate into the `PixelPull` folder:
```bash
cd PixelPull
.\mvnw spring-boot:run
```

---

## 📖 API Usage Guide

### For Photographers
1. **Register an Account:**
   `POST /api/auth/register`
   ```json
   {
     "username": "admin",
     "password": "password123",
     "email": "admin@example.com"
   }
   ```

2. **Login (Receive JWT):**
   `POST /api/auth/login`
   *(Returns `{"jwt": "eyJh..."}`)*

3. **Upload an Event Batch:**
   `POST /api/photos/upload`
   * **Headers:** `Authorization: Bearer <your_jwt>`
   * **Body (Multipart/form-data):** Key `files` containing multiple images.

4. **Get Your Batches & Access Codes:**
   `GET /api/photos/my-batches`
   * **Headers:** `Authorization: Bearer <your_jwt>`
   * *(Returns your `accessCode` to give to attendees).*

### For Attendees
1. **Search for Yourself (Public Endpoint!):**
   `POST /api/photos/search`
   * **Body (Multipart/form-data):**
     * Key `selfie`: Your face image.
     * Key `accessCode`: The 6-character code given by the photographer.
   * **Response:** Returns JSON with full, clickable `imageUrl` paths to your matched group photos!

---

*Built for absolute performance, massive concurrency, and secure data scoping.*
