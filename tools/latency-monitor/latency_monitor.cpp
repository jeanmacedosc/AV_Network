#include <iostream>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <fstream>
#include <iomanip>

// --- Configuration ---
// The interface you specified
#define DEFAULT_IFACE "enx9c956eb58a56" 

// ----------------------------------------------------------------------
// PROTOCOL DEFINITIONS (Must match Gateway packing)
// ----------------------------------------------------------------------
#pragma pack(push, 1)

namespace Ieee1722 {
    struct AvtpCommonHeader {
        uint32_t subtype_data; 
        uint64_t stream_id;
        uint32_t avtp_timestamp;
        uint32_t format_info; 
    };

    struct AcfCommonHeader {
        uint32_t msg_info; 
    };

    struct AcfCanMessage {
        uint8_t  acft;
        uint16_t acfl;
        uint8_t  flags;
        uint32_t can_id_field; 
        uint64_t ingress_timestamp; // <--- The Critical Value
        // Payload follows...
    };

    static constexpr size_t HEADER_SIZE = sizeof(AvtpCommonHeader) + sizeof(AcfCommonHeader);
}
#pragma pack(pop)

int main(int argc, char* argv[]) {
    const char* iface = (argc == 2) ? argv[1] : DEFAULT_IFACE;

    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock < 0) {
        perror("Socket creation failed (Are you root?)");
        return 1;
    }

    // Bind to specific interface
    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = if_nametoindex(iface);
    sll.sll_protocol = htons(ETH_P_ALL);

    if (sll.sll_ifindex == 0) {
        std::cerr << "Interface '" << iface << "' not found." << std::endl;
        return 1;
    }

    if (bind(sock, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        perror("Bind failed");
        close(sock);
        return 1;
    }

    std::cout << "Listening on " << iface << " for IEEE 1722 packets..." << std::endl;
    std::cout << "Writing data to 'latency_da   ta.csv'..." << std::endl;
    
    std::ofstream log_file("../../results/latency_data.csv");
    log_file << "Host_Arrival_NS,Gateway_Ingress_NS,Latency_US\n";

    uint8_t buffer[2048];

    while (true) {
        ssize_t len = recvfrom(sock, buffer, sizeof(buffer), 0, NULL, NULL);
        
        // 1. Capture Host Time IMMEDIATELY upon packet arrival
        // Must use system_clock to match PTP (Wall Time)
        auto now = std::chrono::system_clock::now();
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()
        ).count();

        if (len < 14) continue;

        struct ethhdr *eth = (struct ethhdr *)buffer;
        uint16_t proto = ntohs(eth->h_proto);
        size_t header_offset = 14;

        // 2. Handle VLAN Tagging (Common in automotive setups)
        if (proto == 0x8100) {
            if (len < 18) continue;
            uint16_t *vlan_proto_ptr = (uint16_t*)(buffer + 16);
            proto = ntohs(*vlan_proto_ptr);
            header_offset = 18;
        }

        // 3. Filter for IEEE 1722 (0x88b5)
        if (proto != 0x22F0) {
            continue;
        }

        // 4. Parse Headers
        uint8_t* ptr = buffer + header_offset;
        auto* acf  = reinterpret_cast<Ieee1722::AcfCommonHeader*>(ptr + sizeof(Ieee1722::AvtpCommonHeader));

        // 5. Extract Message Count
        uint32_t msg_info = ntohl(acf->msg_info);
        int msg_count = (msg_info >> 23) & 0x1FF;

        // 6. Iterate through batched CAN messages
        uint8_t* payload_ptr = ptr + Ieee1722::HEADER_SIZE;

        for (int i = 0; i < msg_count; i++) {
            // Safety check bounds
            if ((payload_ptr - buffer) + sizeof(Ieee1722::AcfCanMessage) > len) break;

            auto* can_msg = reinterpret_cast<Ieee1722::AcfCanMessage*>(payload_ptr);
            
            // Get size to advance pointer later
            uint16_t quadlets = ntohs(can_msg->acfl);

            // 7. Extract Gateway Timestamp (Big Endian -> Host Endian)
            uint64_t gw_ingress_ns = __builtin_bswap64(can_msg->ingress_timestamp);
            
            // 8. Filter Logic
            // If timestamp is 0, it's likely from the Injector/Test tool, not real Gateway processing.
            if (gw_ingress_ns > 0) {
                int64_t diff_ns = now_ns - gw_ingress_ns;
                double latency_us = (double)diff_ns / 1000.0;

                // Sanity Filter: 0 < Latency < 1 second
                std::cout << "Lat: " << latency_us << " us" << std::endl;
                // Allows us to skip packets sent before clocks were synced
                if (latency_us > -1000 && latency_us < 1000000) {
                    log_file << now_ns << "," << gw_ingress_ns << "," << latency_us << std::endl;
                    // Optional: Print live to verify activity
                }
            }

            // Advance pointer (Quadlets * 4 bytes)
            payload_ptr += (quadlets * 4);
        }
    }

    close(sock);
    return 0;
}