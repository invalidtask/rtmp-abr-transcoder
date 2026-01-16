# RTMP ABR Transcoder

A high-performance RTMP server with CPU-based H.264/AAC transcoding written in C++20 using POSIX sockets and epoll for non-blocking I/O.

## Features

- **RTMP relay server** with publisher support
- **CPU-based transcoding** using OpenH264 and FDK-AAC
- Multiple output resolutions and bitrates from single source
- Complete RTMP handshake implementation
- RTMP chunk parsing and encoding (formats 0-3)
- AMF0 encoding/decoding
- Single-threaded epoll event loop
- Bounded buffers with backpressure policies
- Graceful shutdown on SIGINT/SIGTERM
- Structured logging

## Dependencies

- OpenH264 (Cisco's H.264 codec - BSD licensed)
- FDK-AAC (Fraunhofer AAC codec)
- libyuv (Google's YUV scaling library)

Install on Ubuntu/Debian:
```bash
sudo apt install libopenh264-dev libfdk-aac-dev libyuv-dev
```

## Build Instructions

```bash
mkdir -p build
cd build
cmake ..
cmake --build . -j
```

This produces two binaries:
- `rtmp_abr_transcoder` - Main server daemon
- `tests/rtmp_abr_tests` - Test suite

## Running Tests

```bash
./build/tests/rtmp_abr_tests
```

## Usage

### Transcode Mode (CPU-based H.264/AAC transcoding)

Receive a stream and transcode to multiple resolutions/bitrates:

```bash
./build/rtmp_abr_transcoder \
  --listen 0.0.0.0:1935 \
  --mode transcode \
  --output "720p:1280x720@2500,rtmp://live.server.net/live/stream_720p" \
  --output "360p:640x360@1000,rtmp://live.server.net/live/stream_360p" \
  --log-level info
```

Output format: `name:widthxheight@video_bitrate_kbps,rtmp_url`

When OBS streams 1080p@6000kbps to the transcoder:
1. Server receives H.264/AAC stream
2. Decoder extracts raw YUV frames and PCM audio
3. Scaler resizes to 720p and 360p
4. Encoders compress at target bitrates
5. RTMP pushers send to destination servers

### Relay Mode (Forward streams to destination)

```bash
./build/rtmp_abr_transcoder \
  --listen 0.0.0.0:1935 \
  --mode relay \
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
- `--mode <mode>` - Mode: relay|sink|transcode (default: relay)
- `--output <spec>` - Output for transcode mode (format: name:WxH@kbps,url)
- `--push-url <url>` - Destination RTMP URL (required for relay mode)
- `--push-template <template>` - Stream path template (default: {app}/{stream})
- `--max-pending-bytes <bytes>` - Max pending bytes buffer (default: 2097152)
- `--pusher-down-disconnect-ms <ms>` - Pusher disconnect timeout (default: 2000)
- `--log-level <level>` - Log level: debug|info (default: info)
- `--help` - Show help message

## Manual Verification

### Test Transcode Mode with FFmpeg

1. Start two sink servers to receive transcoded outputs:
```bash
# Terminal 1 - 720p sink
./build/rtmp_abr_transcoder --listen 0.0.0.0:1936 --mode sink --log-level info

# Terminal 2 - 360p sink
./build/rtmp_abr_transcoder --listen 0.0.0.0:1937 --mode sink --log-level info
```

2. Start the transcoder (in another terminal):
```bash
./build/rtmp_abr_transcoder \
  --listen 0.0.0.0:1935 \
  --mode transcode \
  --output "720p:1280x720@2500,rtmp://127.0.0.1:1936/live/stream_720p" \
  --output "360p:640x360@1000,rtmp://127.0.0.1:1937/live/stream_360p" \
  --log-level info
```

3. Publish a test stream with FFmpeg:
```bash
ffmpeg -re -f lavfi -i testsrc=size=1920x1080:rate=30 \
  -f lavfi -i sine=frequency=1000:sample_rate=44100 \
  -c:v libx264 -preset ultrafast -b:v 6000k -c:a aac -b:a 128k \
  -f flv rtmp://127.0.0.1:1935/live/test
```

4. You should see logs showing:
   - Publisher connection on port 1935
   - Source stream: 1920x1080 @ 30fps, H.264/AAC
   - Transcoding to 720p@2500kbps
   - Transcoding to 360p@1000kbps
   - Decoded frames, scaled frames, encoded packets
   - Outputs connecting to destination servers

### Test Relay Mode with FFmpeg

1. Start the relay server:
```bash
./build/rtmp_abr_transcoder --listen 0.0.0.0:1935 --mode relay --push-url rtmp://127.0.0.1:1936/live
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

#### Transcode Mode

1. Start the transcoder:
```bash
./build/rtmp_abr_transcoder \
  --listen 0.0.0.0:1935 \
  --mode transcode \
  --output "720p:1280x720@2500,rtmp://your-cdn.com/live/stream_720p" \
  --output "360p:640x360@1000,rtmp://your-cdn.com/live/stream_360p"
```

2. Configure OBS:
   - Server: rtmp://127.0.0.1:1935/live
   - Stream Key: mystream

3. Start streaming in OBS

#### Relay Mode

1. Start the relay:
```bash
./build/rtmp_abr_transcoder --listen 0.0.0.0:1935 --mode relay --push-url rtmp://your-cdn.com/live
```

2. Configure OBS:
   - Server: rtmp://127.0.0.1:1935/live
   - Stream Key: mystream

3. Start streaming in OBS

## Transcoding Architecture

```
[OBS/FFmpeg Source] 
       ↓
   [RTMP Ingest]
       ↓
   [FLV Demuxer] - Extract H.264 NALUs and AAC frames
       ↓
   [H.264 Decoder] - Decode to raw YUV frames (OpenH264)
       ↓
   [Scaler] - Resize frames (libyuv)
       ↓
   ┌──────────────────────────────────────┐
   │            [Encoder Pool]            │
   ├──────────────────────────────────────┤
   │  [720p Encoder]     [360p Encoder]   │
   │   H.264@2500kbps    H.264@1000kbps   │
   │   AAC@128kbps       AAC@64kbps       │
   └──────────────────────────────────────┘
       ↓                    ↓
   [FLV Muxer]          [FLV Muxer]
       ↓                    ↓
   [RTMP Push 720p]    [RTMP Push 360p]
```

## Architecture

### Core Components

- **core/** - Utilities (bytes, CLI, logging, time, result types)
- **net/** - Network layer (epoll loop, sockets, buffers)
- **rtmp/** - RTMP protocol (AMF0, chunks, messages, handshake, sessions, server/client)
- **relay/** - Relay logic (stream management, policies)
- **transcode/** - Transcoding (H.264/AAC decode, encode, scaling)

### Transcoding Components

- **H264Decoder** - OpenH264-based H.264 decoder (extracts YUV420 frames)
- **AACDecoder** - FDK-AAC-based AAC decoder (extracts PCM audio)
- **H264Encoder** - OpenH264-based H.264 encoder (configurable bitrate, resolution)
- **AACEncoder** - FDK-AAC-based AAC encoder (configurable bitrate, sample rate)
- **Scaler** - libyuv-based YUV scaler (fast bilinear scaling)
- **Transcoder** - Orchestrates decode → scale → encode → push pipeline

### RTMP Protocol Support

- **Handshake**: Simple handshake (C0/C1/C2, S0/S1/S2)
- **Chunk Formats**: 0, 1, 2, 3
- **Message Types**: SetChunkSize (1), Ack (3), WindowAckSize (5), SetPeerBandwidth (6), Audio (8), Video (9), Data (18), Command (20)
- **AMF0 Types**: Number, Boolean, String, Null, Object, ECMA Array
- **Commands**: connect, createStream, publish

### Design Decisions

- Single-threaded event loop for simplicity and performance
- Non-blocking I/O with edge-triggered epoll
- CPU-based transcoding (no GPU/hardware acceleration)
- Bounded buffers to prevent unbounded memory growth
- Exponential backoff for pusher reconnects (capped at 5s)
- Structured logging with timestamps

## License

MIT