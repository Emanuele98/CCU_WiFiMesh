# Bumblebee WiFi Mesh Performance Testing
This repository contains the source code for performance testing of a WiFi mesh network using ESP32 devices. The project is designed to evaluate the performance of a 2-nodes mesh network with one root node and one leaf node. Ther is a bi-directional communication between the two nodes, allowing for comprehensive performance metrics to be gathered. RTT (Round-Trip-Time) gives us the latency of a 2 way communication between the two nodes plus the time taken to process the packet at the leaf node.

# Network Performance Metrics

## Performance Statistics (Last 10000 ms)

| Metric | Value |
|--------|-------|
| **Transmission (TX)** | 13.18 Mbps, 1170.5 pps, 11706 packets, 16482048 bytes |
| **Reception (RX)** | 13.18 Mbps, 1170.3 pps, 11708 packets, 16484864 bytes |
| **Round Trip Time (RTT)** | avg=3.11 ms, min=1.60 ms, max=15.60 ms (samples=5375) |
| **Packet Loss** | 0 packets dropped |

## Summary

- **Total Throughput**: 13.18 Mbps (bidirectional)
- **Average Latency**: 3.11 ms
- **Packet Loss Rate**: 0% (0/11706 packets)
- **Connection Stability**: Stable connection maintained