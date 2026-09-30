#include "network.hpp"

#include "sdn_switch.hpp"

static std::string sn(int id) { return "s" + std::to_string(id); }

void Network::add_switch(Switch* s) {
    std::lock_guard<std::mutex> g(mu_);
    switches_[s->id()] = s;
}

void Network::add_link(int a, int b, int w) {
    std::lock_guard<std::mutex> g(mu_);
    links_[key(a, b)] = {w, true};
}

bool Network::link_up(int a, int b) const {
    std::lock_guard<std::mutex> g(mu_);
    auto it = links_.find(key(a, b));
    return it != links_.end() && it->second.up;
}

void Network::transmit(int from, int to, const Packet& p) {
    Switch* target = nullptr;
    bool up = false;
    {
        std::lock_guard<std::mutex> g(mu_);
        auto l = links_.find(key(from, to));
        up = l != links_.end() && l->second.up;
        auto s = switches_.find(to);
        if (s != switches_.end()) target = s->second;
    }
    if (!up || !target) {
        sim_log("net", "packet #" + std::to_string(p.id) + " DROPPED: no live link " +
                           sn(from) + "-" + sn(to));
        return;
    }
    sim_log("net", "packet #" + std::to_string(p.id) + " " + sn(from) + " -> " + sn(to));
    target->receive(p);  // lock NOT held: receive() may transmit again
}

void Network::fail_link(int a, int b) {
    Switch *sa = nullptr, *sb = nullptr;
    {
        std::lock_guard<std::mutex> g(mu_);
        auto l = links_.find(key(a, b));
        if (l == links_.end() || !l->second.up) return;
        l->second.up = false;
        sa = switches_[a];
        sb = switches_[b];
    }
    sim_log("net", "LINK FAILURE " + sn(a) + "-" + sn(b));
    // Both endpoints notice and report to the controller.
    if (sa) sa->on_link_event(b, false, 0);
    if (sb) sb->on_link_event(a, false, 0);
}

void Network::restore_link(int a, int b) {
    Switch *sa = nullptr, *sb = nullptr;
    int w = 1;
    {
        std::lock_guard<std::mutex> g(mu_);
        auto l = links_.find(key(a, b));
        if (l == links_.end() || l->second.up) return;
        l->second.up = true;
        w = l->second.weight;
        sa = switches_[a];
        sb = switches_[b];
    }
    sim_log("net", "LINK RESTORED " + sn(a) + "-" + sn(b));
    if (sa) sa->on_link_event(b, true, w);
    if (sb) sb->on_link_event(a, true, w);
}
