/**Diese Header-Datei enthält keine Logik, sondern das gemeinsame 
interne Datenmodell + API zwischen den VirtIO-GPU-Teilmodulen.
**/

#pragma once

#include "virtio_gpu.hpp"
#include "virtqueue.hpp"

#include <cstddef>
#include <cstdint>

// Feste Queue-Indizes gemäß VirtIO-GPU-Spezifikation.
constexpr uint16_t CONTROL_QUEUE_INDEX = 0;
constexpr uint16_t CURSOR_QUEUE_INDEX = 1;

// Zielgrößen für die Ringpuffer.
constexpr uint16_t CONTROL_QUEUE_TARGET_SIZE = 64;
constexpr uint16_t CURSOR_QUEUE_TARGET_SIZE = 16;

// Default-Ausgabeparameter für den initialen Framebuffer.
constexpr uint32_t FRAME_WIDTH = 800;
constexpr uint32_t FRAME_HEIGHT = 600;
constexpr uint32_t BYTES_PER_PIXEL = 4;
constexpr uint32_t FRAME_RESOURCE_ID = 1;

// Maximaldauer für synchrone Control-Queue-Wartevorgänge.
constexpr uint32_t CTRL_TIMEOUT_MS = 1000;

// Feature-Bit für "modern" VirtIO 1.0.
constexpr uint32_t VIRTIO_F_VERSION_1_BIT = 32;

// Verwaltungszustand für die Control Queue inklusive Nachrichtenspeicher.
struct ControlQueue
{
    Virtqueue vq{};
    void *request{nullptr};
    uint64_t request_phys{0};
    size_t request_capacity{0};
    void *response{nullptr};
    uint64_t response_phys{0};
    size_t response_capacity{0};
    size_t dma_pages{0};
};

struct LegacyFramebufferInfo
{
    void *buffer{nullptr};
    uint32_t width{0};
    uint32_t height{0};
    uint8_t color_depth{0};
    uint32_t pitch{0};
};

// Gesamter Laufzeitzustand des Treibers.
struct VirtioGpuDevice
{
    bool ready{false};
    bool virgl{false};
    PciDeviceInfo pci{};
    VirtioPciRegions regs{};
    volatile VirtioPciCommonCfg *common{nullptr};
    volatile uint8_t *notify_base{nullptr};
    uint32_t notify_multiplier{0};
    volatile uint8_t *isr{nullptr};

    ControlQueue control{};
    Virtqueue cursor{};

    VirtioGpuRespDisplayInfo display_info{};

    void *framebuffer{nullptr};
    uint64_t framebuffer_phys{0};
    size_t framebuffer_pages{0};
    size_t framebuffer_bytes{0};
    uint32_t fb_width{FRAME_WIDTH};
    uint32_t fb_height{FRAME_HEIGHT};
};

// Globaler Singleton-Zustand des Treibers.
extern VirtioGpuDevice g_device;
extern LegacyFramebufferInfo g_legacy_lfb;

// Interne Hilfsfunktionen für Untermodule.
VirtqueueContext queue_context();
void memory_fence_release();
void memory_fence_acquire();
void virtio_log(const char *fmt, ...);
void *alloc_dma_pages(size_t page_count, uint64_t *out_phys);
void set_status_failed();

bool control_queue_init(ControlQueue &ctrl);
bool cursor_queue_init(Virtqueue &cursor);
bool control_queue_send(const void *req, size_t req_len, void *resp, size_t resp_len);
bool get_display_info();
bool create_framebuffer_resource();
bool virtio_gpu_demo_render_frame(uint32_t frame_index);
bool negotiate_features();
