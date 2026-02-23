/**
 * @file audio_pipeline.cpp
 * @brief Implementation of DSP functions, capture/playback threads, and PulseAudio helpers.
 */

#include "audio_pipeline.h"
#include <iostream>

// ── Circular Buffer Globals ────────────────────────────────────────────────────

std::vector<float>       circular_buffer(CAPTURE_FRAME_SIZE * 16);
int                      buffer_start = 0;
int                      buffer_end   = 0;
std::mutex               buffer_mutex;
std::condition_variable  buffer_cv;
bool                     buffer_ready = false;

// ── DSP: Resampling ────────────────────────────────────────────────────────────

std::vector<float> resample(const std::vector<float>& input,
                             int input_rate,
                             int output_rate,
                             int output_size)
{
    std::vector<float> output(output_size, 0.0f);

    int src_error = 0;
    SRC_STATE* src_state = src_new(SRC_SINC_MEDIUM_QUALITY, 1, &src_error);
    if (!src_state) {
        std::cerr << "[resample] Failed to create SRC state: "
                  << src_strerror(src_error) << std::endl;
        return output;
    }

    SRC_DATA src_data{};
    src_data.data_in       = input.data();
    src_data.data_out      = output.data();
    src_data.input_frames  = static_cast<long>(input.size());
    src_data.output_frames = static_cast<long>(output.size());
    src_data.src_ratio     = static_cast<double>(output_rate) / input_rate;

    int result = src_process(src_state, &src_data);
    if (result != 0)
        std::cerr << "[resample] SRC processing error: " << src_strerror(result) << std::endl;

    src_delete(src_state);
    return output;
}

// ── DSP: Low-Pass Filter ───────────────────────────────────────────────────────

void low_pass_filter(std::vector<float>& data,
                     float cutoff_frequency,
                     int   sample_rate)
{
    if (data.empty()) return;

    // First-order IIR (RC approximation):  y[i] = y[i-1] + α·(x[i] − y[i-1])
    const float rc    = 1.0f / (cutoff_frequency * 2.0f * static_cast<float>(M_PI));
    const float dt    = 1.0f / static_cast<float>(sample_rate);
    const float alpha = dt / (rc + dt);

    float prev = data[0];
    for (size_t i = 1; i < data.size(); ++i) {
        data[i] = prev + alpha * (data[i] - prev);
        prev    = data[i];
    }
}

// ── Capture Thread ─────────────────────────────────────────────────────────────

void capture_thread(pa_simple* capture, int* error)
{
    pthread_setname_np(pthread_self(), "CaptureThread");

    std::vector<float> temp_buffer(CAPTURE_FRAME_SIZE);

    while (true) {
        // 1. Read raw 48 kHz audio from microphone
        if (!capture_audio(capture, temp_buffer.data(), error)) {
            std::cerr << "[CaptureThread] Read error: " << pa_strerror(*error) << std::endl;
            return;
        }

        // 2. Anti-aliasing filter — cut at 1 kHz (well below Nyquist of 3 kHz)
        low_pass_filter(temp_buffer, 1000.0f, CAPTURE_SAMPLE_RATE);

        // 3. Downsample 48 kHz → 6 kHz
        std::vector<float> downsampled = resample(
            temp_buffer, CAPTURE_SAMPLE_RATE, DOWNSAMPLED_RATE, DOWNSAMPLED_FRAME_SIZE);

        // 4. Write downsampled frames into the circular buffer
        {
            std::unique_lock<std::mutex> lock(buffer_mutex);
            for (float sample : downsampled) {
                circular_buffer[buffer_end] = sample;
                buffer_end = (buffer_end + 1) % static_cast<int>(circular_buffer.size());
                // Overwrite oldest sample on overflow
                if (buffer_end == buffer_start)
                    buffer_start = (buffer_start + 1) % static_cast<int>(circular_buffer.size());
            }
            buffer_ready = true;
        }
        buffer_cv.notify_one();
    }
}

// ── Playback Thread ────────────────────────────────────────────────────────────

void playback_thread(pa_simple* playback, int* error)
{
    pthread_setname_np(pthread_self(), "PlaybackThread");

    std::vector<float> model_buffer(DOWNSAMPLED_FRAME_SIZE);

    while (true) {
        // 1. Block until data is available
        {
            std::unique_lock<std::mutex> lock(buffer_mutex);
            buffer_cv.wait(lock, [] { return buffer_ready; });

            // 2. Read 6 kHz frame from circular buffer
            for (size_t i = 0; i < DOWNSAMPLED_FRAME_SIZE; ++i) {
                model_buffer[i] = circular_buffer[buffer_start];
                buffer_start = (buffer_start + 1) % static_cast<int>(circular_buffer.size());
            }

            if (buffer_start == buffer_end)
                buffer_ready = false;
        }

        // ── AI MODEL INSERTION POINT ──────────────────────────────────────
        //
        // `model_buffer` contains one frame of 6 kHz audio (64 samples).
        //
        // Replace the block below with your inference call, e.g.:
        //
        //   model_buffer = ai_model.infer(model_buffer);
        //
        // Compatible model types:
        //   • Speech super-resolution (e.g. NVIDIA Super Resolution)
        //   • Denoising autoencoder
        //   • Neural vocoder (HiFi-GAN, WaveRNN, LPCNet)
        //   • ONNX Runtime model
        //
        // ─────────────────────────────────────────────────────────────────

        // 3. Upsample 6 kHz → 48 kHz (replace with AI output when ready)
        std::vector<float> upsampled = resample(
            model_buffer, DOWNSAMPLED_RATE, UPSAMPLED_RATE, UPSAMPLED_FRAME_SIZE);

        // 4. Smoothing filter — cut at 2 kHz to remove interpolation artefacts
        low_pass_filter(upsampled, 2000.0f, UPSAMPLED_RATE);

        // 5. Send to speaker
        if (!play_audio(playback, upsampled.data(), error)) {
            std::cerr << "[PlaybackThread] Write error: " << pa_strerror(*error) << std::endl;
            return;
        }
    }
}

// ── PulseAudio Helpers ─────────────────────────────────────────────────────────

bool create_capture_stream(pa_simple** capture, pa_sample_spec* spec, int* error)
{
    *capture = pa_simple_new(nullptr, "AudioCapture", PA_STREAM_RECORD,
                             nullptr, "record", spec, nullptr, nullptr, error);
    return *capture != nullptr;
}

bool create_playback_stream(pa_simple** playback, pa_sample_spec* spec, int* error)
{
    *playback = pa_simple_new(nullptr, "AudioPlayback", PA_STREAM_PLAYBACK,
                              nullptr, "playback", spec, nullptr, nullptr, error);
    return *playback != nullptr;
}

bool capture_audio(pa_simple* capture, float* buffer, int* error)
{
    return pa_simple_read(capture, buffer, CAPTURE_BUFFER_SIZE, error) >= 0;
}

bool play_audio(pa_simple* playback, const float* buffer, int* error)
{
    return pa_simple_write(playback, buffer, PLAYBACK_BUFFER_SIZE, error) >= 0;
}
