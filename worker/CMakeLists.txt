cmake_minimum_required(VERSION 3.10)
project(pixelpull_worker)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(PostgreSQL REQUIRED)
find_package(dlib REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${PostgreSQL_INCLUDE_DIRS})

# Source files
set(SOURCES
    src/main.cpp
    src/db_manager.cpp
    src/face_processor.cpp
)

# Executable 1 (Worker)
add_executable(pixelpull_worker ${SOURCES})
target_link_libraries(pixelpull_worker 
    ${PostgreSQL_LIBRARIES}
    dlib::dlib
    pthread
)

# Executable 2 (Extractor CLI)
add_executable(face_extractor src/face_extractor.cpp src/face_processor.cpp)
target_link_libraries(face_extractor 
    dlib::dlib
    pthread
)
