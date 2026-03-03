#include "engine/core/utils/uuid_v7_generator.hpp"
#include "engine/core/utils/logger.hpp"

#include <thread>

namespace engine::core::utils {

constexpr char UuidV7Generator::HEX_CHARS[];

UuidV7Generator::UuidV7Generator() {
    std::random_device rd;
    std::seed_seq seed{
        rd(), rd(),
        static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()),
        static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count()),
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this))};
    rng_.seed(seed);
}

std::string UuidV7Generator::generate() {
    return bytes_to_string(generate_bytes());
}

UuidV7Generator::UuidBytes UuidV7Generator::generate_bytes() {
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t ts = get_current_timestamp_ms();

    if (ts == last_timestamp_ms_) {
        sequence_counter_ = static_cast<uint16_t>((sequence_counter_ + 1) & 0x0FFF);
        if (sequence_counter_ == 0) {
            do {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                ts = get_current_timestamp_ms();
            } while (ts == last_timestamp_ms_);
            sequence_counter_ = 0;
            last_timestamp_ms_ = ts;
        }
    } else {
        last_timestamp_ms_ = ts;
        sequence_counter_ = 0;
    }

    const uint64_t r1 = dist_(rng_);
    const uint64_t r2 = dist_(rng_);

    UuidBytes b{};

    // 0..47: timestamp ms
    b[0] = static_cast<uint8_t>((ts >> 40) & 0xFF);
    b[1] = static_cast<uint8_t>((ts >> 32) & 0xFF);
    b[2] = static_cast<uint8_t>((ts >> 24) & 0xFF);
    b[3] = static_cast<uint8_t>((ts >> 16) & 0xFF);
    b[4] = static_cast<uint8_t>((ts >> 8) & 0xFF);
    b[5] = static_cast<uint8_t>((ts >> 0) & 0xFF);

    // 48..63: version 7 + rand_a (sequence_counter)
    const uint16_t ver_randA = static_cast<uint16_t>((0x7 << 12) | (sequence_counter_ & 0x0FFF));
    b[6] = static_cast<uint8_t>((ver_randA >> 8) & 0xFF);
    b[7] = static_cast<uint8_t>((ver_randA >> 0) & 0xFF);

    // 64..127: variant 0b10 + rand_b
    const uint16_t var_rand14 = static_cast<uint16_t>((0x2 << 14) | (r1 & 0x3FFF));
    b[8] = static_cast<uint8_t>((var_rand14 >> 8) & 0xFF);
    b[9] = static_cast<uint8_t>((var_rand14 >> 0) & 0xFF);
    b[10] = static_cast<uint8_t>((r1 >> 16) & 0xFF);
    b[11] = static_cast<uint8_t>((r1 >> 24) & 0xFF);
    b[12] = static_cast<uint8_t>((r1 >> 32) & 0xFF);
    b[13] = static_cast<uint8_t>((r1 >> 40) & 0xFF);
    b[14] = static_cast<uint8_t>((r2 >> 0) & 0xFF);
    b[15] = static_cast<uint8_t>((r2 >> 8) & 0xFF);

    return b;
}

uint64_t UuidV7Generator::get_current_timestamp_ms() const {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

std::string UuidV7Generator::bytes_to_string(const UuidBytes& bytes) {
    std::string result;
    result.reserve(36);
    for (size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            result += '-';
        }
        result += HEX_CHARS[(bytes[i] >> 4) & 0x0F];
        result += HEX_CHARS[bytes[i] & 0x0F];
    }
    return result;
}

}  // namespace engine::core::utils
