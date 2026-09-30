#pragma once
#include <iostream>
#include <mutex>
#include <string>

// A data-plane packet travelling between switches.
struct Packet {
    int id;
    int src;
    int dst;
    int ttl;
};

// Thread-safe logging so output from many threads doesn't interleave.
inline void sim_log(const std::string& who, const std::string& msg) {
    static std::mutex m;
    std::lock_guard<std::mutex> g(m);
    std::cout << "[" << who << "] " << msg << std::endl;
}
