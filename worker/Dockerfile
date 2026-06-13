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

# Download models
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

# Copy worker source
COPY . /app/worker

# Build worker
RUN cd /app/worker \
    && mkdir build && cd build \
    && cmake .. \
    && make -j4

# Command
WORKDIR /app/worker/build
CMD ["./pixelpull_worker"]
