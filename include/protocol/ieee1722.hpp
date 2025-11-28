#ifndef IEEE1722_HPP
#define IEEE1722_HPP

#include <cstdint>
#include <array>
#include "can.hpp"

#pragma pack(push, 1) // Force tight packing

namespace Ieee1722 {
    // 1. AVTP Common Header (24 bytes typically for ACF)
    /**
     * @details AVTP Common Header fields:
     * subtype (8 bits) -> CAN Control Data (ACF) is 0x03
     * stream_valid (1 bit)
     * version (3 bits)
     * mr (1 bit) -> Media Restart
     * r (1 bit) -> Reserved
     * gv (1 bit) -> Gateway info. '1' when it is the Gateway sending
     * tv (1 bit) -> Timestamp valid
     * tu (1 bit) -> Timestamp uncertain. '0' = locked w/ gPTP; '1' = usynced
     * stream_id (64 bits) -> Zone ID (Usually MAC + Unique ID)
     * avtp_timestamp (32 bits) -> Presentation time
     * stream_data_length (16 bits) -> The payload size following this header
     * acfv (4 bits) -> ACF version = zero
     * r (12 bits) -> Reserved
     */
    struct AvtpCommonHeader {
        // Bitfields are tricky in C++ due to endianness. 
        // For a raw struct implementation, we often use uint32_t words 
        // and mask/shift, but here is the logical layout.
        
        // Word 0
        uint32_t subtype_data; 
        // Contains: subtype(8), sv(1), ver(3), mr(1), r(1), gv(1), tv(1), seq(8), tu(1), r(7)
        // You will construct this integer manually using bitwise OR.
        
        // Word 1-2
        uint64_t stream_id; // The unique ID of your Gateway

        // Word 3
        uint32_t avtp_timestamp; // The Presentation Time (Deadline)

        // Word 4
        uint32_t format_info; 
        // Contains: stream_data_len(16), acf_msg_type(8), acf_ver(4), r(4)
    };

    /**
     * @details ACF Common Header fields (32 bits):
     * msg_count (9 bits) -> How many CAN frames are in the frame
     * pad_length (2 bits) -> Bytes zeroeds at the end to maintain 32-bits alignment
     * r (21 bits) -> Reserved
     */
    struct AcfCommonHeader {
        uint32_t msg_info; 
        // Contains: msg_count(9), pad_length(2), r(21)
    };

    /**
     * @details ACF CAN Message Wrapper (32 bits) fields:
     * Flags:
     * mtv (1 bit) -> Message Time Valid
     * r (3 bits) -> Reserved bits
     * mbz (4 bits) -> Must Be Zero. For alingment
     * acft (8 bits) -> ACF type. CAN brief = 0x04
     * acfl (16 bits) -> Length of this CAN frame (header + data) in 32-bit word
     */
    struct AcfCanMessage {
        // Word 0 -> ACF CAN Message Header
        uint8_t flags;  // mtv, r, mbz
        uint8_t  acft;      // Type (0x04 for CAN)
        uint16_t acfl;      // Length in Quadlets (4-byte words)

        // Word 1
        uint32_t can_id_field; 
        // Contains: pad(3), rtr(1), eff(1), brs(1), fdf(1), esi(1), bus_id(5), r(3), CAN_ID(11 or 29)

        // Word 2-3
        uint64_t ingress_timestamp; // When it hit the VisionFive 2

        // Payload follows dynamically... but we can define a max container
        uint8_t payload[64]; 
    };

    static constexpr size_t HEADER_SIZE = sizeof(AvtpCommonHeader) + sizeof(AcfCommonHeader);   
}

#pragma pack(pop)

#endif // IEEE1722_HPP