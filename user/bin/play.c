/*
 * play - Play audio files (WAV, MP3)
 *
 * Usage: play <file.wav|file.mp3>
 */

#include "../lib/kiki.h"

// Disable SIMD - freestanding environment has no arm_neon.h
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "../../vendor/minimp3.h"

static kapi_t *api;

// Output helpers that use stdio hooks if available
static void out_putc(char c) {
    if (api->stdio_putc) api->stdio_putc(c);
    else api->putc(c);
}

static void out_puts(const char *s) {
    if (api->stdio_puts) api->stdio_puts(s);
    else api->puts(s);
}

static void out_int(int n) {
    if (n < 0) {
        out_putc('-');
        n = -n;
    }
    if (n == 0) {
        out_putc('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        out_putc(buf[--i]);
    }
}

// WAV file header structure
typedef struct __attribute__((packed)) {
    char riff[4];           // "RIFF"
    uint32_t file_size;     // File size - 8
    char wave[4];           // "WAVE"
    char fmt[4];            // "fmt "
    uint32_t fmt_size;      // Format chunk size (16 for PCM)
    uint16_t audio_format;  // 1 = PCM
    uint16_t channels;      // 1 = mono, 2 = stereo
    uint32_t sample_rate;   // e.g. 44100
    uint32_t byte_rate;     // sample_rate * channels * bits/8
    uint16_t block_align;   // channels * bits/8
    uint16_t bits_per_sample;
} wav_header_t;

// Check if file ends with given suffix
static int ends_with(const char *str, const char *suffix) {
    int str_len = strlen(str);
    int suffix_len = strlen(suffix);
    if (suffix_len > str_len) return 0;
    for (int i = 0; i < suffix_len; i++) {
        char c1 = str[str_len - suffix_len + i];
        char c2 = suffix[i];
        // Case insensitive
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return 0;
    }
    return 1;
}

// Play WAV file
static int play_wav(const uint8_t *data, uint32_t size) {
    if (size < sizeof(wav_header_t) + 8) {
        out_puts("Error: File too small for WAV\n");
        return -1;
    }

    const wav_header_t *hdr = (const wav_header_t *)data;

    // Verify RIFF/WAVE header
    if (hdr->riff[0] != 'R' || hdr->riff[1] != 'I' ||
        hdr->riff[2] != 'F' || hdr->riff[3] != 'F') {
        out_puts("Error: Not a RIFF file\n");
        return -1;
    }

    if (hdr->wave[0] != 'W' || hdr->wave[1] != 'A' ||
        hdr->wave[2] != 'V' || hdr->wave[3] != 'E') {
        out_puts("Error: Not a WAVE file\n");
        return -1;
    }

    if (hdr->audio_format != 1) {
        out_puts("Error: Not PCM format (compressed WAV not supported)\n");
        return -1;
    }

    if (hdr->bits_per_sample != 16) {
        out_puts("Error: Only 16-bit audio supported\n");
        return -1;
    }

    // Find data chunk (skip any extra chunks)
    const uint8_t *ptr = data + sizeof(wav_header_t);
    const uint8_t *end = data + size;

    // Skip any extra format bytes
    if (hdr->fmt_size > 16) {
        ptr += (hdr->fmt_size - 16);
    }

    // Find "data" chunk
    while (ptr + 8 < end) {
        if (ptr[0] == 'd' && ptr[1] == 'a' && ptr[2] == 't' && ptr[3] == 'a') {
            break;
        }
        // Skip this chunk
        uint32_t chunk_size = ptr[4] | (ptr[5] << 8) | (ptr[6] << 16) | (ptr[7] << 24);
        ptr += 8 + chunk_size;
    }

    if (ptr + 8 >= end) {
        out_puts("Error: No data chunk found\n");
        return -1;
    }

    uint32_t data_size = ptr[4] | (ptr[5] << 8) | (ptr[6] << 16) | (ptr[7] << 24);
    const int16_t *pcm_data = (const int16_t *)(ptr + 8);
    uint32_t samples = data_size / (hdr->channels * sizeof(int16_t));

    out_puts("WAV: ");
    out_int(hdr->sample_rate);
    out_puts(" Hz, ");
    out_int(hdr->channels);
    out_puts(" ch, ");
    out_int(samples);
    out_puts(" samples\n");

    // WAV data is in the file buffer which stays allocated, so async is safe
    return api->sound_play_pcm_async(pcm_data, samples, hdr->channels, hdr->sample_rate);
}

// Play MP3 file
static int play_mp3(const uint8_t *data, uint32_t size) {
    mp3dec_t mp3d;
    mp3dec_init(&mp3d);

    // First pass: count total samples needed
    mp3dec_frame_info_t info;
    int16_t temp_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

    const uint8_t *ptr = data;
    int remaining = size;
    uint32_t total_samples = 0;
    int channels = 0;
    int sample_rate = 0;

    // Scan through to get format info and total size
    while (remaining > 0) {
        int samples = mp3dec_decode_frame(&mp3d, ptr, remaining, temp_pcm, &info);
        if (samples > 0) {
            total_samples += samples;
            if (channels == 0) {
                channels = info.channels;
                sample_rate = info.hz;
            }
        }
        if (info.frame_bytes == 0) break;
        ptr += info.frame_bytes;
        remaining -= info.frame_bytes;
    }

    if (total_samples == 0 || channels == 0) {
        out_puts("Error: Could not decode MP3\n");
        return -1;
    }

    out_puts("MP3: ");
    out_int(sample_rate);
    out_puts(" Hz, ");
    out_int(channels);
    out_puts(" ch, ");
    out_int(total_samples);
    out_puts(" samples, ");
    out_int(info.bitrate_kbps);
    out_puts(" kbps\n");

    // Allocate buffer for all decoded PCM (always stereo for output)
    int16_t *pcm_buf = api->malloc(total_samples * 2 * sizeof(int16_t));
    if (!pcm_buf) {
        out_puts("Error: Out of memory for PCM buffer\n");
        return -1;
    }

    // Second pass: decode all frames
    mp3dec_init(&mp3d);
    ptr = data;
    remaining = size;
    int16_t *out_ptr = pcm_buf;

    out_puts("Decoding...\n");

    while (remaining > 0) {
        int samples = mp3dec_decode_frame(&mp3d, ptr, remaining, temp_pcm, &info);
        if (samples > 0) {
            // Convert to stereo if mono
            if (channels == 1) {
                for (int i = 0; i < samples; i++) {
                    *out_ptr++ = temp_pcm[i];  // Left
                    *out_ptr++ = temp_pcm[i];  // Right (duplicate)
                }
            } else {
                for (int i = 0; i < samples * 2; i++) {
                    *out_ptr++ = temp_pcm[i];
                }
            }
        }
        if (info.frame_bytes == 0) break;
        ptr += info.frame_bytes;
        remaining -= info.frame_bytes;
    }

    out_puts("Playing...\n");

    // Always play as stereo (async - returns immediately)
    // NOTE: pcm_buf must stay allocated while playing!
    int result = api->sound_play_pcm_async(pcm_buf, total_samples, 2, sample_rate);

    if (result < 0) {
        api->free(pcm_buf);
        return result;
    }

    // Don't free pcm_buf - kernel needs it during async playback
    // It will be orphaned but that's OK for now
    return 0;
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    if (argc < 2) {
        out_puts("Usage: play <file.wav|file.mp3>\n");
        return 1;
    }

    const char *filename = argv[1];

    // Check if sound is available
    if (!api->sound_play_pcm) {
        out_puts("Error: Sound not available\n");
        return 1;
    }

    // Open the file
    void *file = api->open(filename);
    if (!file) {
        out_puts("Error: Cannot open ");
        out_puts(filename);
        out_puts("\n");
        return 1;
    }

    if (api->is_dir(file)) {
        out_puts("Error: ");
        out_puts(filename);
        out_puts(" is a directory\n");
        return 1;
    }

    // Get file size
    int size = api->file_size(file);
    if (size <= 0) {
        out_puts("Error: Empty or invalid file\n");
        return 1;
    }

    out_puts("Loading ");
    out_puts(filename);
    out_puts(" (");
    out_int(size);
    out_puts(" bytes)...\n");

    // Allocate buffer
    uint8_t *data = api->malloc(size);
    if (!data) {
        out_puts("Error: Out of memory\n");
        return 1;
    }

    // Read file
    int offset = 0;
    while (offset < size) {
        int n = api->read(file, (char *)data + offset, size - offset, offset);
        if (n <= 0) break;
        offset += n;
    }

    if (offset != size) {
        out_puts("Warning: Only read ");
        out_int(offset);
        out_puts(" bytes\n");
    }

    int result;

    // Detect format and play
    if (ends_with(filename, ".mp3")) {
        result = play_mp3(data, size);
    } else if (ends_with(filename, ".wav")) {
        result = play_wav(data, size);
    } else {
        // Try to detect by magic bytes
        if (size >= 4 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F') {
            result = play_wav(data, size);
        } else if (size >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0) {
            // MP3 frame sync
            result = play_mp3(data, size);
        } else if (size >= 3 && data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
            // ID3 tag - it's an MP3
            result = play_mp3(data, size);
        } else {
            out_puts("Error: Unknown file format\n");
            result = -1;
        }
    }

    // Don't free data - kernel needs it during async playback
    // Memory will be orphaned but that's OK for a CLI tool

    if (result < 0) {
        api->free(data);  // Only free on error
        out_puts("Error: Playback failed\n");
        return 1;
    }

    out_puts("Done!\n");
    return 0;
}
