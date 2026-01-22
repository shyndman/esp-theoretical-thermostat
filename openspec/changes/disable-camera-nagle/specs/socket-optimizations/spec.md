# socket-optimizations Specification

## Purpose
Optimize the camera HTTP server's TCP sockets for low-latency video delivery.

## ADDED Requirements

### Requirement: Disable Nagle's Algorithm
The camera HTTP server SHALL disable Nagle's algorithm for all client connections to ensure immediate transmission of MJPEG chunks.

#### Scenario: Client connects to camera server
- **WHEN** a new TCP connection is accepted by the camera HTTP server
- **THEN** the system sets the `TCP_NODELAY` socket option to `1` on the session socket via an `open_fn` callback.

### Requirement: Low-Latency IP Priority
The camera HTTP server SHALL mark its outgoing packets for low-latency delivery.

#### Scenario: Packet prioritization
- **WHEN** a new TCP connection is accepted by the camera HTTP server
- **THEN** the system sets the `IP_TOS` socket option to `IPTOS_LOWDELAY` (0x10) on the session socket.

### Requirement: Immediate Acknowledgments
The camera HTTP server SHALL request immediate acknowledgments for its transmitted data to maintain a steady stream.

#### Scenario: Disabling delayed ACKs
- **WHEN** a new TCP connection is accepted by the camera HTTP server
- **THEN** the system sets the `TCP_QUICKACK` socket option to `1` on the session socket.
