#include "face_processor.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: face_extractor <image_path>" << std::endl;
        return 1;
    }
    
    std::string imagePath = argv[1];
    
    try {
        FaceProcessor processor(
            "/app/models/shape_predictor_68_face_landmarks.dat",
            "/app/models/dlib_face_recognition_resnet_model_v1.dat"
        );
        
        std::vector<std::vector<double>> faceVectors = processor.extractFaceVector(imagePath);
        
        if (faceVectors.empty()) {
            throw std::runtime_error("No faces found in the image.");
        }
        
        // Print comma-separated values of the FIRST face detected
        const auto& faceVector = faceVectors[0];
        for (size_t i = 0; i < faceVector.size(); i++) {
            std::cout << faceVector[i];
            if (i < faceVector.size() - 1) std::cout << ",";
        }
        std::cout << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
