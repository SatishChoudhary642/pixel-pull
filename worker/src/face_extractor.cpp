// face_extractor.cpp
// This binary runs as a long-lived HTTP microservice inside the worker container.
// Spring Boot calls POST /extract-face with {"image_path": "..."} and receives
// the 128D face vector as JSON — no Docker CLI, no synchronized blocks needed.

#include "face_processor.h"
#include "httplib.h"
#include <iostream>
#include <sstream>
#include <mutex>
#include <cstdlib>

int main() {
    const int PORT = std::getenv("EXTRACTOR_PORT")
                         ? std::stoi(std::getenv("EXTRACTOR_PORT"))
                         : 8090;

    // Load the heavy Dlib models ONCE into memory at startup.
    // All subsequent HTTP requests reuse the same loaded models — no reload overhead.
    FaceProcessor processor(
        "/app/models/shape_predictor_68_face_landmarks.dat",
        "/app/models/dlib_face_recognition_resnet_model_v1.dat"
    );

    // The FaceProcessor uses Dlib internals that are NOT thread-safe,
    // so we protect concurrent HTTP requests with a mutex.
    // This is still infinitely better than the old synchronized Java approach
    // because the lock is held for milliseconds (C++ Dlib inference is fast),
    // not for seconds (Docker process boot + model loading on every request).
    std::mutex processorMutex;

    httplib::Server svr;

    // POST /extract-face
    // Request body  (plain text): /var/pixelpull/uploads/batchId/file.jpg
    // Response 200  (plain text): 0.123,0.456,...  (128 comma-separated doubles)
    // Response 400  (plain text): ERROR: <reason>
    svr.Post("/extract-face", [&](const httplib::Request& req, httplib::Response& res) {
        std::string imagePath = req.body;

        // Trim any trailing whitespace/newlines from the path
        while (!imagePath.empty() && (imagePath.back() == '\n' || imagePath.back() == '\r' || imagePath.back() == ' ')) {
            imagePath.pop_back();
        }

        if (imagePath.empty()) {
            res.status = 400;
            res.set_content("ERROR: image_path is required", "text/plain");
            return;
        }

        try {
            std::vector<std::vector<double>> faceVectors;
            {
                // Lock only for the actual Dlib inference
                std::lock_guard<std::mutex> lock(processorMutex);
                faceVectors = processor.extractFaceVector(imagePath);
            }

            if (faceVectors.empty()) {
                res.status = 400;
                res.set_content("ERROR: No faces found in the image.", "text/plain");
                return;
            }

            // Return the FIRST face as 128 comma-separated doubles
            const auto& vec = faceVectors[0];
            std::ostringstream oss;
            for (size_t i = 0; i < vec.size(); i++) {
                oss << vec[i];
                if (i < vec.size() - 1) oss << ",";
            }

            res.status = 200;
            res.set_content(oss.str(), "text/plain");

        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("ERROR: ") + e.what(), "text/plain");
        }
    });

    // Health-check so Spring Boot / Docker can verify the server is up
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("OK", "text/plain");
    });

    std::cout << "face_extractor HTTP server listening on port " << PORT << std::endl;
    svr.listen("0.0.0.0", PORT);

    return 0;
}
