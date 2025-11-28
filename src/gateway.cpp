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
        
        // csv header
        if (_log_file.is_open()) {
            _log_file << "CAN_ID,Priority,Ingress_NS,Egress_NS,Latency_US\n";
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
}

void Gateway::log_latency(Can::Id id, std::chrono::system_clock::time_point ingress) {
    if (!_log_file.is_open()) return;

    auto now = std::chrono::system_clock::now();
    
    // calculate latency in ms
    auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(now - ingress).count();

    // get raw nanoseconds for plotting details later
    auto ingress_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(ingress.time_since_epoch()).count();
    auto egress_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

    // determines priority for coloring the graph later
    int prio = (int)resolve_priority(id);

    // Format: ID, Prio, Ingress, Egress, Latency
    _log_file << "0x" << std::hex << id << std::dec << ","
              << prio << ","
              << ingress_ns << ","
              << egress_ns << ","
              << latency_us << "\n" << std::endl;
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
    Priority prio = resolve_priority(frame->id);
    bool queued = false;

    if (prio == Priority::CRITICAL) queued = _critical_queue.push(*frame);
    else if (prio == Priority::HIGH) queued = _high_queue.push(*frame);
    else queued = _low_queue.push(*frame);

    // trigger egress if critical
    if (queued && prio == Priority::CRITICAL) {
        _cv.notify_one();
    }
}

/**
 * @brief Eth interfaces -> Gateway
 */
void Gateway::update(EthTxSubject* obs, Ethernet::EthType c, Ethernet::Frame* frame) {
    if (c != GW_PROTOCOL) return;

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

        // notifies all attached CAN interfaces
        this->CanTxSubject::notify(&can_frame);

        payload_ptr += bytes_read;
    }
}

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

void Gateway::egress_loop() {
    // 1518 bytes covers standard Ethernet MTU + headers
    uint8_t eth_buffer[1518]; 

    while (_running) {
        std::unique_lock<std::mutex> lock(_cv_mutex);
        
        // waits for 2ms or till a notification for CRITICAL CAN comes
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

    for (int p = 0; p < (int)Priority::COUNT; p++) {
        RingBuffer<Can::Frame, 1024>* queue = nullptr;
        
        if (p == (int)Priority::CRITICAL) queue = &_critical_queue;
        else if (p == (int)Priority::HIGH) queue = &_high_queue;
        else queue = &_low_queue;

        while (!queue->empty() && msg_count < (int)MAX_BATCH_SIZE) {
            auto opt_frame = queue->pop();
            if (!opt_frame) break;

            // We log exactly at the moment the frame leaves the buffer to be packed
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
    
    f.id = id_word & 0x7FF; 
    
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