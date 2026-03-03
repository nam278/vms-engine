#pragma once
/**
 * @file local_storage_manager.hpp
 * @brief Local filesystem storage implementing IStorageManager.
 */
#include "engine/core/storage/istorage_manager.hpp"
#include <string>

namespace engine::infrastructure::storage {

/**
 * @brief Saves files to the local filesystem using std::filesystem.
 *
 * Creates directories as needed. All paths are relative to base_dir.
 */
class LocalStorageManager : public engine::core::storage::IStorageManager {
   public:
    LocalStorageManager() = default;
    ~LocalStorageManager() override = default;

    bool initialize(const std::string& base_path) override;

    bool save(const std::string& dest_path, const void* data, size_t size) override;

    bool save_file(const std::string& src_path, const std::string& dest_path) override;

    bool remove(const std::string& path) override;

   private:
    std::string base_dir_;

    /** @brief Build full path from base_dir_ + relative. */
    std::string full_path(const std::string& relative) const;
};

}  // namespace engine::infrastructure::storage
