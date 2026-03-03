/**
 * @file local_storage_manager.cpp
 * @brief Filesystem-based storage using std::filesystem.
 */
#include "engine/infrastructure/storage/local_storage_manager.hpp"
#include "engine/core/utils/logger.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace engine::infrastructure::storage {

bool LocalStorageManager::initialize(const std::string& base_path) {
    base_dir_ = base_path;
    try {
        if (!fs::exists(base_dir_)) {
            fs::create_directories(base_dir_);
            LOG_I("LocalStorage: created base directory '{}'", base_dir_);
        }
        return true;
    } catch (const fs::filesystem_error& e) {
        LOG_E("LocalStorage: failed to create '{}': {}", base_dir_, e.what());
        return false;
    }
}

bool LocalStorageManager::save(const std::string& dest_path, const void* data, size_t size) {
    try {
        auto path = full_path(dest_path);
        auto parent = fs::path(path).parent_path();
        if (!parent.empty() && !fs::exists(parent)) {
            fs::create_directories(parent);
        }

        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) {
            LOG_E("LocalStorage: cannot open '{}' for writing", path);
            return false;
        }
        ofs.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        return true;

    } catch (const std::exception& e) {
        LOG_E("LocalStorage: save('{}') failed: {}", dest_path, e.what());
        return false;
    }
}

bool LocalStorageManager::save_file(const std::string& src_path, const std::string& dest_path) {
    try {
        auto dest = full_path(dest_path);
        auto parent = fs::path(dest).parent_path();
        if (!parent.empty() && !fs::exists(parent)) {
            fs::create_directories(parent);
        }
        fs::copy_file(src_path, dest, fs::copy_options::overwrite_existing);
        return true;

    } catch (const fs::filesystem_error& e) {
        LOG_E("LocalStorage: save_file('{}' → '{}') failed: {}", src_path, dest_path, e.what());
        return false;
    }
}

bool LocalStorageManager::remove(const std::string& path) {
    try {
        auto fpath = full_path(path);
        if (fs::exists(fpath)) {
            fs::remove_all(fpath);
        }
        return true;
    } catch (const fs::filesystem_error& e) {
        LOG_E("LocalStorage: remove('{}') failed: {}", path, e.what());
        return false;
    }
}

std::string LocalStorageManager::full_path(const std::string& relative) const {
    return (fs::path(base_dir_) / relative).string();
}

}  // namespace engine::infrastructure::storage
