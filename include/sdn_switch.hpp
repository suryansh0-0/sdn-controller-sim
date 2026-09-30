#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "common.hpp"

class Network;

// A switch: flow table (dst -> next hop) + a TCP connection to the controller.
class Switch {
public:
    Switch(int id, Network& net);
    ~Switch();
    Switch(const Switch&) = delete;
    Switch& operator=(const Switch&) = delete;

    bool connect(uint16_t controller_port);
    int id() const { return id_; }

    void inject(int dst, int pkt_id);                  // a host attached here sends a packet
    void receive(Packet p);                            // data-plane entry point
    void on_link_event(int neighbor, bool up, int w);  // from the Network
    void dump_flow_table();

private:
    void control_loop();
    void handle_control(const std::string& msg);
    void send_ctrl(const std::string& msg);

    int id_;
    Network& net_;
    int fd_ = -1;
    std::thread thread_;
    std::mutex send_mu_;   // serialises writes on the control socket
    std::mutex table_mu_;  // protects flows_
    std::map<int, int> flows_;  // match: dst  ->  action: forward to next hop
};
