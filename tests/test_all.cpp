#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>

#include "protocol.hpp"
#include "topology.hpp"

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #c "\n"; \
            std::exit(1);                                                         \
        }                                                                         \
    } while (0)

static void test_dijkstra() {
    Topology t;
    t.add_link(1, 2, 1);
    t.add_link(2, 3, 1);
    t.add_link(1, 4, 2);
    t.add_link(4, 5, 2);
    t.add_link(5, 3, 2);

    CHECK((t.shortest_path(1, 3) == std::vector<int>{1, 2, 3}));
    CHECK((t.shortest_path(1, 1) == std::vector<int>{1}));

    CHECK(t.remove_link(2, 3));
    CHECK(!t.remove_link(2, 3));  // already gone
    CHECK((t.shortest_path(1, 3) == std::vector<int>{1, 4, 5, 3}));

    t.remove_link(5, 3);
    CHECK(t.shortest_path(1, 3).empty());   // unreachable
    CHECK(t.shortest_path(1, 99).empty());  // unknown node
}

static void test_weights_beat_hops() {
    Topology t;
    t.add_link(1, 3, 10);  // 1 hop but expensive
    t.add_link(1, 2, 1);
    t.add_link(2, 3, 1);
    CHECK((t.shortest_path(1, 3) == std::vector<int>{1, 2, 3}));
}

static void test_framing() {
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    std::string big(5000, 'x');
    CHECK(proto::send_msg(sv[0], "HELLO 1"));
    CHECK(proto::send_msg(sv[0], big));
    CHECK(proto::send_msg(sv[0], "PACKET_IN 1 2 3 4 5"));  // back-to-back: boundaries must hold

    std::string m;
    CHECK(proto::recv_msg(sv[1], m) && m == "HELLO 1");
    CHECK(proto::recv_msg(sv[1], m) && m == big);
    CHECK(proto::recv_msg(sv[1], m) && m == "PACKET_IN 1 2 3 4 5");
    close(sv[0]);
    CHECK(!proto::recv_msg(sv[1], m));  // peer closed
    close(sv[1]);
}

int main() {
    test_dijkstra();
    test_weights_beat_hops();
    test_framing();
    std::cout << "all tests passed\n";
}
