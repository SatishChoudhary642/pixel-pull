package com.satish.pixelpull.photo;

import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.time.LocalDateTime;

@Slf4j
@Service
@RequiredArgsConstructor
public class PhotoScheduler {

    private final PhotoRepository photoRepository;

    /**
     * How many minutes a photo may stay in PROCESSING before it is
     * considered stale and is reset to PENDING.
     * Configurable in application.properties via: app.stale.threshold.minutes
     */
    @Value("${app.stale.threshold.minutes:10}")
    private int staleThresholdMinutes;

    /**
     * Runs every 5 minutes.
     * Finds any photo stuck in PROCESSING for longer than staleThresholdMinutes
     * and resets it to PENDING so the worker can pick it up again.
     *
     * This handles two failure scenarios:
     *   1. The Docker worker container crashed mid-job.
     *   2. The Spring Boot server restarted while a photo was being processed.
     */
    @Scheduled(fixedRateString = "${app.stale.check.ms:300000}") // default: every 5 minutes
    @Transactional
    public void resetStaleProcessingPhotos() {
        LocalDateTime threshold = LocalDateTime.now().minusMinutes(staleThresholdMinutes);
        int count = photoRepository.resetStaleProcessingPhotos(threshold);

        if (count > 0) {
            log.warn("Stale job recovery: reset {} photo(s) from PROCESSING back to PENDING " +
                     "(were stuck for more than {} minutes)", count, staleThresholdMinutes);
        } else {
            log.debug("Stale job check: no stuck photos found.");
        }
    }
}
