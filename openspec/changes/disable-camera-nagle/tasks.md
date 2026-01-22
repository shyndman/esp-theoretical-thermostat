# Tasks: Disable Nagle's Algorithm on Camera Server

- [x] Implement session open callback in `mjpeg_stream.c` <!-- id: 0 -->
    - [x] Add `#include <lwip/sockets.h>`
    - [x] Implement `mjpeg_stream_on_sess_open` function
    - [x] Set `TCP_NODELAY` and `IP_TOS` using `setsockopt`
- [x] Register callback in `start_http_server` <!-- id: 1 -->
    - [x] Set `config.open_fn = mjpeg_stream_on_sess_open`
- [x] Verify build and functionality <!-- id: 2 -->
    - [x] Run `idf.py build` to ensure no compilation errors
    - [x] Manual verification: Connect to MJPEG stream and check for stutters (on-device)
