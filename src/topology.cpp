#include "topology.hpp"

#include <algorithm>
#include <functional>
#include <queue>

void Topology::add_link(int a, int b, int w) {
    adj_[a][b] = w;
    adj_[b][a] = w;
}

bool Topology::remove_link(int a, int b) {
    if (!has_link(a, b)) return false;
    adj_[a].erase(b);
    adj_[b].erase(a);
    return true;
}

bool Topology::has_link(int a, int b) const {
    auto it = adj_.find(a);
    return it != adj_.end() && it->second.count(b) > 0;
}

std::vector<int> Topology::shortest_path(int src, int dst) const {
    if (!adj_.count(src) || !adj_.count(dst)) return {};

    using QE = std::pair<int, int>;  // (distance, node)
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    std::map<int, int> dist, prev;

    dist[src] = 0;
    pq.push({0, src});
    while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();
        if (d > dist[u]) continue;  // stale queue entry
        if (u == dst) break;
        for (const auto& [v, w] : adj_.at(u)) {
            int nd = d + w;
            auto it = dist.find(v);
            if (it == dist.end() || nd < it->second) {
                dist[v] = nd;
                prev[v] = u;
                pq.push({nd, v});
            }
        }
    }

    if (!dist.count(dst)) return {};
    std::vector<int> path;
    for (int v = dst; v != src; v = prev[v]) path.push_back(v);
    path.push_back(src);
    std::reverse(path.begin(), path.end());
    return path;
}
