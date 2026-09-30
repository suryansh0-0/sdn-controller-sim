#include "sdn_switch.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "network.hpp"
#include "protocol.hpp"

static std::string sn(int id) { return "s" + std::to_string(id); }

Switch::Switch(int id, Network& net) : id_(id), net_(net) {}

Switch::~Switch() {
    if (fd_ >= 0) ::shutdown(fd_, SHUT_RDWR);  // unblocks recv in control_loop
    if (thread_.joinable()) thread_.join();
    if (fd_ >= 0) ::close(fd_);
}

bool Switch::connect(uint16_t port) {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    send_ctrl("HELLO " + std::to_string(id_));
    thread_ = std::thread(&Switch::control_loop, this);
    return true;
}

void Switch::send_ctrl(const std::string& msg) {
    std::lock_guard<std::mutex> g(send_mu_);
    proto::send_msg(fd_, msg);
}

void Switch::inject(int dst, int pkt_id) {
    sim_log(sn(id_), "host sends packet #" + std::to_string(pkt_id) + " to " + sn(dst));
    receive(Packet{pkt_id, id_, dst, 16});
}

void Switch::receive(Packet p) {
    if (p.dst == id_) {
        sim_log(sn(id_), "packet #" + std::to_string(p.id) + " DELIVERED (from " +
                             sn(p.src) + ")");
        return;
    }
    if (p.ttl <= 0) {
        sim_log(sn(id_), "packet #" + std::to_string(p.id) + " dropped (TTL expired)");
        return;
    }
    --p.ttl;

    int next = -1;
    {
        std::lock_guard<std::mutex> g(table_mu_);
        auto it = flows_.find(p.dst);
        if (it != flows_.end()) next = it->second;
    }

    if (next >= 0) {  // flow table hit: forward without asking the controller
        net_.transmit(id_, next, p);
    } else {          // miss: ask the controller
        sim_log(sn(id_), "flow table MISS for dst " + sn(p.dst) + " -> PACKET_IN");
        send_ctrl("PACKET_IN " + std::to_string(id_) + " " + std::to_string(p.src) + " " +
                  std::to_string(p.dst) + " " + std::to_string(p.id) + " " +
                  std::to_string(p.ttl));
    }
}

void Switch::on_link_event(int nbr, bool up, int w) {
    if (up)
        send_ctrl("LINK_UP " + std::to_string(id_) + " " + std::to_string(nbr) + " " +
                  std::to_string(w));
    else
        send_ctrl("LINK_DOWN " + std::to_string(id_) + " " + std::to_string(nbr));
}

void Switch::control_loop() {
    std::string msg;
    while (proto::recv_msg(fd_, msg)) {
        try {
            handle_control(msg);
        } catch (const std::exception&) {
            sim_log(sn(id_), "ignoring malformed control message: " + msg);
        }
    }
}

void Switch::handle_control(const std::string& msg) {
    auto t = proto::split(msg);
    if (t.empty()) return;

    if (t[0] == "FLOW_MOD" && t.size() == 3) {
        int dst = std::stoi(t[1]), next = std::stoi(t[2]);
        {
            std::lock_guard<std::mutex> g(table_mu_);
            flows_[dst] = next;
        }
        sim_log(sn(id_), "FLOW_MOD: dst " + sn(dst) + " -> next hop " + sn(next));
    } else if (t[0] == "FLOW_DEL" && t.size() == 2) {
        int dst = std::stoi(t[1]);
        {
            std::lock_guard<std::mutex> g(table_mu_);
            flows_.erase(dst);
        }
        sim_log(sn(id_), "FLOW_DEL: dst " + sn(dst));
    } else if (t[0] == "PACKET_OUT" && t.size() == 6) {
        Packet p{std::stoi(t[1]), std::stoi(t[2]), std::stoi(t[3]), std::stoi(t[4])};
        int next = std::stoi(t[5]);
        if (next < 0)
            sim_log(sn(id_), "packet #" + std::to_string(p.id) + " dropped (no route)");
        else
            net_.transmit(id_, next, p);
    }
}

void Switch::dump_flow_table() {
    std::string s;
    {
        std::lock_guard<std::mutex> g(table_mu_);
        for (const auto& [d, n] : flows_) s += " [dst " + sn(d) + " -> " + sn(n) + "]";
    }
    if (s.empty()) s = " (empty)";
    sim_log(sn(id_), "flow table:" + s);
}
