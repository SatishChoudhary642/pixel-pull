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

This diagram shows the internal flow of a single photo through the C++ Dlib ML worker — from raw JPEG on disk to 128-dimensional face vectors stored in PostgreSQL.

```mermaid
flowchart TD
    A["JPEG File on Disk\n/var/pixelpull/uploads/batch/photo.jpg"] --> B

    subgraph INIT ["Worker Startup (Once)"]
        M1["Load HOG Face Detector\ndlib::get_frontal_face_detector()"] 
        M2["Deserialize Shape Predictor\nshape_predictor_68_face_landmarks.dat"]
        M3["Deserialize ResNet-34\ndlib_face_recognition_resnet_model_v1.dat"]
    end

    subgraph LOAD ["Stage 1 - Image Loading"]
        B["dlib::load_image()\nDecode JPEG into raw pixel grid\n1920x1080 x 3 channels = 6.2M numbers in RAM"]
    end

    subgraph DETECT ["Stage 2 - Face Detection (HOG)"]
        C["Divide image into 8x8 pixel blocks"]
        D["Compute gradient direction per block\nHistogram of Oriented Gradients"]
        E["Slide 64x128 window across image\nat multiple scales"]
        F["SVM Classifier per window\nFace or Not Face"]
        G["Output: N bounding rectangles\none per detected face"]
        C --> D --> E --> F --> G
    end

    subgraph LANDMARK ["Stage 3 - Landmark Detection (per face)"]
        H["sp(img, faceRect)\nShape Predictor runs on each\nbounding box region"]
        I["Output: 68 x,y coordinate pairs\nJawline, eyebrows, nose, eyes, mouth"]
        H --> I
    end

    subgraph ALIGN ["Stage 4 - Face Alignment (per face)"]
        J["get_face_chip_details(shape, 150px, 0.25 padding)\nCompute rotation + scale transform\nusing landmark anchor points"]
        K["extract_image_chip()\nWarp and crop face to\nstandardized 150x150 pixel chip\nEyes always horizontally level"]
        J --> K
    end

    subgraph EMBED ["Stage 5 - Embedding Generation (per face)"]
        L["ResNet-34 Forward Pass\n150x150 face chip through\n34 convolutional layers"]
        N["alevel4: 32 filters\nalevel3: 64 filters\nalevel2: 128 filters\nalevel1: 256 filters\nalevel0: 256 filters"]
        O["fc_no_bias layer\nCompress to 128 values"]
        P["Output: 128D float vector\nMathematical face fingerprint"]
        L --> N --> O --> P
    end

    subgraph STORE ["Stage 6 - Database Storage"]
        Q["BEGIN transaction"]
        R["INSERT INTO photo_faces\nphoto_id, face_vector for each face"]
        S["UPDATE photos SET\nstatus=PROCESSED"]
        T["COMMIT"]
        Q --> R --> S --> T
    end

    B --> C
    G --> H
    I --> J
    K --> L
    P --> Q

    INIT -.->|models already in RAM| DETECT
    INIT -.->|models already in RAM| LANDMARK
    INIT -.->|models already in RAM| EMBED
```

### Key Numbers at Each Stage

| Stage | Input | Output |
|-------|-------|--------|
| Image Load | JPEG file (~3MB compressed) | Raw pixel grid (6.2M numbers) |
| Face Detection | Full image grid | N bounding rectangles |
| Landmark Detection | One face rectangle | 68 (x,y) coordinate pairs |
| Face Alignment | 68 landmarks | 150×150 pixel chip |
| Embedding | 150×150 chip | 128 float numbers |
| Storage | 128 floats | 1 row in `photo_faces` table |

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
