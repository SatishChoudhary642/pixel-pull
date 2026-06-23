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

    // Runs the face_extractor C++ binary inside the running Docker worker container
    // The binary loads the image, runs HOG + ResNet-34, and prints 128 comma-separated floats to stdout
    private double[] extractFaceVectorViaCpp(String relativePath) throws IOException {
        String containerPath = "/var/pixelpull/uploads/" + relativePath;

        // docker compose exec -T worker /app/worker/build/face_extractor <path>
        // -T disables pseudo-TTY allocation so stdout is clean (no ANSI codes)
        ProcessBuilder pb = new ProcessBuilder(
            "docker", "compose", "-f", "compose.yaml", "exec", "-T", "worker",
            "/app/worker/build/face_extractor", containerPath
        );
        // Run from the directory that contains compose.yaml
        pb.directory(new File("."));

        Process process = pb.start();

        // Read stdout — face_extractor prints exactly one line: "0.0234,-0.0891,..."
        BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()));
        String line = reader.readLine();

        try {
            int exitCode = process.waitFor();
            if (exitCode != 0) {
                BufferedReader errReader = new BufferedReader(new InputStreamReader(process.getErrorStream()));
                String err = errReader.lines().collect(Collectors.joining("\n"));
                log.error("face_extractor exited with code {}: {}", exitCode, err);
                throw new RuntimeException("face_extractor failed (exit " + exitCode + "): " + err);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("Interrupted while waiting for face extractor", e);
        }

        if (line == null || line.trim().isEmpty()) {
            throw new RuntimeException("face_extractor produced no output. Is the image a valid photo with a visible face?");
        }

        // Parse the 128 comma-separated values into a primitive double array
        String[] parts = line.trim().split(",");
        if (parts.length != 128) {
            throw new RuntimeException("Expected 128 values from face_extractor, got: " + parts.length);
        }
        double[] vector = new double[128];
        for (int i = 0; i < 128; i++) {
            vector[i] = Double.parseDouble(parts[i].trim());
        }
        return vector;
    }
}
