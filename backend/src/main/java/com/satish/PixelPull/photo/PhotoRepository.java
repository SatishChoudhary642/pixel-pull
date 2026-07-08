package com.satish.pixelpull.photo;

import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Modifying;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import org.springframework.stereotype.Repository;
import java.time.LocalDateTime;
import java.util.List;

@Repository
public interface PhotoRepository extends JpaRepository<Photo, Long> {
    
    // Find all photos with a specific status (for worker polling)
    List<Photo> findByStatus(PhotoStatus status);
    
    @Query("SELECT DISTINCT p.batchId FROM Photo p WHERE p.user.id = :userId")
    List<String> findDistinctBatchesByUserId(@Param("userId") Long userId);
    
    @Query("SELECT p FROM Photo p WHERE p.batchId = :batchId")
    List<Photo> findByBatchId(@Param("batchId") String batchId);
    
    // Count pending photos
    @Query("SELECT COUNT(p) FROM Photo p WHERE p.status = 'PENDING'")
    long countPending();

    // Find photos stuck in PROCESSING state older than the given threshold
    @Query("SELECT p FROM Photo p WHERE p.status = com.satish.pixelpull.photo.PhotoStatus.PROCESSING AND p.processingStartedAt < :threshold")
    List<Photo> findStaleProcessingPhotos(@Param("threshold") LocalDateTime threshold);

    // Bulk-reset stale photos back to PENDING
    @Modifying
    @Query("UPDATE Photo p SET p.status = com.satish.pixelpull.photo.PhotoStatus.PENDING, p.processingStartedAt = null WHERE p.status = com.satish.pixelpull.photo.PhotoStatus.PROCESSING AND p.processingStartedAt < :threshold")
    int resetStaleProcessingPhotos(@Param("threshold") LocalDateTime threshold);
}
