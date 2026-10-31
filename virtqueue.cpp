/*
baut und betreibt den generischen VirtIO-Ringpuffer.

Legt DMA-Speicher für desc, avail, used korrekt ausgerichtet an.
Bindet diese Adressen im Gerät (CommonCfg) ein und aktiviert die Queue.
Meldet neue Requests per virtqueue_notify an das Gerät.
Wartet per virtqueue_wait_used auf fertige Antworten (mit Timeout).
*/

#include "virtqueue.hpp"

#include "kernel/service/Service.h"
#include "kernel/service/MemoryService.h"
#include "lib/util/async/Thread.h"
#include "lib/util/time/Timestamp.h"
#include "lib/util/base/String.h"
#include "kernel/log/Log.h"

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>

using Kernel::MemoryService;
using Kernel::Service;

namespace
{
    // Ausrichtungshelfer für Ringsegmente (Descriptor/Avail/Used).
    constexpr size_t align_to(size_t value, size_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    // Speichersichtbarkeit Treiber -> Gerät.
    void memory_fence_release()
    {
        std::atomic_thread_fence(std::memory_order_release);
    }

    // Speichersichtbarkeit Gerät -> Treiber.
    void memory_fence_acquire()
    {
        std::atomic_thread_fence(std::memory_order_acquire);
    }

    // Polling-Helfer mit 1ms Granularität.
    void sleep_ms(uint32_t ms)
    {
        Util::Async::Thread::sleep(Util::Time::Timestamp::ofMicroseconds(static_cast<uint64_t>(ms) * 1000ULL));
    }

    // Physische Seiten reservieren und als MMIO/DMA-Bereich einblenden.
    void *alloc_dma_pages(size_t page_count, uint64_t *out_phys)
    {
        auto &mem = Service::getService<MemoryService>();

        void *phys = mem.allocatePhysicalMemory(static_cast<uint32_t>(page_count), nullptr);
        if (!phys)
        {
            return nullptr;
        }

        void *virt = mem.mapIO(phys, static_cast<uint32_t>(page_count), true);
        if (!virt)
        {
            mem.freePhysicalMemory(phys, static_cast<uint32_t>(page_count));
            return nullptr;
        }

        if (out_phys)
        {
            *out_phys = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(phys));
        }

        return virt;
    }

    // Einheitliches Logging für die Queue-Schicht.
    void virtio_log(const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        const auto msg = Util::String::format(fmt, args);
        va_end(args);
        LOG_INFO("[virtio-gpu] %s", static_cast<const char *>(msg));
    }
}

// Rundet Bytegrößen auf die benötigte Seitenanzahl auf.
size_t virtqueue_bytes_to_pages(size_t bytes)
{
    return (bytes + VIRTQUEUE_PAGE_SIZE - 1) / VIRTQUEUE_PAGE_SIZE;
}

// Signalisiert dem Gerät eine neue verfügbare Queue-Anfrage (Doorbell).
void virtqueue_notify(const VirtqueueContext &context, const Virtqueue &queue)
{
    if (!context.notify_base)
    {
        return;
    }

    const uint32_t stride = context.notify_multiplier ? context.notify_multiplier : sizeof(uint16_t);
    const size_t offset = static_cast<size_t>(queue.notify_off) * stride;

    volatile uint8_t *base = context.notify_base;
    volatile uint16_t *doorbell = reinterpret_cast<volatile uint16_t *>(base + offset);
    *doorbell = queue.index;
}

// Wartet auf Completion im Used-Ring bis Timeout.
bool virtqueue_wait_used(Virtqueue &queue, uint16_t expected, uint32_t timeout_ms)
{
    uint32_t waited = 0;
    while (queue.used->idx != expected)
    {
        memory_fence_acquire();
        if (waited >= timeout_ms)
        {
            return false;
        }

        sleep_ms(1);
        ++waited;
    }

    memory_fence_acquire();
    queue.used_idx = expected;
    return true;
}

// Initialisiert eine Virtqueue vollständig: Größe wählen, Speicher anlegen,
// Ringstrukturen verdrahten und Adressen in der Common Config hinterlegen.
bool virtqueue_setup(Virtqueue &queue,
                     const VirtqueueContext &context,
                     uint16_t queue_index,
                     uint16_t preferred_size)
{
    auto *common = context.common;
    if (!common)
    {
        return false;
    }

    common->queue_select = queue_index;
    uint16_t device_size = common->queue_size;
    if (device_size == 0)
    {
        virtio_log("queue %u unavailable", queue_index);
        return false;
    }

    uint16_t queue_size = preferred_size;
    if (queue_size == 0 || queue_size > device_size)
    {
        queue_size = device_size;
    }
    common->queue_size = queue_size;

    size_t desc_bytes = align_to(virtq_desc_size(queue_size), 16);
    size_t avail_offset = desc_bytes;
    size_t avail_bytes = align_to(virtq_avail_size(queue_size), 2);
    size_t used_offset = align_to(avail_offset + avail_bytes, 64);
    size_t used_bytes = align_to(virtq_used_size(queue_size), 64);
    size_t total_bytes = used_offset + used_bytes;
    size_t page_count = virtqueue_bytes_to_pages(total_bytes);

    uint64_t phys = 0;
    void *area = alloc_dma_pages(page_count, &phys);
    if (!area)
    {
        virtio_log("queue %u allocation failed", queue_index);
        return false;
    }

    std::memset(area, 0, page_count * VIRTQUEUE_PAGE_SIZE);

    uint8_t *base = static_cast<uint8_t *>(area);

    queue.index = queue_index;
    queue.size = queue_size;
    queue.desc = reinterpret_cast<VirtqDesc *>(base);
    queue.avail = reinterpret_cast<VirtqAvail *>(base + avail_offset);
    queue.used = reinterpret_cast<VirtqUsed *>(base + used_offset);
    queue.avail_idx = 0;
    queue.used_idx = 0;
    queue.desc_phys = phys;
    queue.avail_phys = phys + avail_offset;
    queue.used_phys = phys + used_offset;
    queue.dma_area = area;
    queue.dma_pages = page_count;
    queue.dma_bytes = page_count * VIRTQUEUE_PAGE_SIZE;

    const uint16_t notify_off = common->queue_notify_off;

    common->queue_desc_lo = static_cast<uint32_t>(queue.desc_phys & 0xffffffffu);
    common->queue_desc_hi = static_cast<uint32_t>(queue.desc_phys >> 32);
    common->queue_avail_lo = static_cast<uint32_t>(queue.avail_phys & 0xffffffffu);
    common->queue_avail_hi = static_cast<uint32_t>(queue.avail_phys >> 32);
    common->queue_used_lo = static_cast<uint32_t>(queue.used_phys & 0xffffffffu);
    common->queue_used_hi = static_cast<uint32_t>(queue.used_phys >> 32);
    common->queue_msix_vector = 0xffff;
    common->queue_enable = 1;

    queue.notify_off = notify_off;
    memory_fence_release();
    return true;
}
