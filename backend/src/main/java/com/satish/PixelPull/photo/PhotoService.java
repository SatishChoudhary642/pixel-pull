package com.satish.pixelpull.photo;

import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.web.multipart.MultipartFile;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
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

    @Value("${app.compose.dir:./}")
    private String composeDir;

    @Transactional
    public List<Photo> uploadPhotos(List<MultipartFile> files, User user) {
        String batchId = UUID.randomUUID().toString();
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

                log.info("Uploaded photo: {} with ID: {} and AccessCode: {}",
                        file.getOriginalFilename(), saved.getId(), accessCode);

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

        // Return RELATIVE path so Docker C++ worker can build its absolute path
        return batchId + "/" + uniqueFilename;
    }

    public List<Photo> getPhotosByBatch(String batchId) {
        return photoRepository.findByBatchId(batchId);
    }

    public long getPendingCount() {
        return photoRepository.countPending();
    }

    public List<Map<String, Object>> getMyBatches(User user) {
        List<String> batchIds = photoRepository.findDistinctBatchesByUserId(user.getId());
        List<Map<String, Object>> batches = new ArrayList<>();

        for (String batchId : batchIds) {
            List<Photo> photos = photoRepository.findByBatchId(batchId);
            if (!photos.isEmpty()) {
                Photo firstPhoto = photos.get(0);
                long processed = photos.stream().filter(p -> p.getStatus() == PhotoStatus.PROCESSED).count();
                long failed = photos.stream().filter(p -> p.getStatus() == PhotoStatus.FAILED).count();
                long total = photos.size();

                // A batch is done processing if all photos are either PROCESSED or FAILED
                String batchStatus;
                if (processed + failed == total) {
                    if (processed == 0) {
                        batchStatus = "FAILED"; // Every single photo failed
                    } else {
                        batchStatus = "PROCESSED"; // Finished processing (some may have failed, but it's done)
                    }
                } else {
                    batchStatus = "PROCESSING"; // Still waiting on some photos
                }

                Map<String, Object> batchInfo = new HashMap<>();
                batchInfo.put("batchId", batchId);
                batchInfo.put("accessCode", firstPhoto.getAccessCode());
                batchInfo.put("photoCount", total);
                batchInfo.put("processedCount", processed);
                batchInfo.put("uploadTime", firstPhoto.getUploadTime().toString());
                batchInfo.put("status", batchStatus);
                batches.add(batchInfo);
            }
        }
        return batches;
    }

    public List<Photo> searchByFace(MultipartFile selfie, String accessCode) throws IOException {
        // Use a unique batch ID per search so concurrent searches don't overwrite each other
        String tempBatch = "temp_search_" + UUID.randomUUID().toString().substring(0, 8);
        String relativePath = saveFileToDisk(selfie, tempBatch);

        try {
            // Extract the 128D vector from the selfie via the C++ face_extractor binary
            double[] targetVector = extractFaceVectorViaCpp(relativePath);

            // Only fetch faces that belong to this specific event (access code scoping)
            List<PhotoFace> facesInBatch = photoFaceRepository.findFacesByAccessCode(accessCode);
            List<Photo> matches = new ArrayList<>();

            for (PhotoFace face : facesInBatch) {
                if (face.getFaceVector() != null) {
                    double distance = calculateEuclideanDistance(targetVector, face.getFaceVector());
                    if (distance < 0.6) {  // 0.6 is dlib's recommended threshold for this ResNet model
                        matches.add(face.getPhoto());
                    }
                }
            }

            // Distinct — one photo can have multiple faces, avoid returning same photo twice
            return matches.stream().distinct().collect(Collectors.toList());

        } finally {
            // Always clean up temp selfie file and its directory, even if an error occurs
            try {
                Path tempFile = Paths.get(uploadDir, relativePath);
                Files.deleteIfExists(tempFile);
                Path tempDir = Paths.get(uploadDir, tempBatch);
                Files.deleteIfExists(tempDir);  // Only removes if empty (which it will be)
            } catch (Exception e) {
                log.warn("Failed to clean up temp search file", e);
            }
        }
    }

    // Euclidean distance between two 128D face vectors
    // Returns a value between 0 (identical) and ~2 (completely different)
    private double calculateEuclideanDistance(double[] v1, double[] v2) {
        if (v1.length != v2.length) {
            throw new IllegalArgumentException("Vectors must be same length: " + v1.length + " vs " + v2.length);
        }
        double sum = 0.0;
        for (int i = 0; i < v1.length; i++) {
            double diff = v1[i] - v2[i];
            sum += diff * diff;
        }
        return Math.sqrt(sum);
    }

    private Process faceExtractorProcess;
    private java.io.BufferedWriter extractorWriter;
    private BufferedReader extractorReader;

    private synchronized void startFaceExtractor() throws IOException {
        if (faceExtractorProcess != null && faceExtractorProcess.isAlive()) {
            return; // Already running
        }
        
        ProcessBuilder pb = new ProcessBuilder(
            "docker", "compose", "-f", "compose.yaml", "exec", "-i", "worker",
            "/app/worker/build/face_extractor"
        );
        pb.directory(new File(composeDir).getAbsoluteFile());
        faceExtractorProcess = pb.start();
        extractorWriter = new java.io.BufferedWriter(new java.io.OutputStreamWriter(faceExtractorProcess.getOutputStream()));
        extractorReader = new BufferedReader(new InputStreamReader(faceExtractorProcess.getInputStream()));
        
        // Wait for READY signal
        String line;
        while ((line = extractorReader.readLine()) != null) {
            if (line.equals("READY")) break;
        }
    }

    private synchronized double[] extractFaceVectorViaCpp(String relativePath) throws IOException {
        startFaceExtractor();
        String containerPath = "/var/pixelpull/uploads/" + relativePath;

        try {
            extractorWriter.write(containerPath + "\n");
            extractorWriter.flush();
            
            String line;
            while ((line = extractorReader.readLine()) != null) {
                if (line.startsWith("VECTOR:")) {
                    String vectorData = line.substring(7);
                    String[] parts = vectorData.split(",");
                    if (parts.length != 128) {
                        throw new RuntimeException("Expected 128 values from face_extractor, got: " + parts.length);
                    }
                    double[] vector = new double[128];
                    for (int i = 0; i < 128; i++) {
                        vector[i] = Double.parseDouble(parts[i].trim());
                    }
                    return vector;
                } else if (line.startsWith("ERROR:")) {
                    throw new RuntimeException("face_extractor error: " + line.substring(6));
                }
            }
        } catch (IOException e) {
            // Process probably died, clean up so it restarts next time
            faceExtractorProcess.destroy();
            throw new RuntimeException("Communication with face_extractor failed", e);
        }
        
        throw new RuntimeException("face_extractor produced no output.");
    }
}
