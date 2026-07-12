cmake_minimum_required(VERSION 3.10)
project(pixelpull_worker)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(PostgreSQL REQUIRED)
find_package(dlib REQUIRED)
find_package(Threads REQUIRED)

# Include directories
# include/ holds our vendored single-header libraries (httplib.h)
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${PostgreSQL_INCLUDE_DIRS})

# Source files shared between the worker daemon and the extractor HTTP server
set(SOURCES
    src/main.cpp
    src/db_manager.cpp
    src/face_processor.cpp
)

# Executable 1: Background polling worker daemon
add_executable(pixelpull_worker ${SOURCES})
target_link_libraries(pixelpull_worker
    ${PostgreSQL_LIBRARIES}
    dlib::dlib
    Threads::Threads
)

# Executable 2: Face-extractor HTTP microservice
# Replaces the old stdin/stdout pipe approach with a real concurrent HTTP server.
# Spring Boot calls POST /extract-face instead of running docker exec.
add_executable(face_extractor src/face_extractor.cpp src/face_processor.cpp)
target_link_libraries(face_extractor
    dlib::dlib
    Threads::Threads
)
