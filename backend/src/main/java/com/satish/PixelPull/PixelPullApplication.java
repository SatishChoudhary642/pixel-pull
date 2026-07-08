package com.satish.pixelpull;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.scheduling.annotation.EnableScheduling;

@SpringBootApplication
@EnableScheduling
public class PixelPullApplication {

	public static void main(String[] args) {
		java.util.TimeZone.setDefault(java.util.TimeZone.getTimeZone("UTC"));
		SpringApplication.run(PixelPullApplication.class, args);
	}

}
