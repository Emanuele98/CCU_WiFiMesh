# Bumblebee WiFi Mesh LITE Performance Testing
This repository contains the source code for performance testing of a WiFi mesh network using ESP32 devices. The project is designed to evaluate the performance of a 2-nodes mesh network with one root node and one leaf node. Ther is a bi-directional communication between the two nodes, allowing for comprehensive performance metrics to be gathered. RTT (Round-Trip-Time) gives us the latency of a 2 way communication between the two nodes plus the time taken to process the packet at the leaf node.

--> Using TCP/IP sockets to excchange data between the nodes

# Network Performance Metrics

## Performance Statistics (Last 10000 ms)

| Metric | Value |
|--------|-------|
| **Transmission (TX)** | 5.57 Mbps, 491 pps, 4917 packets |
| **Reception (RX)** | 5.57 Mbps, 491 pps, 4917 packets |
| **Round Trip Time (RTT)** | avg=6.82 ms, min=2.55 ms, max=26.50 ms (samples=577) |
| **Packet Loss** | 0 packets dropped |

## Summary

TCP/IP adds more overhead compared to WiFi Mesh which works at the MAC layer. This is confermed by the lower throughput and higher latency values observed in the TCP/IP based tests.

- **Total Throughput**: 5.57 Mbps (bidirectional)
- **Average Latency**: 6.82 ms
- **Packet Loss Rate**: 0% (0/11706 packets)
- **Connection Stability**: Stable connection maintained