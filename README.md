# Bumblebee WiFi Mesh Performance Testing
This repository contains the source code for performance testing of a WiFi mesh network using ESP32 devices. The project is designed to evaluate the performance of a 2-nodes mesh network with one root node and one leaf node. Ther is a bi-directional communication between the two nodes, allowing for comprehensive performance metrics to be gathered. RTT (Round-Trip-Time) gives us the latency of a 2 way communication between the two nodes plus the time taken to process the packet at the leaf node.

## Results
Over 10 seconds, the following performance metrics were observed:
- **TX Data rate**: 13.18 Mbps
- **RX Data rate**: 13.18 Mbps
- **Packets Sent**: 11706 packets (16482048 bytes
- **Packets Received**: 11706 packets (16482048 bytes
- **Packets Lost**: 0 packets
- **Packet Loss Rate**: 0.00%
- **Average RTT**: 3.11 ms
- **Max RTT**: about 50 ms
- **Min RTT**: about 1 ms