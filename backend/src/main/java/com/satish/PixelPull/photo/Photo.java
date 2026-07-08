package com.satish.pixelpull.photo;

import com.satish.pixelpull.user.User;
import jakarta.persistence.*;
import lombok.Data;
import java.time.LocalDateTime;

@Data
@Entity
@Table(name = "photos")
public class Photo {
    
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;
    
    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "user_id", nullable = false)
    private User user;

    @Column(name = "file_path", nullable = false, length = 500)
    private String filePath;
    
    @Column(name = "original_file_name", length = 255)
    private String originalFileName;
    
    @Column(name = "batch_id", length = 100)
    private String batchId;
    
    @Enumerated(EnumType.STRING)
    @Column(nullable = false, length = 20)
    private PhotoStatus status = PhotoStatus.PENDING;
    
    @Column(name = "access_code", length = 20)
    private String accessCode;
    
    @OneToMany(mappedBy = "photo", cascade = CascadeType.ALL, fetch = FetchType.LAZY)
    private java.util.List<PhotoFace> faces = new java.util.ArrayList<>();
    
    @Column(name = "upload_time")
    private LocalDateTime uploadTime = LocalDateTime.now();
    
    @Column(name = "processed_time")
    private LocalDateTime processedTime;
    
    @Column(name = "processing_started_at")
    private LocalDateTime processingStartedAt;
    
    @Column(name = "error_message", columnDefinition = "TEXT")
    private String errorMessage;
}
