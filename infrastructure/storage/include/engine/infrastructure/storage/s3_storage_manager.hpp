#pragma once
/**
 * @file s3_storage_manager.hpp
 * @brief S3/MinIO storage adapter implementing IStorageManager (stub).
 *
 * Placeholder for S3-compatible object storage via libcurl.
 * Currently logs a warning and returns false.
 */
#include "engine/core/storage/istorage_manager.hpp"
#include <string>

namespace engine::infrastructure::storage {

/**
 * @brief Stub S3 storage — returns false until AWS SDK or libcurl S3 is configured.
 */
class S3StorageManager : public engine::core::storage::IStorageManager {
   public:
    S3StorageManager() = default;
    ~S3StorageManager() override = default;

    bool initialize(const std::string& base_path) override;
    bool save(const std::string& dest_path, const void* data, size_t size) override;
    bool save_file(const std::string& src_path, const std::string& dest_path) override;
    bool remove(const std::string& path) override;

   private:
    std::string endpoint_;
    std::string bucket_;
    std::string access_key_;
    std::string secret_key_;
};

}  // namespace engine::infrastructure::storage
