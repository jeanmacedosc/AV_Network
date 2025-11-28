#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include <poll.h>

#include "eth_iface.hpp"

EthIface::EthIface(const char* iface_name)
    : _iface_name(iface_name), _running(false)
{
    socket_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (socket_fd < 0) {
        perror("EthIface: Error opening socket");
        return;
    }

    setup_rx_ring();

    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = if_nametoindex(_iface_name.c_str());
    sll.sll_protocol = htons(ETH_P_ALL);

    if (bind(socket_fd, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        perror("EthIface: Error binding");
        close(socket_fd);
    }
}

EthIface::~EthIface()
{
    _running = false;
    if (_receive_thread.joinable()) _receive_thread.join();
    teardown_rx_ring();
    if (socket_fd >= 0) close(socket_fd);
}

int EthIface::start()
{
    if (!_running) {
        _running = true;
        _receive_thread = std::thread(&EthIface::receive_loop_mmap, this);
    }
    return 0;
}

void EthIface::setup_rx_ring()
{
    int val = TPACKET_V2;
    if (setsockopt(socket_fd, SOL_PACKET, PACKET_VERSION, &val, sizeof(val)) < 0) {
        perror("EthIface: Setsockopt PACKET_VERSION failed");
        return;
    }

    // Frame size 2048 covers standard Ethernet MTU (1500)
    _block_size = getpagesize();
    while (_block_size < 2048) _block_size <<= 1;
    
    _frame_size = 2048; 
    _block_nr = 64; // Number of blocks (buffers)
    
    // Frames per block
    unsigned int frames_per_block = _block_size / _frame_size;
    _frame_nr = _block_nr * frames_per_block;

    struct tpacket_req req;
    memset(&req, 0, sizeof(req));
    req.tp_block_size = _block_size;
    req.tp_frame_size = _frame_size;
    req.tp_block_nr   = _block_nr;
    req.tp_frame_nr   = _frame_nr;

    // request the Ring from Kernel
    if (setsockopt(socket_fd, SOL_PACKET, PACKET_RX_RING, &req, sizeof(req)) < 0) {
        perror("EthIface: Setsockopt PACKET_RX_RING failed");
        return;
    }

    _mmap_size = _block_nr * _block_size;
    _mmap_buffer = (uint8_t*)mmap(NULL, _mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, socket_fd, 0);
    
    if (_mmap_buffer == MAP_FAILED) {
        perror("EthIface: MMAP failed");
        _mmap_buffer = nullptr;
    }
}

void EthIface::teardown_rx_ring()
{
    if (_mmap_buffer) {
        munmap(_mmap_buffer, _mmap_size);
        _mmap_buffer = nullptr;
    }
}


// TX Path: Gateway -> 10BASE-T1S
void EthIface::update(ConditionallyDataObserved<Ethernet::Frame, Ethernet::EthType>* obs, Ethernet::EthType c, Ethernet::Frame* frame) {
    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = if_nametoindex(_iface_name.c_str());
    sll.sll_halen = ETH_ALEN;
    memset(sll.sll_addr, 0xFF, 6); 

    ssize_t sent = sendto(socket_fd, frame, frame->data_length, 0, (struct sockaddr*)&sll, sizeof(sll));
    
    if (sent < 0) {
        perror("EthIface: Send error");
    }
}

// RX Path: 10BASE-T1S -> Gateway -> CAN
void EthIface::receive_loop_mmap() {
    struct pollfd pfd;
    pfd.fd = socket_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    while (_running && _mmap_buffer) {
        struct tpacket2_hdr* header = (struct tpacket2_hdr*)(_mmap_buffer + (_rx_ring_offset * _frame_size));

        if ((header->tp_status & TP_STATUS_USER) == 0) {
            poll(&pfd, 1, 1); 
            continue;
        }
        
        uint8_t* data_ptr = (uint8_t*)header + header->tp_mac;
        int len = header->tp_len; // Actual length of packet

        Ethernet::Frame* raw_frame = reinterpret_cast<Ethernet::Frame*>(data_ptr);
        raw_frame->data_length = len;

        Ethernet::EthType proto = ntohs(raw_frame->header.type);
        this->notify(proto, raw_frame);

        header->tp_status = TP_STATUS_KERNEL; // Clear status to 0
        
        _rx_ring_offset = (_rx_ring_offset + 1) % _frame_nr;
    }
}