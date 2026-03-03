/**
 * @file s3_storage_manager.cpp
 * @brief Stub S3 storage — placeholder until libcurl S3 or AWS SDK is integrated.
 */
#include "engine/infrastructure/storage/s3_storage_manager.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::infrastructure::storage {

bool S3StorageManager::initialize(const std::string& base_path) {
    endpoint_ = base_path;
    LOG_W("S3StorageManager: not yet implemented (endpoint={})", endpoint_);
    return false;
}

bool S3StorageManager::save(const std::string& dest_path, const void* data, size_t size) {
    (void)dest_path;
    (void)data;
    (void)size;
    LOG_W("S3StorageManager: save() stub — S3 not linked");
    return false;
}

bool S3StorageManager::save_file(const std::string& src_path, const std::string& dest_path) {
    (void)src_path;
    (void)dest_path;
    LOG_W("S3StorageManager: save_file() stub — S3 not linked");
    return false;
}

bool S3StorageManager::remove(const std::string& path) {
    (void)path;
    LOG_W("S3StorageManager: remove() stub — S3 not linked");
    return false;
}

}  // namespace engine::infrastructure::storage
