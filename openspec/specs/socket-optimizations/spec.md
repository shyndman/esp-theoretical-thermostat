# socket-optimizations Specification

## Purpose
Describe how the thermostat tunes its streaming sockets for today’s WebRTC/WHEP video pipeline. The goal is to keep the media path on UDP SRTP transports negotiated by the ESP WebRTC stack while limiting HTTP involvement to control-plane signaling.

## Requirements
### Requirement: WebRTC media uses UDP SRTP transports
The thermostat SHALL deliver camera video (and optional audio) exclusively over UDP SRTP transports established by the ESP WebRTC stack. No alternate TCP media paths exist.

#### Scenario: Viewer joins via WHEP
- **WHEN** a viewer completes the WHEP offer/answer exchange at `/api/webrtc`
- **THEN** the system establishes UDP SRTP flows for the negotiated audio/video tracks and routes encoded frames solely over those transports.

### Requirement: WHEP signaling stays on HTTP control plane
The thermostat SHALL use HTTP only for the WHEP REST signaling exchange while keeping media packets on the UDP SRTP transports created by the peer connection.

#### Scenario: Streaming session active
- **WHEN** a WHEP session transitions to the connected state
- **THEN** the HTTP handler limits itself to ICE/WHEP control messages while media RTP/RTCP packets continue exclusively over UDP.
