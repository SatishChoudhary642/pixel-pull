package com.satish.pixelpull.photo;

import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import org.springframework.stereotype.Repository;

import java.util.List;

@Repository
public interface PhotoFaceRepository extends JpaRepository<PhotoFace, Long> {
    
    // Fetch all faces belonging to a specific batch access code where the photo is PROCESSED
    @Query("SELECT pf FROM PhotoFace pf JOIN pf.photo p WHERE p.accessCode = :accessCode AND p.status = 'PROCESSED'")
    List<PhotoFace> findFacesByAccessCode(@Param("accessCode") String accessCode);
}
