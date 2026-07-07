#include "face_processor.h"
#include <iostream>

int main() {
    try {
        FaceProcessor processor(
            "/app/models/shape_predictor_68_face_landmarks.dat",
            "/app/models/dlib_face_recognition_resnet_model_v1.dat"
        );
        
        std::cout << "READY" << std::endl;
        
        std::string imagePath;
        while (std::getline(std::cin, imagePath)) {
            if (imagePath.empty()) continue;
            if (imagePath == "exit" || imagePath == "quit") break;
            
            try {
                std::vector<std::vector<double>> faceVectors = processor.extractFaceVector(imagePath);
                
                if (faceVectors.empty()) {
                    std::cout << "ERROR: No faces found in the image." << std::endl;
                    continue;
                }
                
                // Print comma-separated values of the FIRST face detected
                const auto& faceVector = faceVectors[0];
                std::cout << "VECTOR:";
                for (size_t i = 0; i < faceVector.size(); i++) {
                    std::cout << faceVector[i];
                    if (i < faceVector.size() - 1) std::cout << ",";
                }
                std::cout << std::endl;
                
            } catch (const std::exception& e) {
                std::cout << "ERROR: " << e.what() << std::endl;
            }
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }
}
