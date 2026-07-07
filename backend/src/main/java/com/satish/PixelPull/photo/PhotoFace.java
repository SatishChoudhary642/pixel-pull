package com.satish.pixelpull.photo;

import io.hypersistence.utils.hibernate.type.array.DoubleArrayType;
import jakarta.persistence.*;
import lombok.Data;
import org.hibernate.annotations.Type;

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

    // @Type tells Hibernate to use hypersistence's DoubleArrayType
    // which knows how to read/write PostgreSQL's double precision[] column
    @Type(DoubleArrayType.class)
    @Column(name = "face_vector", columnDefinition = "double precision[]")
    private double[] faceVector;
}
