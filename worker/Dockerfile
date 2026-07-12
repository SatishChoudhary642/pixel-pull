FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libpq-dev \
    libopenblas-dev \
    liblapack-dev \
    libx11-dev \
    libgtk-3-dev \
    wget \
    bzip2 \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Download Dlib models
RUN mkdir -p /app/models && cd /app/models \
    && wget http://dlib.net/files/shape_predictor_68_face_landmarks.dat.bz2 \
    && bunzip2 shape_predictor_68_face_landmarks.dat.bz2 \
    && wget http://dlib.net/files/dlib_face_recognition_resnet_model_v1.dat.bz2 \
    && bunzip2 dlib_face_recognition_resnet_model_v1.dat.bz2

# Build dlib from source
RUN git clone https://github.com/davisking/dlib.git /opt/dlib \
    && cd /opt/dlib \
    && mkdir build && cd build \
    && cmake .. \
    && cmake --build . --config Release \
    && make install

# Copy worker source (includes include/httplib.h vendored header)
COPY . /app/worker

# Build both binaries: pixelpull_worker and face_extractor
RUN cd /app/worker \
    && mkdir build && cd build \
    && cmake .. \
    && make -j4

# Expose the face_extractor HTTP microservice port
EXPOSE 8090

WORKDIR /app/worker/build

# Start both processes:
# 1. face_extractor — HTTP server on port 8090 for real-time selfie vectorization
# 2. pixelpull_worker — background polling daemon for batch processing
# We use a simple shell wrapper so both run inside the same container.
CMD sh -c "./face_extractor & ./pixelpull_worker"
