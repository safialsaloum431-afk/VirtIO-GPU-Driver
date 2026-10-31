/*
Diese Datei enthält Infrastruktur, auf der die anderen VirtIO-GPU-Dateien aufbauen 
(globaler Zustand, DMA, Synchronisation, Logging, Fehlerstatus).
*/


#include "virtio_gpu_internal.hpp"

#include "kernel/service/Service.h"
#include "kernel/service/MemoryService.h"
#include "lib/util/base/String.h"
#include "kernel/log/Log.h"

#include <atomic>
#include <cstdarg>

using Kernel::MemoryService;
using Kernel::Service;

// Zentraler Treiberzustand (ein Gerät, ein globaler Kontext).
VirtioGpuDevice g_device{};
LegacyFramebufferInfo g_legacy_lfb{};

// Erzeugt den minimalen Queue-Kontext für Unterfunktionen.
VirtqueueContext queue_context()
{
    return VirtqueueContext{
        g_device.common,
        g_device.notify_base,
        g_device.notify_multiplier};
}

// Speicherbarriere für Schreibreihenfolge (Treiber -> Gerät).
void memory_fence_release()
{
    std::atomic_thread_fence(std::memory_order_release);
}

// Speicherbarriere für Lesekonsistenz (Gerät -> Treiber).
void memory_fence_acquire()
{
    std::atomic_thread_fence(std::memory_order_acquire);
}

// Einheitliche Log-Ausgabe mit virtio-gpu Präfix.
void virtio_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const auto msg = Util::String::format(fmt, args);
    va_end(args);
    LOG_INFO("[virtio-gpu] %s", static_cast<const char *>(msg));
}

void virtio_gpu_register_legacy_lfb(void *buffer, uint16_t width, uint16_t height, uint8_t color_depth, uint16_t pitch)
{
    g_legacy_lfb.buffer = buffer;
    g_legacy_lfb.width = width;
    g_legacy_lfb.height = height;
    g_legacy_lfb.color_depth = color_depth;
    g_legacy_lfb.pitch = pitch;

    virtio_log("registered legacy lfb (%ux%u@%u, pitch=%u)", width, height, color_depth, pitch);
}

// Allokiert physische Seiten und mappt diese in den Kernel-Adressraum,
// sodass sie direkt als DMA-Buffer genutzt werden können.
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

// Markiert den Gerätezustand als fehlgeschlagen.
void set_status_failed()
{
    if (g_device.common)
    {
        g_device.common->device_status |= VIRTIO_CONFIG_S_FAILED;
    }
    g_device.ready = false;
}
