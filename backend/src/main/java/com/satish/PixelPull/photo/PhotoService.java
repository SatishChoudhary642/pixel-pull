package com.satish.pixelpull.photo;

import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.web.multipart.MultipartFile;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
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

    // URL of the face_extractor HTTP microservice running inside the worker container.
    // Defaults to localhost:8090 (the port we expose in compose.yaml).
    @Value("${app.worker.extractor.url:http://localhost:8090}")
    private String extractorUrl;

    // Java 11 HttpClient — thread-safe by design. One instance shared across
    // ALL requests. No synchronized keyword needed.
    private final HttpClient httpClient = HttpClient.newBuilder()
            .connectTimeout(Duration.ofSeconds(5))
            .build();

    // Dlib's recommended threshold for the ResNet face recognition model.
    // Distances below this value are considered the same person.
    private static final double FACE_MATCH_THRESHOLD = 0.6;

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

        // Return RELATIVE path so the C++ worker can build its container-absolute path
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
                long noFace = photos.stream().filter(p -> p.getStatus() == PhotoStatus.NO_FACE_DETECTED).count();
                long failed = photos.stream().filter(p -> p.getStatus() == PhotoStatus.FAILED).count();
                long total = photos.size();

                // A batch is done processing if all photos have reached a terminal state
                String batchStatus;
                long totalFinished = processed + noFace + failed;

                if (totalFinished == total) {
                    if (failed == total) {
                        batchStatus = "FAILED"; // Every single photo failed due to system error
                    } else if (processed == 0 && noFace > 0) {
                        batchStatus = "NO_FACES_FOUND"; // No system errors, but no faces found in batch
                    } else {
                        batchStatus = "PROCESSED"; // Finished processing
                    }
                } else {
                    batchStatus = "PROCESSING"; // Still waiting on some photos
                }

                Map<String, Object> batchInfo = new HashMap<>();
                batchInfo.put("batchId", batchId);
                batchInfo.put("accessCode", firstPhoto.getAccessCode());
                batchInfo.put("photoCount", total);
                batchInfo.put("processedCount", processed);
                batchInfo.put("noFaceCount", noFace);
                batchInfo.put("uploadTime", firstPhoto.getUploadTime().toString());
                batchInfo.put("status", batchStatus);
                batches.add(batchInfo);
            }
        }
        return batches;
    }

    public List<Photo> searchByFace(MultipartFile selfie, String accessCode) throws IOException {
        // Use a unique temp dir per search so concurrent searches never collide on disk
        String tempBatch = "temp_search_" + UUID.randomUUID().toString().substring(0, 8);
        String relativePath = saveFileToDisk(selfie, tempBatch);

        try {
            // Send the image path to the face_extractor HTTP microservice.
            // This is a non-blocking HTTP call — Spring Boot's thread pool handles
            // all concurrent requests in parallel. No synchronized, no bottleneck.
            double[] targetVector = extractFaceVectorViaHttp(relativePath);

            // Fetch all stored face vectors that belong to this event (scoped by access code)
            List<PhotoFace> facesInBatch = photoFaceRepository.findFacesByAccessCode(accessCode);
            List<Photo> matches = new ArrayList<>();

            for (PhotoFace face : facesInBatch) {
                if (face.getFaceVector() != null) {
                    double distance = calculateEuclideanDistance(targetVector, face.getFaceVector());
                    if (distance < FACE_MATCH_THRESHOLD) {
                        matches.add(face.getPhoto());
                    }
                }
            }

            // Distinct — one photo can contain multiple faces; avoid returning the same photo twice
            return matches.stream().distinct().collect(Collectors.toList());

        } finally {
            // Always clean up the temp selfie file, even if an error occurs
            try {
                Path tempFile = Paths.get(uploadDir, relativePath);
                Files.deleteIfExists(tempFile);
                Path tempDir = Paths.get(uploadDir, tempBatch);
                Files.deleteIfExists(tempDir); // Only deletes if empty (which it always will be)
            } catch (Exception e) {
                log.warn("Failed to clean up temp search file", e);
            }
        }
    }

    /**
     * Sends the image path to the face_extractor HTTP microservice and parses
     * the returned 128D face vector.
     *
     * The C++ server holds the Dlib models in memory permanently. It handles
     * requests concurrently (with an internal mutex for Dlib thread-safety).
     * This method is plain Java — no synchronized, no ProcessBuilder, no Docker CLI.
     */
    private double[] extractFaceVectorViaHttp(String relativePath) {
        // Build the absolute path as the C++ container knows it
        String containerPath = "/var/pixelpull/uploads/" + relativePath;

        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(extractorUrl + "/extract-face"))
                .timeout(Duration.ofSeconds(30))
                .POST(HttpRequest.BodyPublishers.ofString(containerPath))
                .build();

        HttpResponse<String> response;
        try {
            response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
        } catch (IOException | InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new RuntimeException(
                "Could not reach face_extractor service at " + extractorUrl +
                ". Is the worker container running?", e);
        }

        String body = response.body().trim();

        if (response.statusCode() != 200 || body.startsWith("ERROR:")) {
            // Forward the C++ error message directly so it appears in the API response
            String errorMsg = body.startsWith("ERROR:") ? body.substring(6).trim() : body;
            throw new RuntimeException("face_extractor: " + errorMsg);
        }

        // Parse 128 comma-separated doubles from the response body
        String[] parts = body.split(",");
        if (parts.length != 128) {
            throw new RuntimeException(
                "Expected 128 values from face_extractor, got: " + parts.length);
        }

        double[] vector = new double[128];
        for (int i = 0; i < 128; i++) {
            vector[i] = Double.parseDouble(parts[i].trim());
        }
        return vector;
    }

    // Euclidean distance between two 128D face vectors.
    // Returns a value between 0 (identical) and ~2 (completely different).
    private double calculateEuclideanDistance(double[] v1, double[] v2) {
        if (v1.length != v2.length) {
            throw new IllegalArgumentException(
                "Vectors must be same length: " + v1.length + " vs " + v2.length);
        }
        double sum = 0.0;
        for (int i = 0; i < v1.length; i++) {
            double diff = v1[i] - v2[i];
            sum += diff * diff;
        }
        return Math.sqrt(sum);
    }
}
