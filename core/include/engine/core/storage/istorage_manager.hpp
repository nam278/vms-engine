#pragma once
#include <string>
#include <cstddef>

namespace engine::core::storage {

/**
 * @brief Manages saving/loading files (local filesystem, S3, etc.).
 */
class IStorageManager {
   public:
    virtual ~IStorageManager() = default;
    virtual bool initialize(const std::string& base_path) = 0;
    virtual bool save(const std::string& dest_path, const void* data, size_t size) = 0;
    virtual bool save_file(const std::string& src_path, const std::string& dest_path) = 0;
    virtual bool remove(const std::string& path) = 0;
};

}  // namespace engine::core::storage
