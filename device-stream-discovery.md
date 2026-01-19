# Device Stream Discovery Report (go2rtc Ingest Focus)

## Goal
1. Make the device’s HTTP stream ingest cleanly in **stock go2rtc** without patches.

## Required stream shape for stock go2rtc
1. **Annex‑B byte stream** with start codes (`00 00 00 01` or `00 00 01`).
2. **SPS + PPS at connection start**, ideally before the first IDR frame.
3. **Repeat SPS/PPS before each IDR** so new consumers can join mid‑stream.
4. **Start‑on‑IDR** if the device supports “keyframe on connect.”

## Recommended producer‑side fixes
1. Enable a device setting like **“Annex‑B output”** or **“H.264 byte‑stream.”**
2. Enable **“send keyframe on connect”** or equivalent.
3. Enable **“insert SPS/PPS on IDR.”**
4. If the device can’t emit raw Annex‑B, switch the output to **MPEG‑TS or FLV** (stock go2rtc can ingest those without raw bitstream probing).

## What we observed
1. `ffplay` successfully ingests `http://192.168.85.28:8080/video` and identifies H.264.
2. Stock go2rtc reports “codec not matched” when ingesting the same URL.
3. The HTTP response advertises `Content-Type: video/h264`, so the device claims H.264.

## Why go2rtc fails when ffplay succeeds
1. go2rtc’s raw HTTP path uses a **short probe window** and decides codec from the **first NAL** it sees.
2. If the stream starts mid‑GOP (no SPS/PPS/IDR at the beginning), go2rtc can mis‑detect the codec or fail to build a valid H.264 track.
3. ffplay probes deeper and can recover once SPS/PPS arrives later, so it hides the “starts mid‑GOP” issue.

## Evidence (ffplay)
1. ffplay probes the stream as raw H.264 and later finds SPS/PPS/IDR:
   1. `Format h264 probed with size=2048 and score=51`
   2. `nal_unit_type: 7(SPS)` / `8(PPS)` / `5(IDR)`

## Quick diagnostics for developers
1. Capture the **first 2–4 KB** of the HTTP stream and look for `00 00 00 01 67` (SPS) and `00 00 00 01 68` (PPS).
2. If the first NAL isn’t SPS/PPS/IDR, the stream is starting mid‑GOP.
3. Confirm the stream is **Annex‑B** (start codes) and not **AVCC** (length‑prefixed). Stock go2rtc won’t parse raw AVCC over HTTP.

## Optional go2rtc‑side mitigations (if needed later)
1. Increase the probe window or scan multiple NALs before deciding between H.264 and H.265.
2. Add trace logging of NAL types during probe for faster diagnosis.

---
If you want, I can add a short patch to go2rtc that logs NAL types or delays codec selection, but the cleanest path for **stock** go2rtc is still to ensure the producer emits Annex‑B with SPS/PPS at the start of the stream.
