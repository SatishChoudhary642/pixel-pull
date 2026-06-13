package com.satish.pixelpull.photo;

import jakarta.persistence.*;
import lombok.Data;

@Data
@Entity
@Table(name = "photo_faces")
public class PhotoFace {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "photo_id", nullable = false)
    private Photo photo;

    @Column(name = "face_vector", columnDefinition = "double precision[]")
    private Double[] faceVector;
}
