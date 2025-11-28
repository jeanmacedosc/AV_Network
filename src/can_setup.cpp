#include <iostream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/can/netlink.h>

#define ATTR_ID_BITTIMING 1

// --- TIMING PROFILE: 500k @ 8MHz (Safe Mode) ---
// Clock: 8MHz -> Period 125ns.
// Prescaler (BRP): 2.
// Time Quantum (TQ): 2 * 125ns = 250ns.
// Ticks per bit: 500kbps -> 2000ns total. 2000 / 250 = 8 TQ.
// Segments: Sync(1) + Prop(2) + Phase1(3) + Phase2(2) = 8.
// Sample Point: (1+2+3) / 8 = 75%
struct can_bittiming_t {
    __u32 bitrate;      // 500000
    __u32 sample_point; // 750 (75.0%)
    __u32 tq;           // 250 (ns)
    __u32 prop_seg;     // 2
    __u32 phase_seg1;   // 3
    __u32 phase_seg2;   // 2
    __u32 sjw;          // 1
    __u32 brp;          // 2 (Divider)
};

struct nl_req {
    struct nlmsghdr n;
    struct ifinfomsg i;
    char buf[1024];
};

void add_attr(struct nl_req *req, int type, const void *data, int len) {
    struct rtattr *rta = (struct rtattr *)(((char *)req) + NLMSG_ALIGN(req->n.nlmsg_len));
    rta->rta_type = type;
    rta->rta_len = RTA_LENGTH(len);
    memcpy(RTA_DATA(rta), data, len);
    req->n.nlmsg_len = NLMSG_ALIGN(req->n.nlmsg_len) + RTA_ALIGN(rta->rta_len);
}

struct rtattr *add_attr_nest(struct nl_req *req, int type) {
    struct rtattr *nest = (struct rtattr *)(((char *)req) + NLMSG_ALIGN(req->n.nlmsg_len));
    nest->rta_type = type;
    nest->rta_len = RTA_LENGTH(0);
    req->n.nlmsg_len = NLMSG_ALIGN(req->n.nlmsg_len) + RTA_ALIGN(0);
    return nest;
}

void end_attr_nest(struct nl_req *req, struct rtattr *nest) {
    nest->rta_len = (char *)req + req->n.nlmsg_len - (char *)nest;
}

int set_down(const char *ifname) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
    if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0) { close(sockfd); return -1; }
    ifr.ifr_flags &= ~IFF_UP;
    ioctl(sockfd, SIOCSIFFLAGS, &ifr);
    close(sockfd);
    return 0;
}

int set_up(const char *ifname) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
    if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0) { close(sockfd); return -1; }
    ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
    if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) < 0) {
        perror("IOCTL Up Failed");
        close(sockfd); return -1;
    }
    close(sockfd);
    return 0;
}

int set_manual_timing(const char *ifname) {
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0) { perror("Socket"); return -1; }

    struct nl_req req;
    memset(&req, 0, sizeof(req));

    req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    req.n.nlmsg_type = RTM_NEWLINK;
    req.i.ifi_family = AF_UNSPEC;
    req.i.ifi_index = if_nametoindex(ifname);

    if (req.i.ifi_index == 0) {
        std::cerr << "Interface not found" << std::endl;
        close(fd); return -1;
    }

    struct can_bittiming_t bt;
    bt.bitrate = 500000;
    bt.sample_point = 750;
    bt.tq = 250;       // 8MHz/2 = 4MHz tick (250ns)
    bt.prop_seg = 2;
    bt.phase_seg1 = 3;
    bt.phase_seg2 = 2;
    bt.sjw = 1;
    bt.brp = 2;        // Prescaler 2

    struct rtattr *linkinfo = add_attr_nest(&req, IFLA_LINKINFO);
    add_attr(&req, IFLA_INFO_KIND, "can", 4);
    
    struct rtattr *infodata = add_attr_nest(&req, IFLA_INFO_DATA);
    add_attr(&req, ATTR_ID_BITTIMING, &bt, sizeof(bt));
    
    end_attr_nest(&req, infodata);
    end_attr_nest(&req, linkinfo);

    if (send(fd, &req, req.n.nlmsg_len, 0) < 0) {
        perror("Send"); close(fd); return -1;
    }

    char buf[1024];
    recv(fd, buf, sizeof(buf), 0);
    struct nlmsghdr *nh = (struct nlmsghdr *)buf;
    if (nh->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nh);
        if (err->error != 0) {
            std::cerr << "Netlink Error: " << -(err->error) << " (" << strerror(-(err->error)) << ")" << std::endl;
            close(fd); return -1;
        }
    }
    close(fd);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./can_setup <iface>" << std::endl;
        return 1;
    }
    const char *ifname = argv[1];

    std::cout << "1. Forcing " << ifname << " DOWN..." << std::endl;
    set_down(ifname);

    std::cout << "2. Setting SAFE timing (500k/8MHz BRP=2)..." << std::endl;
    if (set_manual_timing(ifname) != 0) {
        std::cerr << "FAILED." << std::endl;
        return 1;
    }

    std::cout << "3. Bringing UP..." << std::endl;
    if (set_up(ifname) != 0) {
        return 1;
    }

    std::cout << "SUCCESS! " << ifname << " configured." << std::endl;
    return 0;
}