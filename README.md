# Bumblebee WiFi Mesh LITE Performance Testing
This repository contains the source code for performance testing of a WiFi mesh network using ESP32 devices. The project is designed to evaluate the performance of a 2-nodes mesh network with one root node and one leaf node. Ther is a bi-directional communication between the two nodes, allowing for comprehensive performance metrics to be gathered. RTT (Round-Trip-Time) gives us the latency of a 2 way communication between the two nodes plus the time taken to process the packet at the leaf node.

--> Using ESP-NOW for communication between nodes.

# Network Performance Metrics

## Performance Statistics (Last 10000 ms)

| Metric | Value |
|--------|-------|
| **Transmission (TX)** | 0.04 Mbps, 19.8 pps, 200 packets |
| **Reception (RX)** | 0.04 Mbps, 19.8 pps, 200 packets |
| **Round Trip Time (RTT)** | avg=7.29 ms, min=6.14 ms, max=21.20 ms (samples=577) |
| **Packet Loss** | 0 packets dropped |

## Summary

ESPNOW reduces the overhead of WiFi-Mesh. However the throughput is very limited (enough for sensor data ). The latency is very low and stable.

- **Total Throughput**: 0.04 Mbps (bidirectional)
- **Average Latency**: 7.29 ms
- **Packet Loss Rate**: 0% (0/11706 packets)
- **Connection Stability**: Stable connection maintained