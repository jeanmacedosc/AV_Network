#ifndef ETHERNET_HPP
#define ETHERNET_HPP

#include <cstdint>
#include <iostream>
#include <iomanip>
#include <cstring>

// pragma push tells the compiler to pack struct members tightly,
#pragma pack(push, 1)

class Ethernet 
{
public:
    static const unsigned int ADDR_LEN = 6;
    
    inline static const uint8_t BROADCAST_ADDR[ADDR_LEN]{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    inline static const uint8_t LOCAL_ADDR[ADDR_LEN] {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    static const unsigned int MTU = 1500;

    typedef uint16_t EthType;

    /**
     * @struct Address
     * @brief Represents a 6-byte Ethernet MAC address
     */
    struct MAC {
        uint8_t addr[ADDR_LEN];

        MAC() {
            memset(addr, 0, ADDR_LEN);
        }

        MAC(const uint8_t* a) {
            memcpy(addr, a, ADDR_LEN);
        }

        bool operator==(const MAC& other) const {
            return memcmp(addr, other.addr, ADDR_LEN) == 0;
        }

        bool operator!=(const MAC& other) const {
            return !(*this == other);
        }

    };

    /**
     * @struct Header
     * @brief Represents the 14-byte Ethernet header.
     */
    struct Header {
        MAC dhost; // Destination MAC address (6 bytes)
        MAC shost; // Source MAC address (6 bytes)
        EthType type; // EtherType/Protocol (2 bytes)
    };

    /**
     * @struct Frame
     * @brief Represents a complete Ethernet frame, including header and data
     */
    struct Frame {
        Header header;
        uint8_t data[MTU];
        unsigned int data_length;
    };

};

// restore previous packing setting
#pragma pack(pop)

/**
 * @brief Easy printing of MAC addresses
 */
inline std::ostream& operator<<(std::ostream& os, const Ethernet::MAC& addr) {
    os << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < Ethernet::ADDR_LEN; ++i) {
        os << std::setw(2) << static_cast<int>(addr.addr[i]);
        if (i < Ethernet::ADDR_LEN - 1) {
            os << ":";
        }
    }
    return os << std::dec;
}


#endif // ETHERNET_HPP
