#ifndef ETH_IFACE_HPP
#define ETH_IFACE_HPP

#include <string>
#include <thread>
#include <atomic>

#include "observer/conditionally_data_observed.h"
#include "observer/conditional_data_observer.hpp"
#include "protocol/ethernet.hpp"

/**
 * @brief Ethernet Interface using MMAP for 10BASE-T1S communication
 * @details EthIface is both a Observer and a Observed.
 * Observed by the Gateway (Receives 10BASE-T1S -> Notifies Gateway)
 * Observes the Gateway (Gateway notifies about encap. CAN packets -> Send in 10BASE-T1S)
 */
class EthIface : 
    public ConditionallyDataObserved<Ethernet::Frame, Ethernet::EthType>,
    public ConditionalDataObserver<Ethernet::Frame, Ethernet::EthType>
{
public:
    EthIface(const char* iface_name);
    ~EthIface();

    int start();

    /**
     * @brief Called when Gateway wants to send an Ethernet frame
     */
    void update(ConditionallyDataObserved<Ethernet::Frame, Ethernet::EthType>* obs, Ethernet::EthType c, Ethernet::Frame* frame) override;

private:
    void receive_loop_mmap();
    void setup_rx_ring();
    void teardown_rx_ring();

private:
    std::string _iface_name;
    int socket_fd;
    std::thread _receive_thread;
    std::atomic<bool> _running;

    // MMAP Specific Attributes
    uint8_t* _mmap_buffer; // pointer to shared kernel memory
    size_t _mmap_size;
    
    // The Ring Configuration
    unsigned int _block_size;
    unsigned int _block_nr;
    unsigned int _frame_size;
    unsigned int _frame_nr;

    // Ring State
    unsigned int _rx_ring_offset; // Which frame are we reading?
};

#endif