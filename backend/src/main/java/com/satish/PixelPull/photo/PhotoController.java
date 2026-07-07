package com.satish.pixelpull.photo;

import com.satish.pixelpull.user.User;
import com.satish.pixelpull.user.UserRepository;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.security.core.userdetails.UserDetails;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.multipart.MultipartFile;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

@Slf4j
@RestController
@RequestMapping("/api/photos")
@RequiredArgsConstructor
public class PhotoController {
    
    private final PhotoService photoService;
    private final UserRepository userRepository;
    
    @PostMapping("/upload")
    public ResponseEntity<?> uploadPhotos(@RequestParam("files") List<MultipartFile> files,
                                          @AuthenticationPrincipal UserDetails userDetails) {

        if (files == null || files.isEmpty()) {
            return ResponseEntity.badRequest().body(Map.of("message", "No files provided"));
        }

        // Validate all files are images before processing any of them
        List<String> allowedTypes = List.of("image/jpeg", "image/png", "image/jpg");
        for (MultipartFile file : files) {
            String contentType = file.getContentType();
            if (contentType == null || !allowedTypes.contains(contentType.toLowerCase())) {
                return ResponseEntity.badRequest()
                        .body(Map.of("message", "Invalid file type: '" + contentType + "'. Only JPEG and PNG images are allowed."));
            }
        }

        log.info("Received upload request with {} files", files.size());

        User user = userRepository.findByUsername(userDetails.getUsername())
                .orElseThrow(() -> new RuntimeException("User not found"));

        List<Photo> uploadedPhotos = photoService.uploadPhotos(files, user);

        Map<String, Object> response = new HashMap<>();
        response.put("message", "Upload successful");
        response.put("totalFiles", files.size());
        response.put("successfulUploads", uploadedPhotos.size());
        response.put("batchId", uploadedPhotos.isEmpty() ? null : uploadedPhotos.get(0).getBatchId());
        response.put("accessCode", uploadedPhotos.isEmpty() ? null : uploadedPhotos.get(0).getAccessCode());

        return ResponseEntity.status(HttpStatus.ACCEPTED).body(response);
    }
    
    @GetMapping("/batch/{batchId}")
    public ResponseEntity<List<Map<String, Object>>> getPhotosByBatch(@PathVariable String batchId) {
        List<Photo> photos = photoService.getPhotosByBatch(batchId);
        List<Map<String, Object>> dtos = photos.stream().map(photo -> {
            Map<String, Object> dto = new HashMap<>();
            dto.put("id", photo.getId());
            dto.put("originalFileName", photo.getOriginalFileName());
            dto.put("uploadTime", photo.getUploadTime());
            dto.put("status", photo.getStatus());
            dto.put("imageUrl", "/images/" + photo.getFilePath());
            return dto;
        }).toList();
        return ResponseEntity.ok(dtos);
    }
    
    @GetMapping("/pending-count")
    public ResponseEntity<Map<String, Long>> getPendingCount() {
        long count = photoService.getPendingCount();
        return ResponseEntity.ok(Map.of("pendingCount", count));
    }
    
    @GetMapping("/my-batches")
    public ResponseEntity<?> getMyBatches(@AuthenticationPrincipal UserDetails userDetails) {
        try {
            User user = userRepository.findByUsername(userDetails.getUsername())
                    .orElseThrow(() -> new RuntimeException("User not found"));
            
            return ResponseEntity.ok(photoService.getMyBatches(user));
        } catch (Exception e) {
            log.error("Failed to fetch batches", e);
            return ResponseEntity.status(HttpStatus.INTERNAL_SERVER_ERROR)
                               .body(Map.of("message", "Failed to fetch batches: " + e.getMessage()));
        }
    }
    
    @PostMapping("/search")
    public ResponseEntity<?> searchByFace(@RequestParam("selfie") MultipartFile selfie,
                                          @RequestParam("accessCode") String accessCode) {
        try {
            List<Photo> matches = photoService.searchByFace(selfie, accessCode);
            
            // Map to DTO so we can inject the static image URL for the frontend
            List<Map<String, Object>> mappedPhotos = matches.stream().map(photo -> {
                Map<String, Object> dto = new HashMap<>();
                dto.put("id", photo.getId());
                dto.put("originalFileName", photo.getOriginalFileName());
                dto.put("uploadTime", photo.getUploadTime());
                // The frontend will prepend the domain, e.g. http://pixelpull.com/images/batchID/file.jpg
                dto.put("imageUrl", "/images/" + photo.getFilePath());
                return dto;
            }).toList();
            
            Map<String, Object> response = new HashMap<>();
            response.put("totalMatches", mappedPhotos.size());
            response.put("photos", mappedPhotos);
            
            return ResponseEntity.ok(response);
            
        } catch (Exception e) {
            log.error("Search failed", e);
            return ResponseEntity.status(HttpStatus.INTERNAL_SERVER_ERROR)
                               .body(Map.of("message", "Search failed: " + e.getMessage()));
        }
    }
}
