## ADDED Requirements

### Requirement: Camera Stream Orientation
The system SHALL apply an always-on vertical flip to the camera capture pipeline before encoding and streaming, without adding a runtime Kconfig toggle.

#### Scenario: Vertical flip accepted
- **WHEN** `CONFIG_THEO_CAMERA_ENABLE` is set and `/dev/video0` capture is initializing
- **THEN** the system sets the YUV420 capture format via `VIDIOC_S_FMT`
- **AND** issues `VIDIOC_S_CTRL` with a `struct v4l2_control` (`id=V4L2_CID_VFLIP`, `value=1`) before `VIDIOC_REQBUFS`
- **AND** logs an info message confirming the vertical flip is enabled
- **AND** continues streaming with the flipped output

#### Scenario: Vertical flip unsupported
- **WHEN** the `V4L2_CID_VFLIP` control returns `EINVAL`, `ENOTTY`, or any other failure
- **THEN** the system logs a warning that includes the errno
- **AND** continues streaming without the flip applied
