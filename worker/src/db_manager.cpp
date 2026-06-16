 #include "db_manager.h"
#include <iostream>
#include <sstream>

DBManager::DBManager(const std::string& host, const std::string& dbname, 
                     const std::string& user, const std::string& password) {
    std::stringstream ss;
    ss << "host=" << host << " dbname=" << dbname 
       << " user=" << user << " password=" << password;
    connInfo = ss.str();
    conn = nullptr;
}

DBManager::~DBManager() {
    disconnect();
}

bool DBManager::connect() {
    conn = PQconnectdb(connInfo.c_str());
    
    if (PQstatus(conn) != CONNECTION_OK) {
        std::cerr << "Connection to database failed: " << PQerrorMessage(conn) << std::endl;
        PQfinish(conn);
        conn = nullptr;
        return false;
    }
    
    std::cout << "Connected to PostgreSQL successfully" << std::endl;
    return true;
}

void DBManager::disconnect() {
    if (conn != nullptr) {
        PQfinish(conn);
        conn = nullptr;
    }
}

std::vector<PendingPhoto> DBManager::getPendingPhotos(int limit) {
    std::vector<PendingPhoto> photos;
    
    std::string query = "SELECT id, file_path FROM photos WHERE status = 'PENDING' LIMIT " + std::to_string(limit);
    
    PGresult* res = PQexec(conn, query.c_str());
    
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "Query failed: " << PQerrorMessage(conn) << std::endl;
        PQclear(res);
        return photos;
    }
    
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++) {
        PendingPhoto photo;
        photo.id = std::stol(PQgetvalue(res, i, 0));
        photo.filePath = PQgetvalue(res, i, 1);
        photos.push_back(photo);
    }
    
    PQclear(res);
    return photos;
}

bool DBManager::updatePhotoStatus(long photoId, const std::string& status) {
    std::string query = "UPDATE photos SET status = '" + status + "' WHERE id = " + std::to_string(photoId);
    
    PGresult* res = PQexec(conn, query.c_str());
    bool success = (PQresultStatus(res) == PGRES_COMMAND_OK);
    PQclear(res);
    
    return success;
}

bool DBManager::savePhotoFaces(long photoId, const std::vector<std::vector<double>>& faceVectors) {
    // Start a transaction
    PGresult* res = PQexec(conn, "BEGIN");
    PQclear(res);
    
    // Insert all faces
    for (const auto& faceVector : faceVectors) {
        std::stringstream arrayStr;
        arrayStr << "{";
        for (size_t i = 0; i < faceVector.size(); i++) {
            arrayStr << faceVector[i];
            if (i < faceVector.size() - 1) arrayStr << ",";
        }
        arrayStr << "}";
        
        std::string query = "INSERT INTO photo_faces (photo_id, face_vector) VALUES (" + 
                            std::to_string(photoId) + ", '" + arrayStr.str() + "')";
        
        res = PQexec(conn, query.c_str());
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::cerr << "Insert face failed: " << PQerrorMessage(conn) << std::endl;
            PQclear(res);
            res = PQexec(conn, "ROLLBACK");
            PQclear(res);
            return false;
        }
        PQclear(res);
    }
    
    // Update photo status to PROCESSED
    std::string updateQuery = "UPDATE photos SET status = 'PROCESSED', processed_time = NOW() WHERE id = " + std::to_string(photoId);
    res = PQexec(conn, updateQuery.c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::cerr << "Update status failed: " << PQerrorMessage(conn) << std::endl;
        PQclear(res);
        res = PQexec(conn, "ROLLBACK");
        PQclear(res);
        return false;
    }
    PQclear(res);
    
    res = PQexec(conn, "COMMIT");
    PQclear(res);
    return true;
}

bool DBManager::updatePhotoError(long photoId, const std::string& errorMsg) {
    // Escape single quotes
    std::string escapedMsg = errorMsg;
    size_t pos = 0;
    while ((pos = escapedMsg.find("'", pos)) != std::string::npos) {
        escapedMsg.replace(pos, 1, "''");
        pos += 2;
    }
    
    std::string query = "UPDATE photos SET status = 'FAILED', error_message = '" + 
                       escapedMsg + "' WHERE id = " + std::to_string(photoId);
    
    PGresult* res = PQexec(conn, query.c_str());
    bool success = (PQresultStatus(res) == PGRES_COMMAND_OK);
    PQclear(res);
    
    return success;
}
