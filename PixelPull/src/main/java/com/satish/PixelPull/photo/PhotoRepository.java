package com.satish.pixelpull.photo;

import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import org.springframework.stereotype.Repository;
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
}
