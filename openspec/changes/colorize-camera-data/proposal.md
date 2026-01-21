# Change: Colorize OV5647 Stream

## Why
The new OV5647 pipeline currently produces a washed-out, nearly monochrome feed because the ISP is left in its default (mostly disabled) state. We need to explicitly enable demosaic/color modules and surface sane defaults so downstream consumers (NVR/ML) see true-color video.

## What Changes
- Enable and tune the ESP ISP stages (demosaic, white balance, color correction, saturation) whenever the camera pipeline starts.
- Provide configuration hooks so we can tweak gains/curves without editing source code.
- Update the camera-streaming spec to describe the ISP color requirement and the validation workflow.

## Impact
- Specs: `camera-streaming`
- Code: `main/streaming/h264_stream.c`, future ISP helper(s)
- Tooling: new Kconfig/openspec tasks for ISP tuning
