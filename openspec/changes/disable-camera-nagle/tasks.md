# Tasks: Disable Nagle's Algorithm on Camera Server

- [ ] Implement session open callback in `mjpeg_stream.c` <!-- id: 0 -->
    - [ ] Add `#include <netinet/tcp.h>` and `#include <sys/socket.h>`
    - [ ] Implement `mjpeg_stream_on_sess_open` function
    - [ ] Set `TCP_NODELAY`, `TCP_QUICKACK`, and `IP_TOS` using `setsockopt`
- [ ] Register callback in `start_http_server` <!-- id: 1 -->
    - [ ] Set `config.open_fn = mjpeg_stream_on_sess_open`
- [ ] Verify build and functionality <!-- id: 2 -->
    - [ ] Run `idf.py build` to ensure no compilation errors
    - [ ] Manual verification: Connect to MJPEG stream and check for stutters (on-device)
