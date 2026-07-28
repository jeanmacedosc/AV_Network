#ifndef GATEWAY_HPP
#define GATEWAY_HPP

#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <iostream>
#include <fstream>

#include "protocol/can.hpp"
#include "protocol/ethernet.hpp"
#include "protocol/ieee1722.hpp"
#include "protocol/ring_buffer.hpp"
#include "observer/conditional_data_observer.hpp"
#include "observer/conditionally_data_observed.h"

// RX/TX Definitions
using CanRxObserver = ConditionalDataObserver<Can::Frame, void>;
using EthRxObserver = ConditionalDataObserver<Ethernet::Frame, Ethernet::EthType>;
using CanTxSubject  = ConditionallyDataObserved<Can::Frame, void>;
using EthTxSubject  = ConditionallyDataObserved<Ethernet::Frame, Ethernet::EthType>;

class Gateway : 
    public CanRxObserver, 
    public EthRxObserver,
    public CanTxSubject, 
    public EthTxSubject 
{
public:
    const static Ethernet::EthType GW_PROTOCOL = 0x22F0; 

    enum class Priority {
        CRITICAL = 0,
        HIGH = 1,
        LOW = 2,
        COUNT = 3
    };

    static constexpr std::chrono::milliseconds MAX_LATENCY_WINDOW {2};
    // MAX_BATCH_SIZE: based on real CAN data (scope_17.csv), max burst is ~11 frames.
    // 16 covers all burst scenarios within the 2ms window with margin.
    static constexpr size_t MAX_BATCH_SIZE = 16;

public:
    static Gateway* get_instance();

    void start();
    void stop();

    // RX Methods
    void update(CanTxSubject* obs, Can::Frame* frame) override;
    void update(EthTxSubject* obs, Ethernet::EthType c, Ethernet::Frame* frame) override;

private:
    Gateway(); // Private Constructor
    static Gateway* instance;

    // Internal Logic
    void configure_routes();
    Priority resolve_priority(Can::Id id);
    void egress_loop();
    void pack_and_send_burst(uint8_t* buffer);

    // Protocol Helpers
    size_t serialize_can_message(uint8_t* ptr, const Can::Frame& f);
    // unpack ACF -> CAN
    size_t deserialize_can_message(uint8_t* ptr, Can::Frame& f);
    void finalize_headers(Ieee1722::AvtpCommonHeader* avtp, Ieee1722::AcfCommonHeader* acf, int msg_count, size_t total_len);

    // Members
    std::thread _worker_thread;
    std::atomic<bool> _running;
    std::condition_variable _cv;
    std::mutex _cv_mutex;

    // Buffer sizes based on real CAN traffic analysis (scope_17.csv):
    // - 675 fps rate, 2ms window → avg 1.35 frames/window
    // - Observed max burst: ~11 frames
    // - 64 slots = 6x safety margin over max burst, saves ~180KB vs 1024
    RingBuffer<Can::Frame, 64> _critical_queue;
    RingBuffer<Can::Frame, 64> _high_queue;
    RingBuffer<Can::Frame, 64> _low_queue;

    struct PriorityRange {
        Can::Id start;
        Can::Id end;
        Priority p;
    };
    std::vector<PriorityRange> _priority_map_ranges;

    std::ofstream _log_file;
    void log_latency(Can::Id id, std::chrono::system_clock::time_point ingress);

    // End-to-End latency log (Node 1 only, requires PTP-synced clocks)
    std::ofstream _e2e_log_file;
    void log_e2e_latency(Can::Id id, std::chrono::system_clock::time_point tx_ingress, std::chrono::system_clock::time_point rx_eth_arrival);

    // Packet loss detection via IEEE 1722 sequence number tracking (Node 1 only)
    // The AVTP header carries a 1-byte sequence counter (0-255, wraps around).
    // Any gap > 1 between consecutive received seq numbers means packet loss.
    uint8_t  _expected_seq  = 0;
    bool     _seq_initialized = false;   // skip first packet (no expected yet)
    uint64_t _total_rx_eth  = 0;
    uint64_t _total_lost    = 0;
    std::ofstream _loss_log_file;
    void log_packet_loss(uint8_t expected, uint8_t received, uint8_t lost);
};

#endif // GATEWAY_HPP