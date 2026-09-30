# SDN Controller Simulator (C++17)

A software-defined networking simulator with a **control plane** (TCP controller) cleanly
separated from a **data plane** (switches + links).

```
            TCP (length-prefixed messages)
 s1 ─┐                                    ┌─ Controller
 s2 ─┤  HELLO / PACKET_IN / LINK_DOWN ──► │  topology graph + Dijkstra
 s3 ─┤  ◄── FLOW_MOD / FLOW_DEL / PACKET_OUT │  installed-flow state
 s4 ─┤                                    └─
 s5 ─┘
   └── data plane: Network class (links, failures, packet delivery)
```

## Build & run
```
cmake -S . -B build && cmake --build build
./build/sdn_sim        # demo: install path, forward, link failure, reroute
ctest --test-dir build # unit tests (Dijkstra, TCP framing)
```
No CMake? `g++ -std=c++17 -Iinclude src/*.cpp -o sdn_sim -pthread`

## How it works
1. A packet with no matching flow entry triggers `PACKET_IN` to the controller.
2. The controller runs Dijkstra on its topology, sends `FLOW_MOD` to every switch on the
   path (destination side first), then `PACKET_OUT` to release the waiting packet.
3. Later packets hit the flow table and never touch the controller.
4. On link failure both endpoints send `LINK_DOWN`. The controller removes the link, finds
   flow entries that used it, sends `FLOW_DEL`, recomputes paths, and pushes new `FLOW_MOD`s.

## Wire protocol
`[4-byte big-endian length][text payload]`, framed because TCP has no message boundaries.

| Direction | Message |
|---|---|
| switch → ctrl | `HELLO id`, `PACKET_IN sw src dst pkt ttl`, `LINK_DOWN a b`, `LINK_UP a b w` |
| ctrl → switch | `FLOW_MOD dst next`, `FLOW_DEL dst`, `PACKET_OUT pkt src dst ttl next` |

## Design notes
- Thread per switch connection on the controller; state guarded by one mutex, and messages
  are queued under the lock and sent after releasing it (no I/O while locked).
- Flows on different switches arrive over different sockets, so a packet can outrun its
  FLOW_MOD. The switch just misses again and the controller handles it (idempotent).
- TTL on packets prevents loops during transient inconsistency.

## Ideas to extend
- Reroute on `LINK_UP` (currently only updates the topology)
- Flow entry timeouts, ECMP, or per-(src,dst) flows
- Link-state discovery (LLDP-style) instead of a preloaded topology
- Run controller and each switch as separate processes
