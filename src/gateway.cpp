#include "gateway.hpp"
#include <cstring>
#include <arpa/inet.h>
#include <iomanip>

Gateway* Gateway::instance = nullptr;

Gateway::Gateway() : _running(false) {
    configure_routes();
}

Gateway* Gateway::get_instance() {
    if (instance == nullptr) {
        instance = new Gateway();
    }
    return instance;
}

void Gateway::configure_routes() {
    _priority_map_ranges.push_back({0x000, 0x100, Priority::CRITICAL});
    _priority_map_ranges.push_back({0x101, 0x400, Priority::HIGH});
}

void Gateway::start() {
    if (!_running) {
        _log_file.open("gateway_latency.csv", std::ios::out | std::ios::trunc);
        if (_log_file.is_open()) {
            _log_file << "CAN_ID,Priority,Ingress_NS,Egress_NS,Latency_US\n";
        }

        _e2e_log_file.open("e2e_latency.csv", std::ios::out | std::ios::trunc);
        if (_e2e_log_file.is_open()) {
            _e2e_log_file << "CAN_ID,Priority,TX_Ingress_NS,RX_Egress_NS,E2E_Latency_US\n";
        }

        // Packet loss log — only meaningful on Node 1 (Receiver)
        _loss_log_file.open("packet_loss.csv", std::ios::out | std::ios::trunc);
        if (_loss_log_file.is_open()) {
            _loss_log_file << "RX_ETH_Count,Expected_Seq,Received_Seq,Lost_In_Gap,Total_Lost\n";
        }

        _running = true;
        _worker_thread = std::thread(&Gateway::egress_loop, this);
    }
}

void Gateway::stop() {
    _running = false;
    _cv.notify_all();
    if (_worker_thread.joinable()) {
        _worker_thread.join();
    }

    if (_log_file.is_open()) {
        _log_file.close();
    }

    if (_e2e_log_file.is_open()) {
        _e2e_log_file.close();
    }

    if (_loss_log_file.is_open()) {
        // Print final summary to console before closing
        std::cout << "[LOSS SUMMARY] Total ETH packets received: " << _total_rx_eth
                  << " | Total CAN frames lost: " << _total_lost << "\n";
        _loss_log_file.close();
    }
}

void Gateway::log_latency(Can::Id id, std::chrono::system_clock::time_point ingress) {
    if (!_log_file.is_open()) return;

    auto now = std::chrono::system_clock::now();
    auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(now - ingress).count();
    auto ingress_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(ingress.time_since_epoch()).count();
    auto egress_ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    int prio = (int)resolve_priority(id);

    _log_file << "0x" << std::hex << id << std::dec << ","
              << prio << ","
              << ingress_ns << ","
              << egress_ns << ","
              << latency_us << "\n";
}

/**
 * @brief Logs End-to-End latency: time from CAN ingress at Node 0 (TX)
 *        to CAN egress at Node 1 (RX). Requires PTP clock synchronization
 *        between the two nodes so CLOCK_REALTIME is shared.
 */
void Gateway::log_e2e_latency(Can::Id id, std::chrono::system_clock::time_point tx_ingress, std::chrono::system_clock::time_point rx_eth_arrival) {
    if (!_e2e_log_file.is_open()) return;

    auto e2e_us = std::chrono::duration_cast<std::chrono::microseconds>(rx_eth_arrival - tx_ingress).count();
    auto tx_ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(tx_ingress.time_since_epoch()).count();
    auto rx_ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(rx_eth_arrival.time_since_epoch()).count();
    int prio = (int)resolve_priority(id);

    _e2e_log_file << "0x" << std::hex << id << std::dec << ","
                  << prio << ","
                  << tx_ns << ","
                  << rx_ns << ","
                  << e2e_us << "\n";
}

/**
 * @brief Logs a packet loss event detected via IEEE 1722 sequence number gap.
 *        Called on Node 1 (Receiver) whenever received_seq != expected_seq.
 *        Loss count is the number of ETH packets (bursts) dropped, NOT
 *        individual CAN frames (one ETH packet may carry up to MAX_BATCH_SIZE frames).
 */
void Gateway::log_packet_loss(uint8_t expected, uint8_t received, uint8_t lost) {
    if (_loss_log_file.is_open()) {
        _loss_log_file << _total_rx_eth << ","
                       << static_cast<int>(expected) << ","
                       << static_cast<int>(received) << ","
                       << static_cast<int>(lost) << ","
                       << _total_lost << "\n";
        _loss_log_file.flush(); // ensure it's written immediately
    }
    std::cerr << "[LOSS] ETH#" << _total_rx_eth
              << " | Seq esperado: "  << static_cast<int>(expected)
              << " | Recebido: "      << static_cast<int>(received)
              << " | Pacotes perdidos nesse gap: " << static_cast<int>(lost)
              << " | Total perdidos: " << _total_lost << "\n";
}


// ----------------------------------------------------------------------
// Gateway RX Logic
// ----------------------------------------------------------------------

/**
 * @brief CAN interfaces -> Gateway
 */
void Gateway::update(CanTxSubject* obs, Can::Frame* frame) {
    if (!frame) return;

    // ingress Timestamp
    frame->ingress_timestamp = std::chrono::system_clock::now();

    // priority and queueing
    // priority resolution (kept only for CSV logging purposes)
    Priority prio = resolve_priority(frame->id);
    
    bool queued = false;
    if (prio == Priority::CRITICAL) {
        queued = _critical_queue.push(*frame);
        // Only critical frames trigger immediate egress
        if (queued) {
            _cv.notify_one();
        }
    } else if (prio == Priority::HIGH) {
        queued = _high_queue.push(*frame);
    } else {
        queued = _low_queue.push(*frame);
    }
}

/**
 * @brief Eth interfaces -> Gateway
 */
void Gateway::update(EthTxSubject* obs, Ethernet::EthType c, Ethernet::Frame* frame) {
    if (c != GW_PROTOCOL) return;

    // Capture the exact moment the Ethernet frame hits the Gateway, before any unpacking.
    // This isolates the pure CAN->ETH->Network->ETH latency.
    auto eth_arrival_time = std::chrono::system_clock::now();

    uint8_t* buffer = reinterpret_cast<uint8_t*>(frame);
    size_t eth_header_len = sizeof(Ethernet::Header);
    
    // parsing AVTP Common Header
    auto* avtp = reinterpret_cast<Ieee1722::AvtpCommonHeader*>(buffer + eth_header_len);
    
    // checking subtype (must be 0x03 for ACF)
    uint32_t subtype_data = ntohl(avtp->subtype_data);
    uint8_t subtype = (subtype_data >> 24) & 0xFF;
    
    if (subtype != 0x03) {
        // std::cerr << "Gateway: Drop non-ACF packet" << std::endl;
        return;
    }

    // ---- Packet Loss Detection (Node 1 / Receiver only) ----
    // Extract the 1-byte sequence counter from the AVTP header.
    // The seq field is the lowest byte of the subtype_data word.
    uint8_t received_seq = static_cast<uint8_t>(ntohl(avtp->subtype_data) & 0xFF);
    _total_rx_eth++;

    if (!_seq_initialized) {
        // First packet: just record the starting sequence, nothing to compare yet.
        _seq_initialized = true;
    } else {
        // Unsigned subtraction handles the 0→255 wrap-around correctly.
        uint8_t gap = received_seq - _expected_seq;
        if (gap > 1) {
            uint8_t lost = gap - 1;
            _total_lost += lost;
            log_packet_loss(_expected_seq, received_seq, lost);
        }
    }
    _expected_seq = static_cast<uint8_t>(received_seq + 1);
    // ---------------------------------------------------------

    // parsing ACF Common Header
    auto* acf = reinterpret_cast<Ieee1722::AcfCommonHeader*>(buffer + eth_header_len + sizeof(Ieee1722::AvtpCommonHeader));
    
    // extracting message count (first 9 bits of msg_info)
    uint32_t msg_info = ntohl(acf->msg_info);
    int msg_count = (msg_info >> 23) & 0x1FF;

    // to iterate and extract CAN frames
    uint8_t* payload_ptr = buffer + eth_header_len + Ieee1722::HEADER_SIZE;
    
    // bound limit check
    uint8_t* end_ptr = buffer + frame->data_length;

    for (int i = 0; i < msg_count; i++) {
        if (payload_ptr >= end_ptr) break;

        Can::Frame can_frame;
        size_t bytes_read = deserialize_can_message(payload_ptr, can_frame);
        
        if (bytes_read == 0) break;

        // Log E2E latency: tx_ingress came from Node 0, rx_eth_arrival is Node 1 reception.
        // Requires PTP-synchronized CLOCK_REALTIME between both nodes.
        log_e2e_latency(can_frame.id, can_frame.ingress_timestamp, eth_arrival_time);

        // notifies all attached CAN interfaces
        this->CanTxSubject::notify(&can_frame);

        payload_ptr += bytes_read;
    }
}

/**
 * @brief Returns the configured Priority for a specified CAN Id
 */
Gateway::Priority Gateway::resolve_priority(Can::Id id) {
    for (const auto& range : _priority_map_ranges) {
        if (id >= range.start && id <= range.end) {
            return range.p;
        }
    }
    return Priority::LOW;
}

// ----------------------------------------------------------------------
// Egress Logic (The Worker)
// ----------------------------------------------------------------------

/**
 * @brief Thread to dispatch buffers to being sent in ETH interfaces (CAN -> 10BASE-T1S)
 */
void Gateway::egress_loop() {
    // 1518 bytes covers standard Ethernet MTU + headers
    uint8_t eth_buffer[1518]; 

    while (_running) {
        std::unique_lock<std::mutex> lock(_cv_mutex);
        
        // waits for the configured MAX_LATENCY_WINDOW or till a notification for CRITICAL CAN comes
        _cv.wait_for(lock, MAX_LATENCY_WINDOW);

        if (!_running) break;

        // check if there is anything to send
        if (!_critical_queue.empty() || !_high_queue.empty() || !_low_queue.empty()) {
            pack_and_send_burst(eth_buffer);
        }
    }
}

void Gateway::pack_and_send_burst(uint8_t* buffer) {
    // AVTP Header starts right after the Ethernet header
    size_t eth_header_len = sizeof(Ethernet::Header);
    
    auto* avtp = reinterpret_cast<Ieee1722::AvtpCommonHeader*>(buffer + eth_header_len);
    auto* acf  = reinterpret_cast<Ieee1722::AcfCommonHeader*>(buffer + eth_header_len + sizeof(Ieee1722::AvtpCommonHeader));
    
    // Payload starts after headers
    uint8_t* payload_ptr = buffer + eth_header_len + Ieee1722::HEADER_SIZE;
    size_t current_len = eth_header_len + Ieee1722::HEADER_SIZE;
    int msg_count = 0;

    // fills a buffer catching CAN frames in the queue by priority
    for (int p = 0; p < (int)Priority::COUNT; p++) {
        RingBuffer<Can::Frame, 64>* queue = nullptr;
        
        if (p == (int)Priority::CRITICAL) queue = &_critical_queue;
        else if (p == (int)Priority::HIGH) queue = &_high_queue;
        else queue = &_low_queue;

        while (!queue->empty() && msg_count < (int)MAX_BATCH_SIZE) {
            auto opt_frame = queue->pop();
            if (!opt_frame) break;

            // Log internal gateway latency: time from CAN ingress to pack moment.
            // This measures only C++ processing overhead, equivalent to VisionFive2 gw_with_opt measurements.
            log_latency(opt_frame->id, opt_frame->ingress_timestamp);

            // serialize and advance ptr
            size_t written = serialize_can_message(payload_ptr, *opt_frame);
            
            payload_ptr += written;
            current_len += written;
            msg_count++;

            if (current_len > 1450) break;
        }
    }

    if (msg_count > 0) {
        // Finalize Headers with total count
        finalize_headers(avtp, acf, msg_count, current_len - eth_header_len);

        Ethernet::Frame* frame_to_send = reinterpret_cast<Ethernet::Frame*>(buffer);

        memset(frame_to_send->header.dhost.addr, 0xFF, 6);

        frame_to_send->header.type = htons(GW_PROTOCOL); 
        frame_to_send->data_length = current_len;

        this->EthTxSubject::notify(GW_PROTOCOL, frame_to_send);
    }
}

// ----------------------------------------------------------------------
// Protocol Implementation (Bit Banging)
// ----------------------------------------------------------------------

size_t Gateway::serialize_can_message(uint8_t* ptr, const Can::Frame& f) {
    auto* acf_msg = reinterpret_cast<Ieee1722::AcfCanMessage*>(ptr);
    
    uint16_t pad_len = (4 - (f.len % 4)) % 4;
    uint16_t total_data_len = f.len + pad_len;

    uint16_t header_size_bytes = 16; 
    uint16_t quadlets = (header_size_bytes + total_data_len) / 4;

    acf_msg->acft = 0x04; // ACF Type = CAN
    acf_msg->acfl = htons(quadlets); 
    acf_msg->flags = 0x80; // MTV (Message Time Valid) = 1

    uint32_t id_word = 0;
    
    uint32_t can_id = f.id & 0x1FFFFFFF; // Mask ID
    id_word |= can_id; 

    acf_msg->can_id_field = htonl(id_word);

    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
        f.ingress_timestamp.time_since_epoch()
    ).count();
    
    acf_msg->ingress_timestamp = __builtin_bswap64(nanos);

    memset(acf_msg->payload, 0, total_data_len);
    memcpy(acf_msg->payload, f.data.data(), f.len);

    return header_size_bytes + total_data_len;
}

// ----------------------------------------------------------------------
// PROTOCOL HELPER: Deserialize
// ----------------------------------------------------------------------

size_t Gateway::deserialize_can_message(uint8_t* ptr, Can::Frame& f) {
    auto* acf_msg = reinterpret_cast<Ieee1722::AcfCanMessage*>(ptr);
    
    uint16_t quadlets = ntohs(acf_msg->acfl);
    size_t total_acf_len = quadlets * 4;

    uint32_t id_word = ntohl(acf_msg->can_id_field);

    // Fix: mask for 29-bit Extended CAN ID (ORCA protocol uses extended frames).
    // Previous value 0x7FF incorrectly truncated to 11-bit standard CAN ID.
    f.id = id_word & 0x1FFFFFFF;

    // Restore the original ingress timestamp that was embedded by Node 0.
    // This is the TX-side timestamp used for E2E latency calculation.
    // Requires PTP-synchronized CLOCK_REALTIME between the two nodes.
    uint64_t ts_raw = __builtin_bswap64(acf_msg->ingress_timestamp);
    f.ingress_timestamp = Can::Timestamp(std::chrono::nanoseconds(ts_raw));

    size_t header_overhead = 16;
    if (total_acf_len > header_overhead) {
        f.len = total_acf_len - header_overhead;
        if (f.len > 64) f.len = 64; // Cap at CAN FD max
    } else {
        f.len = 0;
    }

    memcpy(f.data.data(), acf_msg->payload, f.len);

    return total_acf_len;
}

void Gateway::finalize_headers(Ieee1722::AvtpCommonHeader* avtp, Ieee1722::AcfCommonHeader* acf, int msg_count, size_t total_len) {
    
    uint32_t cd_word = 0;
    cd_word |= (0x03 << 24); // Subtype
    cd_word |= (0x800000);   // SV bit (Bit 23)

    static uint8_t seq = 0;
    cd_word |= (seq++); 
    
    avtp->subtype_data = htonl(cd_word);
    
    avtp->stream_id = __builtin_bswap64(0x1122334455660001);

    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    uint32_t launch_time = (uint32_t)now_ns + 2000000; // +2ms
    avtp->avtp_timestamp = htonl(launch_time);

    uint16_t stream_len = total_len - sizeof(Ieee1722::AvtpCommonHeader);
    uint32_t format_word = 0;
    format_word |= (stream_len << 16);
    format_word |= (0x02 << 8); // ACF Format (0x02 = CAN/LIN/FlexRay/Serial)
    
    avtp->format_info = htonl(format_word);

    uint32_t acf_info = 0;
    acf_info |= (msg_count << 23);
    acf->msg_info = htonl(acf_info);
}