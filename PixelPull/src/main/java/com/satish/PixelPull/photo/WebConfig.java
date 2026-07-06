package com.satish.pixelpull.photo;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.servlet.config.annotation.CorsRegistry;
import org.springframework.web.servlet.config.annotation.ResourceHandlerRegistry;
import org.springframework.web.servlet.config.annotation.WebMvcConfigurer;

import java.nio.file.Path;
import java.nio.file.Paths;

@Configuration
public class WebConfig implements WebMvcConfigurer {

    @Value("${app.upload.dir}")
    private String uploadDir;

    @Override
    public void addResourceHandlers(ResourceHandlerRegistry registry) {
        Path uploadPath = Paths.get(uploadDir);
        String uploadAbsolutePath = uploadPath.toFile().getAbsolutePath();

        // Maps /images/** to the physical uploads directory so photos are accessible
        registry.addResourceHandler("/images/**")
                .addResourceLocations("file:" + uploadAbsolutePath + "/");
    }

    @Override
    public void addCorsMappings(CorsRegistry registry) {
        // Allow the separate frontend (served on any local port) to call the API
        registry.addMapping("/api/**")
                .allowedOrigins(
                    "http://localhost:5500",   // VS Code Live Server
                    "http://127.0.0.1:5500",
                    "http://localhost:3000",   // any local dev server
                    "http://127.0.0.1:3000",
                    "http://localhost:8081",   // same origin fallback
                    "null"                     // file:// protocol (opening HTML directly)
                )
                .allowedMethods("GET", "POST", "PUT", "DELETE", "OPTIONS")
                .allowedHeaders("*")
                .allowCredentials(false);
    }
}
