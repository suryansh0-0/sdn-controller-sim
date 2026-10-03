// Demo: 5 switches, real TCP control channel, simulated data plane.
//
//        1        1
//   s1 ----- s2 ----- s3
//    |                 |
//    | 2               | 2
//    s4 ------------- s5
//            2
#include <signal.h>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "common.hpp"
#include "controller.hpp"
#include "network.hpp"
#include "sdn_switch.hpp"

using namespace std;

static void settle() { this_thread::sleep_for(chrono::milliseconds(300)); }
static void step(const string& s) { sim_log("demo", "\n=== " + s + " ==="); }

int main() {
    signal(SIGPIPE, SIG_IGN);

    struct Edge { int a, b, w; };
    const vector<Edge> edges = {{1, 2, 1}, {2, 3, 1}, {1, 4, 2}, {4, 5, 2}, {5, 3, 2}};

    Topology topo;
    Network net;
    for (auto& e : edges) {
        topo.add_link(e.a, e.b, e.w);
        net.add_link(e.a, e.b, e.w);
    }

    Controller ctrl(topo);
    if (!ctrl.start(0)) { cerr << "controller failed to start\n"; return 1; }

    vector<unique_ptr<Switch>> sw;  // destroyed before ctrl
    for (int id = 1; id <= 5; ++id) {
        sw.push_back(make_unique<Switch>(id, net));
        net.add_switch(sw.back().get());
        if (!sw.back()->connect(ctrl.port())) { cerr << "connect failed\n"; return 1; }
    }
    ctrl.wait_for_switches(5);

    step("Packet 1: s1 -> s3 (empty flow tables)");
    sw[0]->inject(3, 1);
    settle();

    step("Packet 2: s1 -> s3 (flows installed, no controller involved)");
    sw[0]->inject(3, 2);
    settle();
    for (auto& s : sw) s->dump_flow_table();

    step("Link s2-s3 fails");
    net.fail_link(2, 3);
    settle();
    for (auto& s : sw) s->dump_flow_table();

    step("Packet 3: s1 -> s3 (rerouted around the failure)");
    sw[0]->inject(3, 3);
    settle();

    step("Done");
    return 0;
}
