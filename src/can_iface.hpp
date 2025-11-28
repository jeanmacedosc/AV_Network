#ifndef CAN_IFACE_HPP
#define CAN_IFACE_HPP

#include <iostream>
#include <string>
#include <thread>
#include <atomic>

#include "observer/conditionally_data_observed.h"
#include "observer/conditional_data_observer.hpp"
#include "definitions/can.hpp"

namespace Can { struct Frame; }

class CanIface : 
    public ConditionallyDataObserved<Can::Frame, void>,
    public ConditionalDataObserver<Can::Frame, void>
{
public:
    CanIface(const char* iface_name);
    ~CanIface();

    int start();

    /**
     * @brief Called when the Gateway wants to send a frame to the CAN bus
     */
    void update(ConditionallyDataObserved<Can::Frame, void>* obs, Can::Frame* frame) override;

private:
    void receive_loop();

private:
    std::string _iface_name;
    int socket_fd;
    std::thread _receive_thread;
    std::atomic<bool> _running;
};

#endif