#include "face_processor.h"
#include <iostream>

FaceProcessor::FaceProcessor(const std::string& shapePredictorPath, 
                             const std::string& faceRecModelPath) {
    // Load face detector
    detector = dlib::get_frontal_face_detector();
    
    // Load shape predictor
    dlib::deserialize(shapePredictorPath) >> sp;
    
    // Load face recognition model
    dlib::deserialize(faceRecModelPath) >> net;
    
    std::cout << "Face processor initialized" << std::endl;
}

bool FaceProcessor::loadImage(const std::string& path, dlib::matrix<dlib::rgb_pixel>& img) {
    try {
        dlib::load_image(img, path);
        return true;
    } catch (std::exception& e) {
        std::cerr << "Failed to load image: " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::vector<double>> FaceProcessor::extractFaceVector(const std::string& imagePath) {
    std::vector<std::vector<double>> allFaceVectors;
    
    // Load image
    dlib::matrix<dlib::rgb_pixel> img;
    if (!loadImage(imagePath, img)) {
        throw std::runtime_error("Could not load image");
    }
    
    // Detect faces
    std::vector<dlib::rectangle> faces = detector(img);
    
    if (faces.size() == 0) {
        throw std::runtime_error("No face detected in image");
    }
    
    std::cout << "Detected " << faces.size() << " faces in image" << std::endl;
    
    for (const auto& faceRect : faces) {
        // Get facial landmarks
        dlib::full_object_detection shape = sp(img, faceRect);
        
        // Extract face chip (normalized face image)
        dlib::matrix<dlib::rgb_pixel> face_chip;
        dlib::extract_image_chip(img, dlib::get_face_chip_details(shape, 150, 0.25), face_chip);
        
        // Get 128D descriptor
        dlib::matrix<float,0,1> face_descriptor = net(face_chip);
        
        // Convert to vector<double>
        std::vector<double> faceVector;
        faceVector.resize(face_descriptor.size());
        for (long i = 0; i < face_descriptor.size(); ++i) {
            faceVector[i] = static_cast<double>(face_descriptor(i));
        }
        
        allFaceVectors.push_back(faceVector);
    }
    
    return allFaceVectors;
}
