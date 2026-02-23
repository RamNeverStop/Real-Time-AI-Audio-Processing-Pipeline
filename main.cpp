/**
 * @file main.cpp
 * @brief Real-Time Audio Processing Pipeline with AI Integration Point
 *
 * Captures live audio at 48 kHz, downsamples to 6 kHz for AI/ML inference,
 * then upsamples back to 48 kHz for playback.
 *
 * Dependencies: PulseAudio, libsamplerate
 * Build: See CMakeLists.txt
 */

#include "audio_pipeline.h"
#include <iostream>
#include <thread>
#include <pthread.h>

int main() {
    pa_simple* capture  = nullptr;
    pa_simple* playback = nullptr;

    pa_sample_spec capture_spec  = { PA_SAMPLE_FLOAT32, CAPTURE_SAMPLE_RATE, 1 };
    pa_sample_spec playback_spec = { PA_SAMPLE_FLOAT32, UPSAMPLED_RATE,      1 };
    int error = 0;

    // ── 1. Initialize PulseAudio Streams ───────────────────────────────────
    if (!create_capture_stream(&capture, &capture_spec, &error)) {
        std::cerr << "FATAL: Failed to create capture stream: "
                  << pa_strerror(error) << std::endl;
        return 1;
    }

    if (!create_playback_stream(&playback, &playback_spec, &error)) {
        std::cerr << "FATAL: Failed to create playback stream: "
                  << pa_strerror(error) << std::endl;
        pa_simple_free(capture);
        return 1;
    }

    // ── 2. Configure Real-Time Scheduling ──────────────────────────────────
    sched_param param;
    param.sched_priority = 99; // Highest non-kernel priority

    // ── 3. Launch Capture & Playback Threads ───────────────────────────────
    std::thread capture_thr([capture, &error]()  { capture_thread(capture,  &error); });
    std::thread playback_thr([playback, &error]() { playback_thread(playback, &error); });

    if (pthread_setschedparam(capture_thr.native_handle(), SCHED_FIFO, &param) != 0)
        std::cerr << "WARNING: Could not set SCHED_FIFO for Capture Thread. Check permissions.\n";

    if (pthread_setschedparam(playback_thr.native_handle(), SCHED_FIFO, &param) != 0)
        std::cerr << "WARNING: Could not set SCHED_FIFO for Playback Thread. Check permissions.\n";

    capture_thr.join();
    playback_thr.join();

    // ── 4. Cleanup ─────────────────────────────────────────────────────────
    pa_simple_free(capture);
    pa_simple_free(playback);

    return 0;
}
