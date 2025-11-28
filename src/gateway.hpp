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

#include "observer/conditional_data_observer.hpp"
#include "observer/conditionally_data_observed.h"
#include "definitions/can.hpp"
#include "definitions/ethernet.hpp"
#include "definitions/ieee1722.hpp"
#include "definitions/ring_buffer.hpp"

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
    const static Ethernet::EthType GW_PROTOCOL = 0x88b5; 

    enum class Priority {
        CRITICAL = 0,
        HIGH = 1,
        LOW = 2,
        COUNT = 3
    };

    static constexpr std::chrono::milliseconds MAX_LATENCY_WINDOW {2};
    static constexpr size_t MAX_BATCH_SIZE = 10;

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

    RingBuffer<Can::Frame, 1024> _critical_queue;
    RingBuffer<Can::Frame, 1024> _high_queue;
    RingBuffer<Can::Frame, 1024> _low_queue;

    struct PriorityRange {
        Can::Id start;
        Can::Id end;
        Priority p;
    };
    std::vector<PriorityRange> _priority_map_ranges;

    std::ofstream _log_file;
    void log_latency(Can::Id id, std::chrono::system_clock::time_point ingress);
};

#endif // GATEWAY_HPP