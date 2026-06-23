#include "db_manager.h"
#include "face_processor.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>

int main() {
    const char* dbHost = std::getenv("DB_HOST") ? std::getenv("DB_HOST") : "localhost";
    const char* dbName = std::getenv("DB_NAME") ? std::getenv("DB_NAME") : "mydatabase";
    const char* dbUser = std::getenv("DB_USER") ? std::getenv("DB_USER") : "myuser";
    const char* dbPass = std::getenv("DB_PASS") ? std::getenv("DB_PASS") : "secret";

    // Database configuration
    DBManager db(dbHost, dbName, dbUser, dbPass);
    
    if (!db.connect()) {
        std::cerr << "Failed to connect to database. Exiting." << std::endl;
        return 1;
    }
    
    // Initialize face processor
    FaceProcessor processor(
        "/app/models/shape_predictor_68_face_landmarks.dat",
        "/app/models/dlib_face_recognition_resnet_model_v1.dat"
    );
    
    std::cout << "Worker started. Polling for pending photos..." << std::endl;
    
    // Main processing loop
    while (true) {
        // Fetch pending photos
        std::vector<PendingPhoto> pendingPhotos = db.getPendingPhotos(5);
        
        if (pendingPhotos.empty()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }
        
        std::cout << "Found " << pendingPhotos.size() << " pending photos" << std::endl;
        
        // Process each photo
        for (const auto& photo : pendingPhotos) {
            std::cout << "Processing photo ID: " << photo.id << std::endl;
            // Status already set to PROCESSING atomically in getPendingPhotos()
            try {
                // Extract face vectors (prepend container upload dir to relative path)
                std::string fullPath = "/var/pixelpull/uploads/" + photo.filePath;
                std::vector<std::vector<double>> faceVectors = processor.extractFaceVector(fullPath);
                
                // Update database with vectors
                if (db.savePhotoFaces(photo.id, faceVectors)) {
                    std::cout << "Successfully processed photo ID " << photo.id 
                              << " with " << faceVectors.size() << " faces" << std::endl;
                } else {
                    std::cerr << "Failed to save face vectors for photo ID " << photo.id << std::endl;
                    db.updatePhotoError(photo.id, "Database update failed");
                }
                
            } catch (const std::exception& e) {
                std::cerr << "Error processing photo ID " << photo.id << ": " << e.what() << std::endl;
                db.updatePhotoError(photo.id, e.what());
            }
        }
        
        // Small delay before next poll
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    return 0;
}
