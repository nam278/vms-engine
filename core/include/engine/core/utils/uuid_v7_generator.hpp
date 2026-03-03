#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
#include <random>
#include <string>

namespace engine::core::utils {

/**
 * @brief Thread-safe UUIDv7 generator (RFC 9562).
 *
 * UUID v7 layout (big-endian bytes):
 *   - bits  0..47 : Unix timestamp in milliseconds since Unix epoch
 *   - bits 48..51 : version = 0b0111 (7)
 *   - bits 52..63 : rand_a (12 bits) — counter for monotonic ordering within same ms
 *   - bits 64..65 : variant = 0b10 (RFC 4122 variant)
 *   - bits 66..127: rand_b (62 bits) — random
 *
 * Output format: xxxxxxxx-xxxx-7xxx-xxxx-xxxxxxxxxxxx
 */
class UuidV7Generator {
   public:
    using UuidBytes = std::array<uint8_t, 16>;

    UuidV7Generator();
    ~UuidV7Generator() = default;

    UuidV7Generator(const UuidV7Generator&) = delete;
    UuidV7Generator& operator=(const UuidV7Generator&) = delete;
    UuidV7Generator(UuidV7Generator&&) = delete;
    UuidV7Generator& operator=(UuidV7Generator&&) = delete;

    /** @brief Generate a new UUIDv7 string. */
    std::string generate();

    /** @brief Generate a new UUIDv7 as raw 16-byte array. */
    UuidBytes generate_bytes();

    /** @brief Convert UUID bytes to standard string format. */
    static std::string bytes_to_string(const UuidBytes& bytes);

   private:
    std::mutex mutex_;
    std::mt19937_64 rng_;
    std::uniform_int_distribution<uint64_t> dist_{0, std::numeric_limits<uint64_t>::max()};

    uint64_t last_timestamp_ms_{0};
    uint16_t sequence_counter_{0};

    uint64_t get_current_timestamp_ms() const;
    static constexpr char HEX_CHARS[] = "0123456789abcdef";
};

}  // namespace engine::core::utils
