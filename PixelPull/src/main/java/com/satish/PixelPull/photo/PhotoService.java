package com.satish.pixelpull.photo;

import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.web.multipart.MultipartFile;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.LocalDateTime;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;

import com.satish.pixelpull.user.User;

@Slf4j
@Service
@RequiredArgsConstructor
public class PhotoService {
    
    private final PhotoRepository photoRepository;
    private final PhotoFaceRepository photoFaceRepository;
    
    @Value("${app.upload.dir}")
    private String uploadDir;
    
    @Transactional
    public List<Photo> uploadPhotos(List<MultipartFile> files, User user) {
        String batchId = UUID.randomUUID().toString();
        // Generate a simple 6-character access code
        String accessCode = UUID.randomUUID().toString().substring(0, 6).toUpperCase();
        List<Photo> savedPhotos = new ArrayList<>();
        
        createUploadDirectoryIfNotExists();
        
        for (MultipartFile file : files) {
            try {
                String savedFilePath = saveFileToDisk(file, batchId);
                
                Photo photo = new Photo();
                photo.setUser(user);
                photo.setFilePath(savedFilePath);
                photo.setOriginalFileName(file.getOriginalFilename());
                photo.setBatchId(batchId);
                photo.setAccessCode(accessCode);
                photo.setStatus(PhotoStatus.PENDING);
                photo.setUploadTime(LocalDateTime.now());
                
                Photo saved = photoRepository.save(photo);
                savedPhotos.add(saved);
                
                log.info("Uploaded photo: {} with ID: {} and AccessCode: {}", file.getOriginalFilename(), saved.getId(), accessCode);
                
            } catch (IOException e) {
                log.error("Failed to upload file: {}", file.getOriginalFilename(), e);
            }
        }
        
        return savedPhotos;
    }
    
    private void createUploadDirectoryIfNotExists() {
        try {
            Path uploadPath = Paths.get(uploadDir);
            if (!Files.exists(uploadPath)) {
                Files.createDirectories(uploadPath);
                log.info("Created upload directory: {}", uploadDir);
            }
        } catch (IOException e) {
            throw new RuntimeException("Could not create upload directory", e);
        }
    }
    
    private String saveFileToDisk(MultipartFile file, String batchId) throws IOException {
        String batchDir = uploadDir + "/" + batchId;
        Files.createDirectories(Paths.get(batchDir));
        
        String originalFilename = file.getOriginalFilename();
        String extension = ".jpg";
        if (originalFilename != null && originalFilename.contains(".")) {
            extension = originalFilename.substring(originalFilename.lastIndexOf("."));
        }
        String uniqueFilename = UUID.randomUUID().toString() + extension;
        
        Path filePath = Paths.get(batchDir, uniqueFilename);
        Files.write(filePath, file.getBytes());
        
        // Save RELATIVE path so Docker C++ worker can read it correctly
        return batchId + "/" + uniqueFilename;
    }
    
    public List<Photo> getPhotosByBatch(String batchId) {
        return photoRepository.findByBatchId(batchId);
    }
    
    public long getPendingCount() {
        return photoRepository.countPending();
    }
    
    public List<Map<String, String>> getMyBatches(User user) {
        List<String> batchIds = photoRepository.findDistinctBatchesByUserId(user.getId());
        List<Map<String, String>> batches = new ArrayList<>();
        
        for (String batchId : batchIds) {
            List<Photo> photos = photoRepository.findByBatchId(batchId);
            if (!photos.isEmpty()) {
                Photo firstPhoto = photos.get(0);
                Map<String, String> batchInfo = new HashMap<>();
                batchInfo.put("batchId", batchId);
                batchInfo.put("accessCode", firstPhoto.getAccessCode());
                batchInfo.put("photoCount", String.valueOf(photos.size()));
                batchInfo.put("uploadTime", firstPhoto.getUploadTime().toString());
                batches.add(batchInfo);
            }
        }
        return batches;
    }
    
    public List<Photo> searchByFace(MultipartFile selfie, String accessCode) throws IOException {
        String tempBatch = "temp_search";
        String relativePath = saveFileToDisk(selfie, tempBatch);
        
        try {
            Double[] targetVector = extractFaceVectorViaCpp(relativePath);
            
            // Only fetch faces that match the access code
            List<PhotoFace> facesInBatch = photoFaceRepository.findFacesByAccessCode(accessCode);
            List<Photo> matches = new ArrayList<>();
            
            for (PhotoFace face : facesInBatch) {
                if (face.getFaceVector() != null) {
                    double distance = calculateEuclideanDistance(targetVector, face.getFaceVector());
                    if (distance < 0.6) {  // Threshold
                        matches.add(face.getPhoto());
                    }
                }
            }
            
            // Return distinct photos (in case multiple faces in one photo matched somehow)
            return matches.stream().distinct().collect(Collectors.toList());
        } finally {
            try {
                Path tempPath = Paths.get(uploadDir, relativePath);
                Files.deleteIfExists(tempPath);
            } catch (Exception e) {
                log.warn("Failed to delete temp file", e);
            }
        }
    }
    
    private double calculateEuclideanDistance(Double[] v1, Double[] v2) {
        if (v1.length != v2.length) {
            throw new IllegalArgumentException("Vectors must be same length");
        }
        double sum = 0.0;
        for (int i = 0; i < v1.length; i++) {
            double diff = v1[i] - v2[i];
            sum += diff * diff;
        }
        return Math.sqrt(sum);
    }
    
    private Double[] extractFaceVectorViaCpp(String relativePath) throws IOException {
        // We run face_extractor via docker exec since it's built in the Linux container!
        // Path inside container is /var/pixelpull/uploads/ + relativePath
        String containerPath = "/var/pixelpull/uploads/" + relativePath;
        
        // docker compose exec -T worker /app/worker/build/face_extractor containerPath
        // Note: we must run from the directory containing compose.yaml, or use docker exec with container name
        ProcessBuilder pb = new ProcessBuilder(
            "docker", "compose", "-f", "compose.yaml", "exec", "-T", "worker", 
            "/app/worker/build/face_extractor", containerPath
        );
        pb.directory(new java.io.File(".")); // Should run in PixelPull directory
        
        Process process = pb.start();
        java.io.BufferedReader reader = new java.io.BufferedReader(new java.io.InputStreamReader(process.getInputStream()));
        String line = reader.readLine();
        
        try {
            process.waitFor();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        
        if (line == null || line.trim().isEmpty()) {
            // Read error stream for debugging
            java.io.BufferedReader errReader = new java.io.BufferedReader(new java.io.InputStreamReader(process.getErrorStream()));
            String errLine = errReader.readLine();
            log.error("Extractor error: {}", errLine);
            throw new RuntimeException("No output from face extractor. Error: " + errLine);
        }
        
        String[] parts = line.split(",");
        Double[] vector = new Double[128];
        for (int i = 0; i < 128; i++) {
            vector[i] = Double.parseDouble(parts[i]);
        }
        
        return vector;
    }
}
