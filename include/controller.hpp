#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "topology.hpp"

// The CONTROL PLANE: a TCP server holding the global topology view.
// One thread per switch connection; all shared state guarded by mu_.
class Controller {
public:
    explicit Controller(Topology topo);
    ~Controller();

    bool start(uint16_t port = 0);  // 0 = pick a free port
    uint16_t port() const { return port_; }
    void wait_for_switches(size_t n);
    void stop();

private:
    struct Conn {
        int fd = -1;
        std::mutex send_mu;
        ~Conn();
    };
    using Out = std::vector<std::pair<int, std::string>>;  // (switch id, message)

    void accept_loop();
    void serve(std::shared_ptr<Conn> conn);
    bool send_to(int sw, const std::string& msg);
    void flush(const Out& out);

    // These run with mu_ held and only *queue* messages into `out`.
    void handle_packet_in(int sw, int src, int dst, int pkt, int ttl, Out& out);
    void handle_link_down(int a, int b, Out& out);
    void handle_link_up(int a, int b, int w);
    void install_path(const std::vector<int>& path, int dst, Out& out);

    std::mutex mu_;
    std::condition_variable cv_;
    Topology topo_;                                  // controller's view
    std::map<int, std::shared_ptr<Conn>> conns_;     // switch id -> connection
    std::map<int, std::map<int, int>> installed_;    // switch -> (dst -> next hop)

    std::mutex thr_mu_;
    std::vector<std::shared_ptr<Conn>> all_conns_;
    std::vector<std::thread> threads_;
    std::thread accept_thread_;
    int listen_fd_ = -1;
    uint16_t port_ = 0;
    std::atomic<bool> running_{false};
};
