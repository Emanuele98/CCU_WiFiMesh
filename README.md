# Bumblebee WiFi Mesh LITE Performance Testing
This repository contains the source code for performance testing of a WiFi mesh network using ESP32 devices. The project is designed to evaluate the performance of a 2-nodes mesh network with one root node and one leaf node. Ther is a bi-directional communication between the two nodes, allowing for comprehensive performance metrics to be gathered. RTT (Round-Trip-Time) gives us the latency of a 2 way communication between the two nodes plus the time taken to process the packet at the leaf node.

--> Using Native Mesh Lite functions (TCP protocol)

# Network Performance Metrics

## Performance Statistics (Last 10000 ms)

| Metric | Value |
|--------|-------|
| **Transmission (TX)** | 224.22 Kbps, 19.7 pps, 40 packets |
| **Reception (RX)** | 224.22 Kbps, 19.7 pps, 40 packets |
| **Round Trip Time (RTT)** | avg=3.73 ms, min=3.12 ms, max=16.11 ms (samples=40) |
| **Packet Loss** | 0 packets dropped |

## Summary

These native TCP functions handle the mesh routing intrinsically. They are limited in memory size and therefore the throughput is limited as well; however well above Bunblebee use-case. Latency is similar to the custom TCP sockets.

- **Total Throughput**: 224.22 Kbps (bidirectional)
- **Average Latency**: 3.73 ms
- **Packet Loss Rate**: 0%
- **Connection Stability**: Stable connection maintained