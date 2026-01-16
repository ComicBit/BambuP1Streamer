# Bambu P1 Camera Streamer

Only tested on a P1S. I would expect it to work for a p1p camera. I would not expect this to work on an X1/X1C - the codecs are different and I don't believe that local network streaming is enabled. 

Built and tested on Debian 12 / amd64. Other platforms may not work.

Derived from https://github.com/hisptoot/BambuSource2Raw.  

https://github.com/AlexxIT/go2rtc does most of the work.

# AUTOMATED BUILDING

This section is for the TL;DR approach.

## Quickstart

### Using Docker Compose (Recommended)

```bash
git clone https://github.com/slynn1324/BambuP1Streamer.git
cd BambuP1Streamer
# Copy and edit the environment file with your printer details
cp .env.example .env
# Edit .env with your PRINTER_ADDRESS and PRINTER_ACCESS_CODE
# Then start the service
docker compose up -d
# View logs
docker compose logs -f
```

The service will be available at:
- go2rtc web interface: http://localhost:1984
- Stream status API: http://localhost:8081/stream_started

### Using Podman

```bash
sudo apt update
sudo apt install podman
git clone https://github.com/slynn1324/BambuP1Streamer.git
cd BambuP1Streamer
podman build -t bambu_p1_streamer .
podman run --name bambu_p1_streamer -p 1984:1984 -p 8081:8081 \
  -e PRINTER_ADDRESS=10.1.1.13 -e PRINTER_ACCESS_CODE=24952313 \
  localhost/bambu_p1_streamer
```


# MANUAL BUILDING

This section is going through all the steps manually one by one.

## DEPENDENCIES

Bambu Studio Proprietary Plugin Library
```
wget https://public-cdn.bambulab.com/upgrade/studio/plugins/01.04.00.15/linux_01.04.00.15.zip
unzip linux_01.04.00.15.zip
```

Go2Rtc
```
wget https://github.com/AlexxIT/go2rtc/releases/download/v1.6.2/go2rtc_linux_amd64
chmod a+x go2rtc_linux_amd64
```

## BUILD
replace `podman` with `docker` if that's what you're running. 

### build binary
```
podman run --rm -v $(pwd):/work docker.io/gcc:12 gcc /work/src/BambuP1Streamer.cpp -o /work/BambuP1Streamer 
```

### build container
```
podman build -t bambu_p1_streamer .
```

## RUN
Plug in the right values for the environment variables
```
podman run -d --name bambu_p1_streamer -p 1984:1984 -e PRINTER_ADDRESS=192.168.12.34 -e PRINTER_ACCESS_CODE=12345678 bambu_p1_streamer
```

## ACCESS
### Index Page (only the MJPEG parts will work)
```
http://<host>:1984/links.html?src=p1s
```

### MJPEG url
```
http://norm:1984/api/stream.mjpeg?src=p1s
```

### WebSocket
go2rtc has a unique feature for "mjpeg-over-websocket" that may demonstrate lower latency and better control than a regular MJPEG image in a browser.  This however will require creating a custom player (TODO) to leverage, but could better emulate a video control. 

WebSocket url:
```
ws://<host>:1984/api/ws?src=p1s
```

WebSocket pseudo-code:
```
1) connect to websocket
2) send 'mjpeg'
3) receive 'mjpeg'
4) receive binary messages with each frame
	update displayed impage with received data (data/base64 url)
5) disconnect web socket to stop
```

# Troubleshooting

### Bambu_Open failed: 0xffffff9b
This error on the containers output seems to occur when the PRINTER_ADDRESS destination is somehow unreachable.

### [1] [BambuTunnel::read_sample] error [1]
This error on the containers output seems to occur when the PRINTER_ACCESS_CODE is not correct.

### Harmless errors on http://localhost:1984/stream.html?src=p1s
Sometimes a few error messages appear before the stream starts. One would just wait a few more seconds.

# Home Assistant / Polling Endpoint

A lightweight HTTP status server runs on port `8081` (configurable via `HTTP_PORT` env var) to monitor stream status. **The status server starts immediately, but reports `"started": true` only when a client is actively streaming.**

### How It Works

- The status server starts immediately when the container launches
- `BambuP1Streamer` only starts when go2rtc receives a stream request (when someone views the stream)
- While streaming, BambuP1Streamer updates a heartbeat file every second
- The status server checks this heartbeat file (stream is active if updated within last 5 seconds)
- When the stream stops, the status returns to `false`

### Available Endpoints

- **Stream status:** `GET /stream_started`  
  Returns JSON `{"started": true}` when a client is actively viewing the stream, otherwise `{"started": false}`.

- **Health check:** `GET /health`  
  Returns `{"ok": true}`.

### Configuration

Set the HTTP server port in your `.env` file:
```bash
HTTP_PORT=8081
```

### Container Logs

When the container starts:
```
==========================================
Starting Bambu P1 Streamer Container
==========================================
Printer Address: 192.168.1.100
HTTP Status Port: 8081
==========================================
Starting HTTP Status Server on port 8081...
========================================
[StatusServer] HTTP Status Server Started
[StatusServer] Listening on port 8081
[StatusServer] Endpoints:
[StatusServer]   - GET /stream_started
[StatusServer]   - GET /health
========================================
StatusServer started successfully (PID: 7)
==========================================
Note: BambuP1Streamer will start automatically
when go2rtc receives a stream request.
==========================================
Starting go2rtc...
```

When someone starts viewing the stream:
```
Starting Session
Stream started successfully
```

When requests are made:
```
[StatusServer] GET /stream_started -> {"started":true}
[StatusServer] GET /stream_started -> {"started":false}
```

### Usage Example

Home Assistant can poll the stream status endpoint to detect when someone is actively viewing the Bambu stream:

```bash
curl http://<host>:8081/stream_started
```

When running in a container, expose port 8080:
```bash
podman run -d --name bambu_p1_streamer -p 1984:1984 -p 8080:8080 \
  -e PRINTER_ADDRESS=192.168.12.34 -e PRINTER_ACCESS_CODE=12345678 \
  bambu_p1_streamer
```
