#include "can_iface.hpp"

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

CanIface::CanIface(const char* iface_name) 
    : _iface_name(iface_name), _running(false)
{
    socket_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (socket_fd < 0) {
        perror("CanIface: Error opening socket");
        return;
    }

    struct ifreq itr;
    strcpy(itr.ifr_name, _iface_name.c_str());
    ioctl(socket_fd, SIOCGIFINDEX, &itr);

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = itr.ifr_ifindex;

    if (bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("CanIface: Error binding");
        close(socket_fd);
    }
    
    int enable_fd = 1;
    setsockopt(socket_fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_fd, sizeof(enable_fd));
}

CanIface::~CanIface() {
    _running = false;
    if (socket_fd >= 0) close(socket_fd);
    if (_receive_thread.joinable()) _receive_thread.join();
}

int CanIface::start() {
    if (!_running) {
        _running = true;
        _receive_thread = std::thread(&CanIface::receive_loop, this);
    }
    return 0;
}

// TX Path: Gateway -> CAN Bus
void CanIface::update(ConditionallyDataObserved<Can::Frame, void>* obs, Can::Frame* frame) {
    if (!frame) return;

    struct canfd_frame k_frame;
    memset(&k_frame, 0, sizeof(k_frame));

    k_frame.can_id = frame->id;
    k_frame.len = frame->len;
    
    size_t copy_len = (frame->len > 64) ? 64 : frame->len;
    memcpy(k_frame.data, frame->data.data(), copy_len);

    if (write(socket_fd, &k_frame, sizeof(struct canfd_frame)) != sizeof(struct canfd_frame)) {
        perror("CanIface: Write error");
    }
}

// RX Path: CAN Bus -> Gateway
void CanIface::receive_loop() {
    struct canfd_frame frame;

    while (_running) {
        int n_bytes = read(socket_fd, &frame, sizeof(struct canfd_frame));

        if (n_bytes < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
            continue;
        }

        Can::Frame app_frame;
        app_frame.id = frame.can_id;
        app_frame.len = frame.len; 
        
        size_t copy_len = (frame.len > 64) ? 64 : frame.len;
        std::copy(std::begin(frame.data), std::begin(frame.data) + copy_len, app_frame.data.begin());

        this->notify(&app_frame);
    }
}