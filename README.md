# 🎙️ Real-Time AI Audio Processing Pipeline (C++)

A compact, production-ready **real-time audio pipeline** built in C++ that captures live microphone audio, downsamples it for AI/ML inference, and reconstructs high-quality output for playback — all with low-latency, thread-safe architecture.

```
Mic (48 kHz) → Low-Pass Filter → Downsample (6 kHz) → [AI Model] → Upsample (48 kHz) → Speaker
```

---

## 📋 Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Architecture](#architecture)
- [Prerequisites](#prerequisites)
- [Build & Run](#build--run)
- [Project Structure](#project-structure)
- [Configuration](#configuration)
- [Integrating an AI Model](#integrating-an-ai-model)
- [Use Cases](#use-cases)
- [Technical Details](#technical-details)
- [Contributing](#contributing)
- [License](#license)

---

## Overview

Most AI/ML audio models don't operate on raw 48 kHz audio. They expect **downsampled, filtered, low-bandwidth inputs**. This pipeline creates exactly that bridge between a live audio source and any AI model requiring pre-processed audio.

The pipeline is modeled after the architectures used in:
- **Google AudioLM** — generative audio modelling
- **Meta EnCodec** — neural audio compression
- **NVIDIA Speech Super Resolution** — bandwidth extension
- **Low-bitrate VoIP systems** — 6 kHz telephony

---

## Features

| Feature | Detail |
|---|---|
| 🎤 Live Audio Capture | PulseAudio Simple API at 48 kHz |
| 🔽 Downsampling | 48 kHz → 6 kHz via SINC interpolation |
| 🔁 Circular Buffer | Thread-safe producer/consumer queue |
| 🤖 AI Integration Point | Drop-in slot for any inference model |
| 🔼 Upsampling | 6 kHz → 48 kHz via libsamplerate |
| 🔈 Live Playback | Reconstructed audio streamed to speaker |
| ⚡ Real-Time Scheduling | `SCHED_FIFO` for glitch-free audio |
| 🎛️ Anti-Aliasing Filters | IIR low-pass before and after resampling |

---

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                        CAPTURE THREAD                        │
│                                                              │
│  Microphone → [48 kHz raw] → Low-Pass (1 kHz) →             │
│  Downsample → [6 kHz] → Circular Buffer (write)             │
└───────────────────────────┬──────────────────────────────────┘
                            │  mutex + condition_variable
┌───────────────────────────▼──────────────────────────────────┐
│                       PLAYBACK THREAD                        │
│                                                              │
│  Circular Buffer (read) → [6 kHz] → ┌──────────────────┐   │
│                                      │  AI MODEL (ONNX, │   │
│                                      │  HiFi-GAN, etc.) │   │
│                                      └────────┬─────────┘   │
│  Upsample → [48 kHz] → Low-Pass (2 kHz) → Speaker           │
└──────────────────────────────────────────────────────────────┘
```

---

## Prerequisites

### System (Ubuntu / Debian)

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    pkg-config \
    libpulse-dev \
    libsamplerate0-dev
```

### System (Fedora / RHEL)

```bash
sudo dnf install -y \
    gcc-c++ cmake pkg-config \
    pulseaudio-libs-devel \
    libsamplerate-devel
```

### System (Arch Linux)

```bash
sudo pacman -S cmake pkg-config libpulse libsamplerate
```

---

## Build & Run

```bash
# 1. Clone the repository
git clone https://github.com/your-username/realtime-audio-pipeline.git
cd realtime-audio-pipeline

# 2. Configure with CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 3. Build
cmake --build build --parallel

# 4. Run
./build/audio_pipeline
```

> **Note:** Real-time scheduling (`SCHED_FIFO`) requires elevated privileges. If you see a scheduling warning, run with `sudo` or configure `rlimit` for your user.

```bash
# Option A — run as root
sudo ./build/audio_pipeline

# Option B — grant the binary CAP_SYS_NICE
sudo setcap cap_sys_nice+ep ./build/audio_pipeline
```

---

## Project Structure

```
realtime-audio-pipeline/
├── CMakeLists.txt          # Build configuration
├── README.md
├── .gitignore
└── src/
    ├── main.cpp            # Entry point — thread launch & PulseAudio init
    ├── audio_pipeline.h    # Constants, declarations, shared globals
    └── audio_pipeline.cpp  # DSP functions, threads, PulseAudio helpers
```

---

## Configuration

All tunable parameters live in `src/audio_pipeline.h`:

```cpp
// Capture
constexpr int CAPTURE_SAMPLE_RATE    = 48000;  // Microphone rate (Hz)
constexpr int CAPTURE_FRAME_SIZE     = 1024;   // Samples per capture frame

// AI Model Input
constexpr int DOWNSAMPLED_RATE       = 6000;   // Target rate for inference
constexpr int DOWNSAMPLED_FRAME_SIZE = 64;     // Samples per inference frame

// Playback / Output
constexpr int UPSAMPLED_RATE         = 48000;  // Speaker output rate (Hz)
constexpr int UPSAMPLED_FRAME_SIZE   = 1024;   // Samples per playback frame
```

Change `DOWNSAMPLED_RATE` to match whatever sample rate your AI model expects (e.g. `8000`, `16000`, `22050`).

---

## Integrating an AI Model

The **AI insertion point** is clearly marked inside `src/audio_pipeline.cpp`, in the `playback_thread` function:

```cpp
// ── AI MODEL INSERTION POINT ──────────────────────────────────
//
// `model_buffer` contains one frame of 6 kHz audio (64 samples).
//
// Replace the upsample call below with your inference, e.g.:
//
//   model_buffer = ai_model.infer(model_buffer);
//
// ─────────────────────────────────────────────────────────────

std::vector<float> upsampled = resample(
    model_buffer, DOWNSAMPLED_RATE, UPSAMPLED_RATE, UPSAMPLED_FRAME_SIZE);
```

### Compatible Model Types

| Model Type | Example |
|---|---|
| Speech super-resolution | NVIDIA Super Resolution, NU-Wave |
| Denoising autoencoder | RNNoise, DeepFilterNet |
| Neural vocoder | HiFi-GAN, WaveRNN, LPCNet |
| ONNX Runtime model | Any `.onnx` model via `onnxruntime-c` |
| TensorFlow Lite | TFLite C API |

### ONNX Runtime Example Stub

```cpp
#include <onnxruntime_cxx_api.h>

Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "audio");
Ort::Session session(env, "model.onnx", Ort::SessionOptions{});

// Inside playback_thread, at the AI insertion point:
std::vector<int64_t> input_shape = {1, static_cast<int64_t>(model_buffer.size())};
Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
    mem, model_buffer.data(), model_buffer.size(), input_shape.data(), 2);

const char* input_names[]  = {"input"};
const char* output_names[] = {"output"};
auto outputs = session.Run({}, input_names, &input_tensor, 1, output_names, 1);
// `outputs[0]` contains the model's enhanced audio frame
```

---

## Use Cases

| Domain | Application |
|---|---|
| 🔊 Smart Assistants | Far-field voice capture, wake-word detection |
| 📞 Telephony / VoIP | Low-bitrate → high-quality reconstruction |
| 🦻 Hearing Assistance | Noise filtering + AI speech enhancement |
| 🤖 Edge AI Devices | ESP32-S3, Raspberry Pi, NVIDIA Jetson |
| 📡 Audio Analytics | Anomaly / gunshot / cough / baby-cry detection |
| 🎧 Audio Super-Resolution | Wideband reconstruction from narrowband audio |

---

## Technical Details

### Resampling (libsamplerate)
Uses `SRC_SINC_MEDIUM_QUALITY` — a windowed SINC interpolation that balances CPU cost with audio quality. Switch to `SRC_SINC_BEST_QUALITY` for offline/studio use or `SRC_LINEAR` for resource-constrained embedded targets.

### Low-Pass Filter
A first-order IIR (RC approximation) applied at two points:
- **Before downsampling** — anti-aliasing at 1 kHz to prevent spectral fold-over
- **After upsampling** — smoothing at 2 kHz to remove interpolation artefacts

### Circular Buffer
A lock-protected ring buffer sized at `CAPTURE_FRAME_SIZE × 16` samples decouples the capture and playback threads. On overflow the oldest sample is silently dropped (suitable for real-time; adjust policy for recording use cases).

### Real-Time Scheduling
Both threads request `SCHED_FIFO` priority 99 via `pthread_setschedparam`. This prevents the OS scheduler from preempting audio threads, eliminating glitches under moderate system load.

---

## Contributing

Contributions are welcome! Ideas for extension:
- ONNX Runtime inference integration
- ALSA backend alternative to PulseAudio
- Configurable filter orders (biquad / FIR)
- Web dashboard for real-time spectrum visualization
- Benchmark harness for comparing AI model latency

Please open an issue first to discuss major changes.

---

## License

MIT License — see [LICENSE](LICENSE) for details.

---

*Built with PulseAudio · libsamplerate · C++17 · pthreads*
