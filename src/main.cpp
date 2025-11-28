#include <iostream>
#include <string.h>
#include <vector>
#include <memory>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>

#include "can_iface.hpp"
#include "eth_iface.hpp"
#include "gateway.hpp"

void setup_realtime_env() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(3, &cpuset); // select Core 3

    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("RT-Setup: Failed to set CPU affinity");
    } else {
        std::cout << "RT-Setup: Process pinned to CPU Core 3." << std::endl;
    }

    struct sched_param param;
    param.sched_priority = 80;

    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        perror("RT-Setup: Failed to set SCHED_FIFO (Are you root?)");
    } else {
        std::cout << "RT-Setup: Policy set to SCHED_FIFO (Prio 80)." << std::endl;
    }
}

int main(int argc, char* argv[])
{
    setup_realtime_env();
    if (argc < 3) {
         std::cerr << "Usage: ./gateway <can_iface> <eth_iface>" << std::endl;
         return 1;
    }

    std::vector<std::shared_ptr<CanIface>> can_interfaces;
    std::vector<std::shared_ptr<EthIface>> eth_interfaces;

    Gateway* gw = Gateway::get_instance();

    for (int i = 1; i < argc; i++) {
        if (strstr(argv[i], "can") != NULL) {
            can_interfaces.push_back(std::make_shared<CanIface>(argv[i]));
        } else {
            eth_interfaces.push_back(std::make_shared<EthIface>(argv[i]));
        }
    }

    for (auto& iface : eth_interfaces) {
        // gateway listens to eth interfaces
        iface->attach(gw, Gateway::GW_PROTOCOL);
        
        gw->EthTxSubject::attach(iface.get(), Gateway::GW_PROTOCOL);
        
        iface->start();
        std::cout << "Started Ethernet Interface" << std::endl;
    }

    for (auto& iface : can_interfaces) {
        iface->attach(gw); // Condition is void/nullptr for CAN
        
        gw->CanTxSubject::attach(iface.get());
        
        iface->start();
        std::cout << "Started CAN Interface" << std::endl;
    }

    gw->start();
    std::cout << "Gateway Started. Press Ctrl+C to exit." << std::endl;

    while(true) {
        sleep(100);
    }

    return 0;
}