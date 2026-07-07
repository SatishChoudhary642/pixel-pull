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

    Photographer(["Photographer"])
    Attendee(["Attendee"])

    subgraph "Spring Boot API"
        AuthController["Auth Controller<br>/api/auth/login"]
        UploadController["Photo Controller<br>/api/photos/upload"]
        SearchController["Search Controller<br>/api/photos/search"]
        PhotoService["Photo Service"]
    end

    subgraph "Storage"
        Postgres[("PostgreSQL Database")]
        DiskStorage["Local Disk (/uploads)"]
    end

    subgraph "Worker"
        CPPWorker["C++ Worker Daemon"]
        CPPCLI["C++ Extractor CLI"]
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

## ML Pipeline Architecture

Flow of a single photo through the C++ Dlib worker — one stage per face found.

```mermaid
flowchart TD
    A["Group Photo\nJPEG on disk"] --> B

    subgraph LOAD ["Image Loading"]
        B["Decode JPEG\ninto RGB pixel grid"]
    end

    subgraph DETECT ["Face Detection · HOG + SVM"]
        C["Compute edge gradients\nacross image in 8x8 blocks"]
        D["Slide detection window\nat multiple scales"]
        E["SVM classifies each window\nFace / Not Face"]
        C --> D --> E
    end

    subgraph PERFACE ["Per Detected Face (runs N times)"]
        F["Landmark Detection\n68 keypoints — eyes, nose, mouth"]
        G["Face Alignment\nCrop + rotate to 150x150px\nusing keypoints as anchors"]
        H["ResNet-34 Embedding\n34 conv layers → 128 float numbers"]
        F --> G --> H
    end

    subgraph STORE ["PostgreSQL Storage"]
        I["INSERT face vector\ninto photo_faces"]
        J["UPDATE photo\nstatus = PROCESSED"]
        I --> J
    end

    B --> C
    E -->|"N face rectangles"| F
    H -->|"128D vector per face"| I
```

| Stage | What goes in | What comes out |
|-------|-------------|----------------|
| Image Load | JPEG file (~3MB) | Raw pixel grid (~6M numbers) |
| Face Detection | Full pixel grid | N bounding boxes |
| Landmark Detection | One bounding box | 68 (x,y) points |
| Face Alignment | 68 landmarks | 150×150 standardized chip |
| ResNet Embedding | 150×150 chip | 128 float numbers |
| Storage | 128 floats | 1 row in `photo_faces` per face |






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
   cd backend
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
