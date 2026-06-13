#ifndef DB_MANAGER_H
#define DB_MANAGER_H

#include <libpq-fe.h>
#include <string>
#include <vector>
#include <memory>

struct PendingPhoto {
    long id;
    std::string filePath;
};

class DBManager {
private:
    PGconn* conn;
    std::string connInfo;
    
public:
    DBManager(const std::string& host, const std::string& dbname, 
              const std::string& user, const std::string& password);
    ~DBManager();
    
    bool connect();
    void disconnect();
    
    std::vector<PendingPhoto> getPendingPhotos(int limit = 10);
    bool updatePhotoStatus(long photoId, const std::string& status);
    bool savePhotoFaces(long photoId, const std::vector<std::vector<double>>& faceVectors);
    bool updatePhotoError(long photoId, const std::string& errorMsg);
};

#endif
