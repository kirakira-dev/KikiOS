/*
 * KikiOS Virtio Sound Driver
 *
 * Implements virtio-snd for audio playback on QEMU virt machine.
 * Based on virtio 1.2 spec (modern mode).
 *
 * Device ID: 25
 * Virtqueues:
 *   0 - controlq (control messages)
 *   1 - eventq (device events)
 *   2 - txq (audio output)
 *   3 - rxq (audio input)
 */

#include "virtio_sound.h"
#include "printf.h"
#include "string.h"

// Virtio MMIO registers
#define VIRTIO_MMIO_BASE        0x0a000000
#define VIRTIO_MMIO_STRIDE      0x200

// Virtio MMIO register offsets
#define VIRTIO_MMIO_MAGIC           0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_VENDOR_ID       0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
#define VIRTIO_MMIO_QUEUE_READY     0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK   0x064
#define VIRTIO_MMIO_STATUS          0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW  0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0a4
#define VIRTIO_MMIO_CONFIG          0x100

// Virtio status bits
#define VIRTIO_STATUS_ACK         1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8

// Virtio device types
#define VIRTIO_DEV_SOUND  25

// Virtio sound virtqueue indices
#define VIRTIO_SND_VQ_CONTROL  0
#define VIRTIO_SND_VQ_EVENT    1
#define VIRTIO_SND_VQ_TX       2
#define VIRTIO_SND_VQ_RX       3

// Virtio sound request codes
#define VIRTIO_SND_R_JACK_INFO       0x0001
#define VIRTIO_SND_R_JACK_REMAP      0x0002
#define VIRTIO_SND_R_PCM_INFO        0x0100
#define VIRTIO_SND_R_PCM_SET_PARAMS  0x0101
#define VIRTIO_SND_R_PCM_PREPARE     0x0102
#define VIRTIO_SND_R_PCM_RELEASE     0x0103
#define VIRTIO_SND_R_PCM_START       0x0104
#define VIRTIO_SND_R_PCM_STOP        0x0105
#define VIRTIO_SND_R_CHMAP_INFO      0x0200

// Virtio sound status codes
#define VIRTIO_SND_S_OK        0x8000
#define VIRTIO_SND_S_BAD_MSG   0x8001
#define VIRTIO_SND_S_NOT_SUPP  0x8002
#define VIRTIO_SND_S_IO_ERR    0x8003

// PCM stream direction
#define VIRTIO_SND_D_OUTPUT  0
#define VIRTIO_SND_D_INPUT   1

// PCM sample formats
#define VIRTIO_SND_PCM_FMT_IMA_ADPCM  0
#define VIRTIO_SND_PCM_FMT_MU_LAW     1
#define VIRTIO_SND_PCM_FMT_A_LAW      2
#define VIRTIO_SND_PCM_FMT_S8         3
#define VIRTIO_SND_PCM_FMT_U8         4
#define VIRTIO_SND_PCM_FMT_S16        5
#define VIRTIO_SND_PCM_FMT_U16        6
#define VIRTIO_SND_PCM_FMT_S18_3      7
#define VIRTIO_SND_PCM_FMT_U18_3      8
#define VIRTIO_SND_PCM_FMT_S20_3      9
#define VIRTIO_SND_PCM_FMT_U20_3      10
#define VIRTIO_SND_PCM_FMT_S24_3      11
#define VIRTIO_SND_PCM_FMT_U24_3      12
#define VIRTIO_SND_PCM_FMT_S20        13
#define VIRTIO_SND_PCM_FMT_U20        14
#define VIRTIO_SND_PCM_FMT_S24        15
#define VIRTIO_SND_PCM_FMT_U24        16
#define VIRTIO_SND_PCM_FMT_S32        17
#define VIRTIO_SND_PCM_FMT_U32        18
#define VIRTIO_SND_PCM_FMT_FLOAT      19
#define VIRTIO_SND_PCM_FMT_FLOAT64    20

// PCM sample rates (as indices)
#define VIRTIO_SND_PCM_RATE_5512    0
#define VIRTIO_SND_PCM_RATE_8000    1
#define VIRTIO_SND_PCM_RATE_11025   2
#define VIRTIO_SND_PCM_RATE_16000   3
#define VIRTIO_SND_PCM_RATE_22050   4
#define VIRTIO_SND_PCM_RATE_32000   5
#define VIRTIO_SND_PCM_RATE_44100   6
#define VIRTIO_SND_PCM_RATE_48000   7
#define VIRTIO_SND_PCM_RATE_64000   8
#define VIRTIO_SND_PCM_RATE_88200   9
#define VIRTIO_SND_PCM_RATE_96000   10
#define VIRTIO_SND_PCM_RATE_176400  11
#define VIRTIO_SND_PCM_RATE_192000  12
#define VIRTIO_SND_PCM_RATE_384000  13

// Virtqueue structures
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} virtq_avail_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[];
} virtq_used_t;

// Virtio sound config space
typedef struct __attribute__((packed)) {
    uint32_t jacks;
    uint32_t streams;
    uint32_t chmaps;
} virtio_snd_config_t;

// Generic request header
typedef struct __attribute__((packed)) {
    uint32_t code;
} virtio_snd_hdr_t;

// PCM request header
typedef struct __attribute__((packed)) {
    uint32_t code;
    uint32_t stream_id;
} virtio_snd_pcm_hdr_t;

// Query info request
typedef struct __attribute__((packed)) {
    uint32_t code;
    uint32_t start_id;
    uint32_t count;
    uint32_t size;
} virtio_snd_query_info_t;

// PCM info response (per stream)
typedef struct __attribute__((packed)) {
    uint32_t hda_fn_nid;
    uint32_t features;
    uint64_t formats;      // Bitmask of supported formats
    uint64_t rates;        // Bitmask of supported rates
    uint8_t  direction;
    uint8_t  channels_min;
    uint8_t  channels_max;
    uint8_t  padding[5];
} virtio_snd_pcm_info_t;

// PCM set params request
typedef struct __attribute__((packed)) {
    uint32_t code;
    uint32_t stream_id;
    uint32_t buffer_bytes;
    uint32_t period_bytes;
    uint32_t features;
    uint8_t  channels;
    uint8_t  format;
    uint8_t  rate;
    uint8_t  padding;
} virtio_snd_pcm_set_params_t;

// PCM xfer header (for TX/RX queues)
typedef struct __attribute__((packed)) {
    uint32_t stream_id;
} virtio_snd_pcm_xfer_t;

// PCM status response
typedef struct __attribute__((packed)) {
    uint32_t status;
    uint32_t latency_bytes;
} virtio_snd_pcm_status_t;

// Driver state
static volatile uint32_t *snd_base = NULL;
static int snd_device_index = -1;

// Queue structures - one set per queue
#define QUEUE_SIZE 64
#define DESC_F_NEXT  1
#define DESC_F_WRITE 2

// Control queue
static uint8_t ctrl_queue_mem[4096] __attribute__((aligned(4096)));
static virtq_desc_t *ctrl_desc = NULL;
static virtq_avail_t *ctrl_avail = NULL;
static virtq_used_t *ctrl_used = NULL;
static uint16_t ctrl_last_used = 0;

// TX queue (audio output)
static uint8_t tx_queue_mem[4096] __attribute__((aligned(4096)));
static virtq_desc_t *tx_desc = NULL;
static virtq_avail_t *tx_avail = NULL;
static virtq_used_t *tx_used = NULL;
static uint16_t tx_last_used = 0;

// Request/response buffers
static virtio_snd_hdr_t ctrl_response __attribute__((aligned(16)));
static virtio_snd_pcm_status_t tx_status __attribute__((aligned(16)));

// Audio playback state
static int playing = 0;
static uint32_t playback_position = 0;

// Async playback state
static const uint8_t *async_pcm_data = NULL;
static uint32_t async_pcm_bytes = 0;
static uint32_t async_pcm_offset = 0;
static int async_playing = 0;
static int async_paused = 0;
static uint8_t async_channels = 2;
static uint32_t async_sample_rate = 44100;

// Memory barriers for device communication
static inline void mb(void) {
    asm volatile("dsb sy" ::: "memory");
}

static inline uint32_t read32(volatile uint32_t *addr) {
    uint32_t val = *addr;
    mb();
    return val;
}

static inline void write32(volatile uint32_t *addr, uint32_t val) {
    mb();
    *addr = val;
    mb();
}

static volatile uint32_t *find_virtio_sound(void) {
    for (int i = 0; i < 32; i++) {
        volatile uint32_t *base = (volatile uint32_t *)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STRIDE);

        uint32_t magic = read32(base + VIRTIO_MMIO_MAGIC/4);
        uint32_t device_id = read32(base + VIRTIO_MMIO_DEVICE_ID/4);

        if (magic == 0x74726976 && device_id == VIRTIO_DEV_SOUND) {
            snd_device_index = i;
            return base;
        }
    }

    return NULL;
}

static int setup_queue(int queue_num, uint8_t *queue_mem,
                       virtq_desc_t **desc_out, virtq_avail_t **avail_out, virtq_used_t **used_out) {
    write32(snd_base + VIRTIO_MMIO_QUEUE_SEL/4, queue_num);

    uint32_t max_queue = read32(snd_base + VIRTIO_MMIO_QUEUE_NUM_MAX/4);
    if (max_queue == 0) {
        printf("[SND] Queue %d not available\n", queue_num);
        return -1;
    }

    uint32_t queue_size = (max_queue < QUEUE_SIZE) ? max_queue : QUEUE_SIZE;
    write32(snd_base + VIRTIO_MMIO_QUEUE_NUM/4, queue_size);

    // Setup queue memory layout
    *desc_out = (virtq_desc_t *)queue_mem;
    *avail_out = (virtq_avail_t *)(queue_mem + queue_size * sizeof(virtq_desc_t));
    *used_out = (virtq_used_t *)(queue_mem + 2048);  // Aligned offset

    uint64_t desc_addr = (uint64_t)*desc_out;
    uint64_t avail_addr = (uint64_t)*avail_out;
    uint64_t used_addr = (uint64_t)*used_out;

    write32(snd_base + VIRTIO_MMIO_QUEUE_DESC_LOW/4, (uint32_t)desc_addr);
    write32(snd_base + VIRTIO_MMIO_QUEUE_DESC_HIGH/4, (uint32_t)(desc_addr >> 32));
    write32(snd_base + VIRTIO_MMIO_QUEUE_AVAIL_LOW/4, (uint32_t)avail_addr);
    write32(snd_base + VIRTIO_MMIO_QUEUE_AVAIL_HIGH/4, (uint32_t)(avail_addr >> 32));
    write32(snd_base + VIRTIO_MMIO_QUEUE_USED_LOW/4, (uint32_t)used_addr);
    write32(snd_base + VIRTIO_MMIO_QUEUE_USED_HIGH/4, (uint32_t)(used_addr >> 32));

    (*avail_out)->flags = 0;
    (*avail_out)->idx = 0;

    write32(snd_base + VIRTIO_MMIO_QUEUE_READY/4, 1);

    return 0;
}

// Send a control request and wait for response
static int send_ctrl_request(void *request, uint32_t req_len, void *response, uint32_t resp_len) {
    // Setup descriptor chain: request (device reads) -> response (device writes)
    ctrl_desc[0].addr = (uint64_t)request;
    ctrl_desc[0].len = req_len;
    ctrl_desc[0].flags = DESC_F_NEXT;
    ctrl_desc[0].next = 1;

    ctrl_desc[1].addr = (uint64_t)response;
    ctrl_desc[1].len = resp_len;
    ctrl_desc[1].flags = DESC_F_WRITE;
    ctrl_desc[1].next = 0;

    // Add to available ring
    mb();
    uint16_t old_used_idx = ctrl_used->idx;
    uint16_t avail_slot = ctrl_avail->idx % QUEUE_SIZE;
    ctrl_avail->ring[avail_slot] = 0;
    mb();
    ctrl_avail->idx++;
    mb();

    // Notify device
    write32(snd_base + VIRTIO_MMIO_QUEUE_NOTIFY/4, VIRTIO_SND_VQ_CONTROL);

    // Poll for completion
    int timeout = 10000000;
    while (ctrl_used->idx == old_used_idx && timeout > 0) {
        mb();
        timeout--;
    }

    if (timeout == 0) {
        printf("[SND] Control request timed out\n");
        return -1;
    }

    ctrl_last_used = ctrl_used->idx;

    // Ack interrupt
    write32(snd_base + VIRTIO_MMIO_INTERRUPT_ACK/4, read32(snd_base + VIRTIO_MMIO_INTERRUPT_STATUS/4));

    return 0;
}

int virtio_sound_init(void) {
    printf("[SND] Initializing sound...\n");

    snd_base = find_virtio_sound();
    if (!snd_base) {
        printf("[SND] No virtio-sound device found\n");
        return -1;
    }

    printf("[SND] Found virtio-sound at device %d\n", snd_device_index);

    // Reset device (with timeout to prevent hang)
    write32(snd_base + VIRTIO_MMIO_STATUS/4, 0);
    int reset_timeout = 100000;
    while (read32(snd_base + VIRTIO_MMIO_STATUS/4) != 0 && --reset_timeout > 0) {
        asm volatile("nop");
    }
    if (reset_timeout == 0) {
        printf("[SND] Device reset timeout\n");
        return -1;
    }

    // Acknowledge and set driver
    write32(snd_base + VIRTIO_MMIO_STATUS/4, VIRTIO_STATUS_ACK);
    write32(snd_base + VIRTIO_MMIO_STATUS/4, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    // Read device config
    volatile uint8_t *config = (volatile uint8_t *)snd_base + VIRTIO_MMIO_CONFIG;
    uint32_t jacks = *(volatile uint32_t *)(config + 0);
    uint32_t streams = *(volatile uint32_t *)(config + 4);
    uint32_t chmaps = *(volatile uint32_t *)(config + 8);
    printf("[SND] Config: jacks=%d streams=%d chmaps=%d\n", jacks, streams, chmaps);

    if (streams == 0) {
        printf("[SND] No PCM streams available\n");
        return -1;
    }

    // Accept no special features
    write32(snd_base + VIRTIO_MMIO_DEVICE_FEATURES_SEL/4, 0);
    write32(snd_base + VIRTIO_MMIO_DRIVER_FEATURES_SEL/4, 0);
    write32(snd_base + VIRTIO_MMIO_DRIVER_FEATURES/4, 0);
    write32(snd_base + VIRTIO_MMIO_STATUS/4,
            VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

    uint32_t status = read32(snd_base + VIRTIO_MMIO_STATUS/4);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        printf("[SND] Feature negotiation failed\n");
        return -1;
    }

    // Setup control queue
    if (setup_queue(VIRTIO_SND_VQ_CONTROL, ctrl_queue_mem, &ctrl_desc, &ctrl_avail, &ctrl_used) < 0) {
        printf("[SND] Failed to setup control queue\n");
        return -1;
    }

    // Setup TX queue (audio output)
    if (setup_queue(VIRTIO_SND_VQ_TX, tx_queue_mem, &tx_desc, &tx_avail, &tx_used) < 0) {
        printf("[SND] Failed to setup TX queue\n");
        return -1;
    }

    // Mark driver ready
    write32(snd_base + VIRTIO_MMIO_STATUS/4,
            VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    status = read32(snd_base + VIRTIO_MMIO_STATUS/4);
    if (status & 0x40) {
        printf("[SND] Device failure\n");
        return -1;
    }

    // Query PCM stream info
    static virtio_snd_query_info_t query __attribute__((aligned(16)));
    static virtio_snd_hdr_t query_resp __attribute__((aligned(16)));
    static virtio_snd_pcm_info_t pcm_info __attribute__((aligned(16)));

    query.code = VIRTIO_SND_R_PCM_INFO;
    query.start_id = 0;
    query.count = 1;
    query.size = sizeof(virtio_snd_pcm_info_t);

    // For PCM_INFO, we need: request -> response header -> pcm_info array
    // Setup 3-descriptor chain
    ctrl_desc[0].addr = (uint64_t)&query;
    ctrl_desc[0].len = sizeof(query);
    ctrl_desc[0].flags = DESC_F_NEXT;
    ctrl_desc[0].next = 1;

    ctrl_desc[1].addr = (uint64_t)&query_resp;
    ctrl_desc[1].len = sizeof(query_resp);
    ctrl_desc[1].flags = DESC_F_NEXT | DESC_F_WRITE;
    ctrl_desc[1].next = 2;

    ctrl_desc[2].addr = (uint64_t)&pcm_info;
    ctrl_desc[2].len = sizeof(pcm_info);
    ctrl_desc[2].flags = DESC_F_WRITE;
    ctrl_desc[2].next = 0;

    mb();
    uint16_t old_used = ctrl_used->idx;
    ctrl_avail->ring[ctrl_avail->idx % QUEUE_SIZE] = 0;
    mb();
    ctrl_avail->idx++;
    mb();
    write32(snd_base + VIRTIO_MMIO_QUEUE_NOTIFY/4, VIRTIO_SND_VQ_CONTROL);

    int timeout = 10000000;
    while (ctrl_used->idx == old_used && timeout > 0) {
        mb();
        timeout--;
    }

    write32(snd_base + VIRTIO_MMIO_INTERRUPT_ACK/4, read32(snd_base + VIRTIO_MMIO_INTERRUPT_STATUS/4));

    if (timeout == 0) {
        printf("[SND] PCM_INFO timed out\n");
    } else {
        printf("[SND] PCM stream 0: direction=%d, channels=%d-%d, formats=0x%x, rates=0x%x\n",
               pcm_info.direction, pcm_info.channels_min, pcm_info.channels_max,
               (uint32_t)pcm_info.formats, (uint32_t)pcm_info.rates);
    }

    printf("[SND] Sound initialized!\n");
    return 0;
}

// Configure stream for playback
static int configure_stream(uint8_t channels, uint8_t format, uint8_t rate) {
    static virtio_snd_pcm_set_params_t params __attribute__((aligned(16)));

    params.code = VIRTIO_SND_R_PCM_SET_PARAMS;
    params.stream_id = 0;
    params.buffer_bytes = 32768;  // 32KB buffer
    params.period_bytes = 4096;   // 4KB periods
    params.features = 0;
    params.channels = channels;
    params.format = format;
    params.rate = rate;
    params.padding = 0;

    if (send_ctrl_request(&params, sizeof(params), &ctrl_response, sizeof(ctrl_response)) < 0) {
        return -1;
    }

    if (ctrl_response.code != VIRTIO_SND_S_OK) {
        printf("[SND] SET_PARAMS failed: 0x%x\n", ctrl_response.code);
        return -1;
    }

    return 0;
}

// Prepare stream for playback
static int prepare_stream(void) {
    static virtio_snd_pcm_hdr_t hdr __attribute__((aligned(16)));

    hdr.code = VIRTIO_SND_R_PCM_PREPARE;
    hdr.stream_id = 0;

    if (send_ctrl_request(&hdr, sizeof(hdr), &ctrl_response, sizeof(ctrl_response)) < 0) {
        return -1;
    }

    if (ctrl_response.code != VIRTIO_SND_S_OK) {
        printf("[SND] PREPARE failed: 0x%x\n", ctrl_response.code);
        return -1;
    }

    return 0;
}

// Start stream
static int start_stream(void) {
    static virtio_snd_pcm_hdr_t hdr __attribute__((aligned(16)));

    hdr.code = VIRTIO_SND_R_PCM_START;
    hdr.stream_id = 0;

    if (send_ctrl_request(&hdr, sizeof(hdr), &ctrl_response, sizeof(ctrl_response)) < 0) {
        return -1;
    }

    if (ctrl_response.code != VIRTIO_SND_S_OK) {
        printf("[SND] START failed: 0x%x\n", ctrl_response.code);
        return -1;
    }

    return 0;
}

// Stop stream
static int stop_stream(void) {
    static virtio_snd_pcm_hdr_t hdr __attribute__((aligned(16)));

    hdr.code = VIRTIO_SND_R_PCM_STOP;
    hdr.stream_id = 0;

    if (send_ctrl_request(&hdr, sizeof(hdr), &ctrl_response, sizeof(ctrl_response)) < 0) {
        return -1;
    }

    if (ctrl_response.code != VIRTIO_SND_S_OK) {
        printf("[SND] STOP failed: 0x%x\n", ctrl_response.code);
        return -1;
    }

    return 0;
}

// Submit audio data to TX queue
static int submit_audio(const void *data, uint32_t size) {
    static virtio_snd_pcm_xfer_t xfer __attribute__((aligned(16)));

    xfer.stream_id = 0;

    // Descriptor chain: xfer header -> audio data -> status response
    tx_desc[0].addr = (uint64_t)&xfer;
    tx_desc[0].len = sizeof(xfer);
    tx_desc[0].flags = DESC_F_NEXT;
    tx_desc[0].next = 1;

    tx_desc[1].addr = (uint64_t)data;
    tx_desc[1].len = size;
    tx_desc[1].flags = DESC_F_NEXT;
    tx_desc[1].next = 2;

    tx_desc[2].addr = (uint64_t)&tx_status;
    tx_desc[2].len = sizeof(tx_status);
    tx_desc[2].flags = DESC_F_WRITE;
    tx_desc[2].next = 0;

    mb();
    uint16_t old_used = tx_used->idx;
    tx_avail->ring[tx_avail->idx % QUEUE_SIZE] = 0;
    mb();
    tx_avail->idx++;
    mb();

    // Notify TX queue
    write32(snd_base + VIRTIO_MMIO_QUEUE_NOTIFY/4, VIRTIO_SND_VQ_TX);

    // Wait for completion
    int timeout = 50000000;  // Longer timeout for audio
    while (tx_used->idx == old_used && timeout > 0) {
        mb();
        timeout--;
    }

    write32(snd_base + VIRTIO_MMIO_INTERRUPT_ACK/4, read32(snd_base + VIRTIO_MMIO_INTERRUPT_STATUS/4));

    if (timeout == 0) {
        printf("[SND] Audio submit timed out\n");
        return -1;
    }

    if (tx_status.status != VIRTIO_SND_S_OK) {
        printf("[SND] Audio submit failed: 0x%x\n", tx_status.status);
        return -1;
    }

    tx_last_used = tx_used->idx;
    return 0;
}

// Convert sample rate in Hz to virtio rate index
static int hz_to_rate_index(uint32_t hz) {
    switch (hz) {
        case 5512:   return VIRTIO_SND_PCM_RATE_5512;
        case 8000:   return VIRTIO_SND_PCM_RATE_8000;
        case 11025:  return VIRTIO_SND_PCM_RATE_11025;
        case 16000:  return VIRTIO_SND_PCM_RATE_16000;
        case 22050:  return VIRTIO_SND_PCM_RATE_22050;
        case 32000:  return VIRTIO_SND_PCM_RATE_32000;
        case 44100:  return VIRTIO_SND_PCM_RATE_44100;
        case 48000:  return VIRTIO_SND_PCM_RATE_48000;
        case 64000:  return VIRTIO_SND_PCM_RATE_64000;
        case 88200:  return VIRTIO_SND_PCM_RATE_88200;
        case 96000:  return VIRTIO_SND_PCM_RATE_96000;
        case 176400: return VIRTIO_SND_PCM_RATE_176400;
        case 192000: return VIRTIO_SND_PCM_RATE_192000;
        default:     return -1;  // Unsupported
    }
}

int virtio_sound_play_pcm(const int16_t *data, uint32_t samples, uint8_t channels, uint32_t sample_rate) {
    if (!snd_base) return -1;

    int rate_idx = hz_to_rate_index(sample_rate);
    if (rate_idx < 0) {
        printf("[SND] Unsupported sample rate: %d\n", sample_rate);
        return -1;
    }

    if (configure_stream(channels, VIRTIO_SND_PCM_FMT_S16, rate_idx) < 0) {
        return -1;
    }

    if (prepare_stream() < 0) {
        return -1;
    }

    if (start_stream() < 0) {
        return -1;
    }

    playing = 1;
    playback_position = 0;

    // Submit audio in chunks
    uint32_t bytes = samples * channels * sizeof(int16_t);
    uint32_t chunk_size = 4096;  // Match period_bytes
    const uint8_t *ptr = (const uint8_t *)data;

    while (bytes > 0 && playing) {
        uint32_t to_send = (bytes < chunk_size) ? bytes : chunk_size;

        if (submit_audio(ptr, to_send) < 0) {
            break;
        }

        ptr += to_send;
        bytes -= to_send;
        playback_position += to_send / (channels * sizeof(int16_t));
    }

    stop_stream();
    playing = 0;

    return 0;
}

int virtio_sound_play(const int16_t *data, uint32_t samples) {
    // Legacy function - assume stereo 44100Hz
    return virtio_sound_play_pcm(data, samples, 2, 44100);
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

int virtio_sound_play_wav(const void *data, uint32_t size) {
    if (!snd_base) return -1;
    if (size < sizeof(wav_header_t) + 8) return -1;

    const uint8_t *ptr = (const uint8_t *)data;
    const wav_header_t *hdr = (const wav_header_t *)ptr;

    // Verify RIFF/WAVE header
    if (hdr->riff[0] != 'R' || hdr->riff[1] != 'I' ||
        hdr->riff[2] != 'F' || hdr->riff[3] != 'F') {
        printf("[SND] Not a RIFF file\n");
        return -1;
    }

    if (hdr->wave[0] != 'W' || hdr->wave[1] != 'A' ||
        hdr->wave[2] != 'V' || hdr->wave[3] != 'E') {
        printf("[SND] Not a WAVE file\n");
        return -1;
    }

    if (hdr->audio_format != 1) {
        printf("[SND] Only PCM format supported (got %d)\n", hdr->audio_format);
        return -1;
    }

    printf("[SND] WAV: %dHz, %d-bit, %d channels\n",
           hdr->sample_rate, hdr->bits_per_sample, hdr->channels);

    // Find format and rate indices
    uint8_t format;
    if (hdr->bits_per_sample == 8) {
        format = VIRTIO_SND_PCM_FMT_U8;
    } else if (hdr->bits_per_sample == 16) {
        format = VIRTIO_SND_PCM_FMT_S16;
    } else if (hdr->bits_per_sample == 32) {
        format = VIRTIO_SND_PCM_FMT_S32;
    } else {
        printf("[SND] Unsupported bit depth: %d\n", hdr->bits_per_sample);
        return -1;
    }

    uint8_t rate;
    switch (hdr->sample_rate) {
        case 8000:   rate = VIRTIO_SND_PCM_RATE_8000; break;
        case 11025:  rate = VIRTIO_SND_PCM_RATE_11025; break;
        case 16000:  rate = VIRTIO_SND_PCM_RATE_16000; break;
        case 22050:  rate = VIRTIO_SND_PCM_RATE_22050; break;
        case 32000:  rate = VIRTIO_SND_PCM_RATE_32000; break;
        case 44100:  rate = VIRTIO_SND_PCM_RATE_44100; break;
        case 48000:  rate = VIRTIO_SND_PCM_RATE_48000; break;
        case 96000:  rate = VIRTIO_SND_PCM_RATE_96000; break;
        default:
            printf("[SND] Unsupported sample rate: %d\n", hdr->sample_rate);
            return -1;
    }

    // Find data chunk
    ptr += sizeof(wav_header_t);
    uint32_t remaining = size - sizeof(wav_header_t);

    // Skip any extra fmt data
    if (hdr->fmt_size > 16) {
        uint32_t extra = hdr->fmt_size - 16;
        if (extra > remaining) return -1;
        ptr += extra;
        remaining -= extra;
    }

    // Find "data" chunk
    while (remaining >= 8) {
        if (ptr[0] == 'd' && ptr[1] == 'a' && ptr[2] == 't' && ptr[3] == 'a') {
            uint32_t data_size = *(uint32_t *)(ptr + 4);
            ptr += 8;
            remaining -= 8;

            if (data_size > remaining) {
                data_size = remaining;
            }

            printf("[SND] Playing %d bytes of audio...\n", data_size);

            // Configure and play
            if (configure_stream(hdr->channels, format, rate) < 0) {
                return -1;
            }

            if (prepare_stream() < 0) {
                return -1;
            }

            if (start_stream() < 0) {
                return -1;
            }

            playing = 1;
            playback_position = 0;

            // Submit audio in chunks
            uint32_t chunk_size = 4096;
            const uint8_t *audio_ptr = ptr;
            uint32_t bytes_left = data_size;

            while (bytes_left > 0 && playing) {
                uint32_t to_send = (bytes_left < chunk_size) ? bytes_left : chunk_size;

                if (submit_audio(audio_ptr, to_send) < 0) {
                    break;
                }

                audio_ptr += to_send;
                bytes_left -= to_send;
                playback_position += to_send / hdr->block_align;
            }

            stop_stream();
            playing = 0;

            printf("[SND] Playback complete\n");
            return 0;
        }

        // Skip unknown chunk
        uint32_t chunk_size = *(uint32_t *)(ptr + 4);
        ptr += 8 + chunk_size;
        if (8 + chunk_size > remaining) break;
        remaining -= 8 + chunk_size;
    }

    printf("[SND] No data chunk found\n");
    return -1;
}

void virtio_sound_stop(void) {
    if (!snd_base) return;
    playing = 0;
    async_playing = 0;
    async_paused = 0;
    async_pcm_data = NULL;
    stop_stream();
}

// Pause async playback - can be resumed later
void virtio_sound_pause(void) {
    if (!snd_base) return;
    if (!async_playing) return;  // Nothing to pause

    // Stop the stream but keep state
    stop_stream();
    async_playing = 0;
    async_paused = 1;
    playing = 0;
}

// Resume paused playback
int virtio_sound_resume(void) {
    if (!snd_base) return -1;
    if (!async_paused || !async_pcm_data) return -1;  // Nothing to resume

    int rate_idx = hz_to_rate_index(async_sample_rate);
    if (rate_idx < 0) return -1;

    // Reconfigure and restart stream
    if (configure_stream(async_channels, VIRTIO_SND_PCM_FMT_S16, rate_idx) < 0) {
        return -1;
    }

    if (prepare_stream() < 0) {
        return -1;
    }

    if (start_stream() < 0) {
        return -1;
    }

    // Resume from where we left off
    async_playing = 1;
    async_paused = 0;
    playing = 1;

    // Submit next chunk
    virtio_sound_pump();

    return 0;
}

int virtio_sound_is_paused(void) {
    return async_paused;
}

int virtio_sound_is_playing(void) {
    return playing;
}

void virtio_sound_set_volume(int volume) {
    (void)volume;  // Not implemented yet
}

uint32_t virtio_sound_get_position(void) {
    return playback_position;
}

// Non-blocking audio submit - returns immediately, doesn't wait for completion
static int submit_audio_async(const void *data, uint32_t size) {
    static virtio_snd_pcm_xfer_t xfer __attribute__((aligned(16)));

    xfer.stream_id = 0;

    // Descriptor chain: xfer header -> audio data -> status response
    tx_desc[0].addr = (uint64_t)&xfer;
    tx_desc[0].len = sizeof(xfer);
    tx_desc[0].flags = DESC_F_NEXT;
    tx_desc[0].next = 1;

    tx_desc[1].addr = (uint64_t)data;
    tx_desc[1].len = size;
    tx_desc[1].flags = DESC_F_NEXT;
    tx_desc[1].next = 2;

    tx_desc[2].addr = (uint64_t)&tx_status;
    tx_desc[2].len = sizeof(tx_status);
    tx_desc[2].flags = DESC_F_WRITE;
    tx_desc[2].next = 0;

    mb();
    tx_avail->ring[tx_avail->idx % QUEUE_SIZE] = 0;
    mb();
    tx_avail->idx++;
    mb();

    // Notify TX queue and return immediately
    write32(snd_base + VIRTIO_MMIO_QUEUE_NOTIFY/4, VIRTIO_SND_VQ_TX);

    return 0;
}

// Check if previous async submit completed
static int async_submit_ready(void) {
    static uint16_t last_checked_idx = 0;

    mb();
    if (tx_used->idx != last_checked_idx) {
        last_checked_idx = tx_used->idx;
        write32(snd_base + VIRTIO_MMIO_INTERRUPT_ACK/4,
                read32(snd_base + VIRTIO_MMIO_INTERRUPT_STATUS/4));
        return 1;
    }
    return 0;
}

// Start async playback - returns immediately
int virtio_sound_play_pcm_async(const int16_t *data, uint32_t samples, uint8_t channels, uint32_t sample_rate) {
    if (!snd_base) return -1;

    // Stop any current playback
    if (async_playing || async_paused) {
        virtio_sound_stop();
    }

    int rate_idx = hz_to_rate_index(sample_rate);
    if (rate_idx < 0) {
        printf("[SND] Unsupported sample rate: %d\n", sample_rate);
        return -1;
    }

    if (configure_stream(channels, VIRTIO_SND_PCM_FMT_S16, rate_idx) < 0) {
        return -1;
    }

    if (prepare_stream() < 0) {
        return -1;
    }

    if (start_stream() < 0) {
        return -1;
    }

    // Store async state
    async_pcm_data = (const uint8_t *)data;
    async_pcm_bytes = samples * channels * sizeof(int16_t);
    async_pcm_offset = 0;
    async_playing = 1;
    async_paused = 0;
    async_channels = channels;
    async_sample_rate = sample_rate;
    playing = 1;
    playback_position = 0;

    // Submit first chunk
    virtio_sound_pump();

    return 0;
}

// Called periodically (e.g., from timer) to feed more audio data
void virtio_sound_pump(void) {
    if (!async_playing || !async_pcm_data) return;

    // Check if device is ready for more data
    if (!async_submit_ready() && async_pcm_offset > 0) {
        return;  // Previous chunk still processing
    }

    // Check if we're done
    if (async_pcm_offset >= async_pcm_bytes) {
        stop_stream();
        async_playing = 0;
        playing = 0;
        async_pcm_data = NULL;
        return;
    }

    // Submit next chunk
    uint32_t chunk_size = 4096;  // Match period_bytes
    uint32_t remaining = async_pcm_bytes - async_pcm_offset;
    uint32_t to_send = (remaining < chunk_size) ? remaining : chunk_size;

    submit_audio_async(async_pcm_data + async_pcm_offset, to_send);
    async_pcm_offset += to_send;
    playback_position = async_pcm_offset / 4;  // Approx samples (stereo S16)
}
