#ifndef CAN_HPP
#define CAN_HPP

#include <cstdint>
#include <array>
#include <chrono>

namespace Can {

    using Id = uint32_t;
    using Size = uint8_t;
    using Timestamp = std::chrono::time_point<std::chrono::system_clock>;

    // Standard CAN max payload is 8, CAN FD is 64. 
    // Let's reserve 64 to be safe for FD support.
    static constexpr size_t MAX_DATA_LEN = 64;

    /**
     * @struct Frame
     * @brief Represents a standardized CAN/CAN-FD frame with metadata
     */
    struct Frame {
        Id id;
        Size len;
        std::array<uint8_t, MAX_DATA_LEN> data;
        
        // Metadata for IEEE 1722
        Timestamp ingress_timestamp; 
        
        Frame() : id(0), len(0), ingress_timestamp(std::chrono::system_clock::now()) {
            data.fill(0);
        }
    };
}

#endif  // CAN_HPP