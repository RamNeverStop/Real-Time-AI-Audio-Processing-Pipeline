#pragma once

/**
 * @file audio_pipeline.h
 * @brief Shared constants, globals, and function declarations for the audio pipeline.
 */

#include <pulse/simple.h>
#include <pulse/error.h>
#include <samplerate.h>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <cmath>
#include <pthread.h>

// ── Sample Rate & Frame Constants ─────────────────────────────────────────────

/// Microphone capture sample rate (Hz)
constexpr int CAPTURE_SAMPLE_RATE   = 48000;
/// Number of samples per capture frame
constexpr int CAPTURE_FRAME_SIZE    = 1024;
/// Byte size of one capture frame (float32)
constexpr int CAPTURE_BUFFER_SIZE   = CAPTURE_FRAME_SIZE * sizeof(float);

/// Downsampled rate fed to the AI model (Hz)
constexpr int DOWNSAMPLED_RATE      = 6000;
/// Number of samples per downsampled frame
constexpr int DOWNSAMPLED_FRAME_SIZE = 64;

/// Upsampled / playback rate (Hz)
constexpr int UPSAMPLED_RATE        = 48000;
/// Number of samples per playback frame
constexpr int UPSAMPLED_FRAME_SIZE  = 1024;
/// Byte size of one playback frame (float32)
constexpr int PLAYBACK_BUFFER_SIZE  = UPSAMPLED_FRAME_SIZE * sizeof(float);

// ── Shared Circular Buffer ─────────────────────────────────────────────────────

extern std::vector<float>          circular_buffer;
extern int                         buffer_start;
extern int                         buffer_end;
extern std::mutex                  buffer_mutex;
extern std::condition_variable     buffer_cv;
extern bool                        buffer_ready;

// ── DSP Functions ──────────────────────────────────────────────────────────────

/**
 * @brief Resample audio from input_rate to output_rate using libsamplerate SINC interpolation.
 */
std::vector<float> resample(const std::vector<float>& input,
                             int input_rate,
                             int output_rate,
                             int output_size);

/**
 * @brief Apply a first-order IIR low-pass filter in-place.
 * @param data            Audio samples (modified in-place).
 * @param cutoff_frequency Cutoff frequency in Hz.
 * @param sample_rate     Sample rate of the data in Hz.
 */
void low_pass_filter(std::vector<float>& data,
                     float cutoff_frequency,
                     int   sample_rate);

// ── Thread Functions ───────────────────────────────────────────────────────────

/**
 * @brief Capture thread: reads mic audio → filters → downsamples → circular buffer.
 */
void capture_thread(pa_simple* capture, int* error);

/**
 * @brief Playback thread: circular buffer → [AI model] → upsample → filters → speaker.
 */
void playback_thread(pa_simple* playback, int* error);

// ── PulseAudio Helpers ─────────────────────────────────────────────────────────

bool create_capture_stream (pa_simple** capture,  pa_sample_spec* spec, int* error);
bool create_playback_stream(pa_simple** playback, pa_sample_spec* spec, int* error);
bool capture_audio(pa_simple* capture,  float* buffer, int* error);
bool play_audio   (pa_simple* playback, const float* buffer, int* error);
