#pragma once
#include <map>
#include <vector>

// Undirected weighted graph (adjacency list) + Dijkstra.
class Topology {
public:
    void add_link(int a, int b, int weight = 1);
    bool remove_link(int a, int b);  // true if the link existed
    bool has_link(int a, int b) const;

    // Shortest path from src to dst, inclusive of both. Empty if unreachable.
    std::vector<int> shortest_path(int src, int dst) const;

private:
    std::map<int, std::map<int, int>> adj_;  // node -> (neighbor -> weight)
};
