# RTMP ABR Transcoder

A high-performance RTMP relay server written in C++20 using POSIX sockets and epoll for non-blocking I/O.

## Features

- RTMP relay server with publisher support
- Complete RTMP handshake implementation
- RTMP chunk parsing and encoding (formats 0-3)
- AMF0 encoding/decoding
- Single-threaded epoll event loop
- Bounded buffers with backpressure policies
- Graceful shutdown on SIGINT/SIGTERM
- Structured logging

## Build Instructions

```bash
mkdir -p build
cd build
cmake ..
cmake --build . -j
```

This produces two binaries:
- `rtmp_abr_transcoder` - Main relay daemon
- `tests/rtmp_abr_tests` - Test suite

## Running Tests

```bash
./build/tests/rtmp_abr_tests
```

## Usage

### Relay Mode (Forward streams to destination)

```bash
./build/rtmp_abr_transcoder \
  --listen 0.0.0.0:1935 \
  --push-url rtmp://127.0.0.1:1936/live \
  --push-template "{app}/{stream}" \
  --max-pending-bytes 2097152 \
  --pusher-down-disconnect-ms 2000 \
  --log-level info
```

### Sink Mode (Receive only, no forwarding)

```bash
./build/rtmp_abr_transcoder \
  --listen 0.0.0.0:1935 \
  --mode sink \
  --log-level debug
```

## Command Line Options

- `--listen <addr:port>` - Listen address (default: 0.0.0.0:1935)
- `--push-url <url>` - Destination RTMP URL (required for relay mode)
- `--push-template <template>` - Stream path template (default: {app}/{stream})
- `--max-pending-bytes <bytes>` - Max pending bytes buffer (default: 2097152)
- `--pusher-down-disconnect-ms <ms>` - Pusher disconnect timeout (default: 2000)
- `--log-level <level>` - Log level: debug|info (default: info)
- `--mode <mode>` - Mode: relay|sink (default: relay)
- `--help` - Show help message

## Manual Verification

### Test with FFmpeg

1. Start the relay server:
```bash
./build/rtmp_abr_transcoder --listen 0.0.0.0:1935 --push-url rtmp://127.0.0.1:1936/live
```

2. Start a sink server (in another terminal):
```bash
./build/rtmp_abr_transcoder --listen 0.0.0.0:1936 --mode sink
```

3. Publish a test stream with FFmpeg:
```bash
ffmpeg -re -f lavfi -i testsrc=size=640x480:rate=30 \
  -f lavfi -i sine=frequency=1000:sample_rate=44100 \
  -c:v libx264 -preset ultrafast -c:a aac \
  -f flv rtmp://127.0.0.1:1935/live/test
```

4. You should see logs showing:
   - Publisher connection on port 1935
   - Stream relay to port 1936
   - Messages being forwarded

### Test with OBS Studio

1. Start the relay:
```bash
./build/rtmp_abr_transcoder --listen 0.0.0.0:1935 --push-url rtmp://your-cdn.com/live
```

2. Configure OBS:
   - Server: rtmp://127.0.0.1:1935/live
   - Stream Key: mystream

3. Start streaming in OBS

## Architecture

### Core Components

- **core/** - Utilities (bytes, CLI, logging, time, result types)
- **net/** - Network layer (epoll loop, sockets, buffers)
- **rtmp/** - RTMP protocol (AMF0, chunks, messages, handshake, sessions, server/client)
- **relay/** - Relay logic (stream management, policies)

### RTMP Protocol Support

- **Handshake**: Simple handshake (C0/C1/C2, S0/S1/S2)
- **Chunk Formats**: 0, 1, 2, 3
- **Message Types**: SetChunkSize (1), Ack (3), WindowAckSize (5), SetPeerBandwidth (6), Audio (8), Video (9), Data (18), Command (20)
- **AMF0 Types**: Number, Boolean, String, Null, Object, ECMA Array
- **Commands**: connect, createStream, publish

### Design Decisions

- Single-threaded event loop for simplicity and performance
- Non-blocking I/O with edge-triggered epoll
- Bounded buffers to prevent unbounded memory growth
- Exponential backoff for pusher reconnects (capped at 5s)
- Structured logging with timestamps

## License

MIT