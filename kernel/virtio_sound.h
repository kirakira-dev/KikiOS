/*
 * KikiOS Virtio Sound Driver
 *
 * Implements virtio-snd for audio playback on QEMU virt machine.
 * Based on virtio 1.2 spec (modern mode).
 */

#ifndef VIRTIO_SOUND_H
#define VIRTIO_SOUND_H

#include <stdint.h>

// Initialize the virtio sound device
// Returns 0 on success, -1 on failure
int virtio_sound_init(void);

// Play raw PCM audio with configurable format
// data: pointer to PCM samples (S16LE)
// samples: number of samples per channel (not bytes)
// channels: 1 = mono, 2 = stereo
// sample_rate: sample rate in Hz (e.g. 44100, 48000)
// Returns 0 on success, -1 on failure
int virtio_sound_play_pcm(const int16_t *data, uint32_t samples, uint8_t channels, uint32_t sample_rate);

// Play raw PCM audio (legacy - assumes stereo 44100Hz)
// data: pointer to PCM samples (S16LE, stereo)
// samples: number of samples (not bytes)
// Returns 0 on success, -1 on failure
int virtio_sound_play(const int16_t *data, uint32_t samples);

// Play a WAV file from memory
// data: pointer to WAV file data
// size: size of WAV file in bytes
// Returns 0 on success, -1 on failure
int virtio_sound_play_wav(const void *data, uint32_t size);

// Stop playback
void virtio_sound_stop(void);

// Pause playback (can be resumed)
void virtio_sound_pause(void);

// Resume paused playback
// Returns 0 on success, -1 on failure
int virtio_sound_resume(void);

// Check if sound is currently playing
int virtio_sound_is_playing(void);

// Check if sound is paused
int virtio_sound_is_paused(void);

// Set volume (0-100)
void virtio_sound_set_volume(int volume);

// Get current playback position in samples
uint32_t virtio_sound_get_position(void);

// Async playback - starts playing and returns immediately
// The PCM buffer must remain valid until playback completes!
int virtio_sound_play_pcm_async(const int16_t *data, uint32_t samples, uint8_t channels, uint32_t sample_rate);

// Pump audio data - call periodically (e.g., from timer) to feed audio
void virtio_sound_pump(void);

#endif // VIRTIO_SOUND_H
