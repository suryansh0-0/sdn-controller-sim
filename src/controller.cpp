#include "controller.hpp"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>

#include "common.hpp"
#include "protocol.hpp"

using namespace std;
using namespace proto;

namespace {
string sn(int id) { return "s" + to_string(id); }
string path_str(const vector<int>& p) {
    string s;
    for (size_t i = 0; i < p.size(); ++i) s += (i ? " -> " : "") + sn(p[i]);
    return s;
}
}  // namespace

Controller::Conn::~Conn() {
    if (fd >= 0) ::close(fd);
}

Controller::Controller(Topology topo) : topo_(move(topo)) {}
Controller::~Controller() { stop(); }

bool Controller::start(uint16_t port) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;
    int yes = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 ||
        ::listen(listen_fd_, 16) < 0)
        return false;

    socklen_t len = sizeof addr;
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);

    running_ = true;
    accept_thread_ = thread(&Controller::accept_loop, this);
    sim_log("controller", "listening on 127.0.0.1:" + to_string(port_));
    return true;
}

void Controller::stop() {
    if (!running_.exchange(false)) return;
    if (accept_thread_.joinable()) accept_thread_.join();
    ::close(listen_fd_);
    vector<thread> threads;
    {
        lock_guard<mutex> g(thr_mu_);
        for (auto& c : all_conns_) ::shutdown(c->fd, SHUT_RDWR);  // unblock recv
        threads.swap(threads_);
    }
    for (auto& t : threads) t.join();
    lock_guard<mutex> g(mu_);
    conns_.clear();
    all_conns_.clear();
}

void Controller::wait_for_switches(size_t n) {
    unique_lock<mutex> lk(mu_);
    cv_.wait_for(lk, chrono::seconds(5), [&] { return conns_.size() >= n; });
}

void Controller::accept_loop() {
    while (running_) {
        pollfd pfd{listen_fd_, POLLIN, 0};
        if (::poll(&pfd, 1, 100) <= 0) continue;  // timeout lets us notice stop()
        int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) continue;
        auto conn = make_shared<Conn>();
        conn->fd = fd;
        lock_guard<mutex> g(thr_mu_);
        all_conns_.push_back(conn);
        threads_.emplace_back(&Controller::serve, this, conn);
    }
}

void Controller::serve(shared_ptr<Conn> conn) {
    int id = -1;
    string msg;
    try {
        if (!recv_msg(conn->fd, msg)) return;
        auto t = split(msg);
        if (t.size() != 2 || t[0] != "HELLO") return;
        id = stoi(t[1]);
        {
            lock_guard<mutex> g(mu_);
            conns_[id] = conn;
        }
        cv_.notify_all();
        sim_log("controller", "HELLO from " + sn(id));
    } catch (const exception&) {
        return;
    }

    while (running_ && recv_msg(conn->fd, msg)) {
        Out out;
        try {
            auto t = split(msg);
            if (t.empty()) continue;
            {
                lock_guard<mutex> g(mu_);
                if (t[0] == "PACKET_IN" && t.size() == 6)
                    handle_packet_in(stoi(t[1]), stoi(t[2]), stoi(t[3]),
                                     stoi(t[4]), stoi(t[5]), out);
                else if (t[0] == "LINK_DOWN" && t.size() == 3)
                    handle_link_down(stoi(t[1]), stoi(t[2]), out);
                else if (t[0] == "LINK_UP" && t.size() == 4)
                    handle_link_up(stoi(t[1]), stoi(t[2]), stoi(t[3]));
            }
            flush(out);  // send outside the state lock
        } catch (const exception&) {
            sim_log("controller", "ignoring malformed message from " + sn(id));
        }
    }

    lock_guard<mutex> g(mu_);
    auto it = conns_.find(id);
    if (it != conns_.end() && it->second == conn) conns_.erase(it);
}

bool Controller::send_to(int sw, const string& msg) {
    shared_ptr<Conn> c;
    {
        lock_guard<mutex> g(mu_);
        auto it = conns_.find(sw);
        if (it == conns_.end()) return false;
        c = it->second;
    }
    lock_guard<mutex> g(c->send_mu);
    return send_msg(c->fd, msg);
}

void Controller::flush(const Out& out) {
    for (const auto& [sw, msg] : out) send_to(sw, msg);
}

// Install dst-based forwarding along `path`, downstream switches first.
void Controller::install_path(const vector<int>& path, int dst, Out& out) {
    for (int i = static_cast<int>(path.size()) - 2; i >= 0; --i) {
        installed_[path[i]][dst] = path[i + 1];
        out.push_back({path[i], "FLOW_MOD " + to_string(dst) + " " +
                                    to_string(path[i + 1])});
    }
}

void Controller::handle_packet_in(int sw, int src, int dst, int pkt, int ttl, Out& out) {
    sim_log("controller", "PACKET_IN from " + sn(sw) + " (packet #" + to_string(pkt) +
                              ", dst " + sn(dst) + ")");
    auto path = topo_.shortest_path(sw, dst);
    int next = -1;
    if (path.size() >= 2) {
        sim_log("controller", "Dijkstra path: " + path_str(path) + " -> installing flows");
        install_path(path, dst, out);
        next = path[1];
    } else {
        sim_log("controller", "no route from " + sn(sw) + " to " + sn(dst));
    }
    out.push_back({sw, "PACKET_OUT " + to_string(pkt) + " " + to_string(src) +
                           " " + to_string(dst) + " " + to_string(ttl) + " " +
                           to_string(next)});
}

void Controller::handle_link_down(int a, int b, Out& out) {
    if (!topo_.remove_link(a, b)) return;  // second endpoint's report: already handled
    sim_log("controller", "LINK_DOWN " + sn(a) + "-" + sn(b) + ": topology updated");

    // Find installed flow entries that forward over the dead link.
    vector<pair<int, int>> affected;  // (switch, dst)
    for (const auto& [sw, table] : installed_)
        for (const auto& [dst, next] : table)
            if ((sw == a && next == b) || (sw == b && next == a)) affected.push_back({sw, dst});

    for (const auto& [sw, dst] : affected) {
        installed_[sw].erase(dst);
        out.push_back({sw, "FLOW_DEL " + to_string(dst)});
    }
    // Recompute and push new paths.
    for (const auto& [sw, dst] : affected) {
        auto path = topo_.shortest_path(sw, dst);
        if (path.size() >= 2) {
            sim_log("controller", "reroute " + sn(sw) + " -> " + sn(dst) + ": " + path_str(path));
            install_path(path, dst, out);
        } else {
            sim_log("controller", "no alternate path " + sn(sw) + " -> " + sn(dst));
        }
    }
}

void Controller::handle_link_up(int a, int b, int w) {
    if (topo_.has_link(a, b)) return;
    topo_.add_link(a, b, w);
    sim_log("controller", "LINK_UP " + sn(a) + "-" + sn(b) + ": topology updated");
}
