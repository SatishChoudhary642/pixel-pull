# PixelPull

A backend service for event photo distribution. Attendees upload a single selfie to instantly retrieve all group photos containing their face from a specific event batch.

## Features

- **JWT Authentication**: Secured endpoints allowing only registered photographers to create events and upload batches.
- **Batch Isolation**: Photos are grouped by an `accessCode`. Searches are scoped to a specific event, significantly reducing vector comparison overhead.
- **Asynchronous ML Worker**: A containerized C++ worker (using Dlib) polls the database to asynchronously extract 128D facial vectors from uploaded group photos.
- **In-Memory Vector Search**: Sub-second facial matching achieved via Java-based Euclidean distance calculations against PostgreSQL records.

---

## Architecture

The system uses a decoupled architecture to separate the heavy ML processing from the Spring Boot API.

```mermaid
graph TD
    %% Define styles
    classDef client fill:#f9f,stroke:#333,stroke-width:2px;
    classDef api fill:#bbf,stroke:#333,stroke-width:2px;
    classDef db fill:#bfb,stroke:#333,stroke-width:2px;
    classDef worker fill:#fbf,stroke:#333,stroke-width:2px;

    Photographer(["Photographer"]):::client
    Attendee(["Attendee"]):::client

    subgraph "Spring Boot API"
        AuthController["Auth Controller<br>/api/auth/login"]:::api
        UploadController["Photo Controller<br>/api/photos/upload"]:::api
        SearchController["Search Controller<br>/api/photos/search"]:::api
        PhotoService["Photo Service"]:::api
    end

    subgraph "Storage"
        Postgres[("PostgreSQL Database")]:::db
        DiskStorage["Local Disk (/uploads)"]:::db
    end

    subgraph "Worker"
        CPPWorker["C++ Worker Daemon"]:::worker
        CPPCLI["C++ Extractor CLI"]:::worker
    end

    %% Photographer Flow
    Photographer -- "1. Login (Gets JWT)" --> AuthController
    Photographer -- "2. Upload Photos" --> UploadController
    UploadController -- "3. Save files" --> DiskStorage
    UploadController -- "4. Save 'PENDING' state" --> Postgres

    %% Background Processing
    CPPWorker -- "5. Poll DB" --> Postgres
    Postgres -. "6. Return PENDING photos" .-> CPPWorker
    CPPWorker -- "7. Read images" --> DiskStorage
    CPPWorker -- "8. Detect multiple faces" --> CPPWorker
    CPPWorker -- "9. Insert 128D vectors" --> Postgres

    %% Attendee Search Flow
    Attendee -- "10. Upload Selfie + Code" --> SearchController
    SearchController -- "11. Run face_extractor" --> CPPCLI
    CPPCLI -. "12. Return 128D Vector" .-> SearchController
    SearchController -- "13. Compare vectors" --> PhotoService
    PhotoService -- "14. Query batch vectors" --> Postgres
    PhotoService -. "15. Return image URLs" .-> Attendee
```

---

## Running Locally

**Dependencies:** Docker, Docker Compose, Java 17, Maven.

1. Start the PostgreSQL database and C++ worker container:
   ```bash
   docker compose up -d --build
   ```
   *(Note: The initial Docker build compiles Dlib natively and takes ~15-30 minutes).*

2. Start the Spring Boot API:
   ```bash
   cd PixelPull
   ./mvnw spring-boot:run
   ```

---

## API Reference

### 1. Photographer (Requires Auth)

**Register:**
`POST /api/auth/register`
```json
{
  "username": "admin",
  "password": "password123",
  "email": "admin@example.com"
}
```

**Login:**
`POST /api/auth/login`
Returns a JWT token.

**Upload Photos:**
`POST /api/photos/upload`
Headers: `Authorization: Bearer <token>`
Body: `multipart/form-data` with `files` (multiple images allowed).

**List Batches:**
`GET /api/photos/my-batches`
Headers: `Authorization: Bearer <token>`
Returns the `accessCode` mapping for uploaded events.

### 2. Attendee (Public)

**Search Photos:**
`POST /api/photos/search`
Body: `multipart/form-data`
- `selfie`: Image file of the attendee's face.
- `accessCode`: 6-character code provided by the photographer.

Returns a list of URLs pointing to the matched group photos.
