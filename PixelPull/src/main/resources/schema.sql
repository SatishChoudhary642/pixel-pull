CREATE TABLE IF NOT EXISTS users (
    id BIGSERIAL PRIMARY KEY,
    username VARCHAR(50) UNIQUE NOT NULL,
    password VARCHAR(255) NOT NULL,
    email VARCHAR(100) UNIQUE NOT NULL
);

CREATE TABLE IF NOT EXISTS photos (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT REFERENCES users(id) ON DELETE CASCADE,
    file_path VARCHAR(500) NOT NULL,
    original_file_name VARCHAR(255),
    batch_id VARCHAR(100),
    access_code VARCHAR(20),
    status VARCHAR(20) NOT NULL DEFAULT 'PENDING',
    upload_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    processed_time TIMESTAMP,
    error_message TEXT,
    
    CONSTRAINT status_check CHECK (status IN ('PENDING', 'PROCESSING', 'PROCESSED', 'FAILED', 'NO_FACE_DETECTED'))
);

CREATE INDEX IF NOT EXISTS idx_status ON photos(status);
CREATE INDEX IF NOT EXISTS idx_batch_id ON photos(batch_id);
CREATE INDEX IF NOT EXISTS idx_access_code ON photos(access_code);
CREATE INDEX IF NOT EXISTS idx_upload_time ON photos(upload_time DESC);

CREATE TABLE IF NOT EXISTS photo_faces (
    id BIGSERIAL PRIMARY KEY,
    photo_id BIGINT REFERENCES photos(id) ON DELETE CASCADE,
    face_vector DOUBLE PRECISION[]
);

CREATE INDEX IF NOT EXISTS idx_photo_faces_photo_id ON photo_faces(photo_id);
