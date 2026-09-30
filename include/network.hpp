#pragma once
// The simulated DATA PLANE: physical links between switches.
// Packets move switch-to-switch through here; control traffic does not.
#include <algorithm>
#include <map>
#include <mutex>

#include "common.hpp"

class Switch;

class Network {
public:
    void add_switch(Switch* s);
    void add_link(int a, int b, int weight);
    bool link_up(int a, int b) const;

    void transmit(int from, int to, const Packet& p);  // deliver over a link
    void fail_link(int a, int b);                      // physical failure
    void restore_link(int a, int b);

private:
    struct Link {
        int weight;
        bool up;
    };
    static std::pair<int, int> key(int a, int b) {
        return {std::min(a, b), std::max(a, b)};
    }

    mutable std::mutex mu_;
    std::map<int, Switch*> switches_;
    std::map<std::pair<int, int>, Link> links_;
};
