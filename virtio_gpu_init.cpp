/*sie führt die komplette Geräte-Initialisierung aus.
egotiate_features() liest Device-Features, aktiviert nur sichere/kompatible Treiber-Features (hier vor allem VirtIO 1.0), 
setzt FEATURES_OK und prüft, ob das Gerät akzeptiert.
Danach folgt die Reihenfolge:
control_queue_init → cursor_queue_init (optional) → get_display_info → create_framebuffer_resource.
Bei Fehlern wird set_status_failed() gesetzt und sauber mit false abgebrochen.
Am Ende setzt der Code DRIVER_OK, markiert g_device.ready = true und loggt Auflösung + virgl-Status.*/


#include "virtio_gpu_internal.hpp"

#include <cstdint>

// Liest Gerätefeatures aus und aktiviert eine konservative, kompatible Teilmenge.
bool negotiate_features()
{
    uint64_t device_features = 0;
    for (uint32_t sel = 0; sel < 2; ++sel)
    {
        g_device.common->device_feature_select = sel;
        uint32_t value = g_device.common->device_feature;
        device_features |= static_cast<uint64_t>(value) << (sel * 32);
    }

    uint64_t driver_features = 0;
    if (device_features & (1ull << VIRTIO_F_VERSION_1_BIT))
    {
        driver_features |= (1ull << VIRTIO_F_VERSION_1_BIT);
    }

    g_device.virgl = (device_features & 0x1u) != 0;

    // Schreiben der akzeptierten Features (untere/obere 32 Bit).
    g_device.common->driver_feature_select = 0;
    g_device.common->driver_feature = static_cast<uint32_t>(driver_features & 0xffffffffu);
    g_device.common->driver_feature_select = 1;
    g_device.common->driver_feature = static_cast<uint32_t>((driver_features >> 32) & 0xffffffffu);

    g_device.common->device_status |= VIRTIO_CONFIG_S_FEATURES_OK;
    memory_fence_acquire();
    if ((g_device.common->device_status & VIRTIO_CONFIG_S_FEATURES_OK) == 0)
    {
        virtio_log("device rejected FEATURES_OK");
        return false;
    }

    return true;
}

// Hauptinitialisierung des VirtIO-GPU-Geräts.
bool virtio_gpu_try_init(const PciDeviceInfo &device, const VirtioPciRegions &regions)
{
    if (g_device.ready)
    {
        virtio_log("already initialized");
        return true;
    }

    g_device = VirtioGpuDevice{};
    g_device.pci = device;
    g_device.regs = regions;
    g_device.common = reinterpret_cast<volatile VirtioPciCommonCfg *>(regions.common_cfg.base);
    g_device.notify_base = reinterpret_cast<volatile uint8_t *>(regions.notify.base);
    g_device.notify_multiplier = regions.notify_off_multiplier;
    g_device.isr = reinterpret_cast<volatile uint8_t *>(regions.isr.base);

    // Ohne Common- und Notify-Bereich ist der Treiber nicht funktionsfähig.
    if (!g_device.common || !g_device.notify_base)
    {
        virtio_log("mandatory MMIO regions missing");
        set_status_failed();
        return false;
    }

    g_device.common->device_status = 0;
    memory_fence_release();

    g_device.common->device_status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    g_device.common->device_status |= VIRTIO_CONFIG_S_DRIVER;

    // 1) Features aushandeln, 2) Queues aufsetzen, 3) Display-Info holen,
    // 4) Framebuffer-Ressource aufbauen.
    if (!negotiate_features())
    {
        set_status_failed();
        return false;
    }

    if (!control_queue_init(g_device.control))
    {
        virtio_log("control queue init failed");
        set_status_failed();
        return false;
    }

    if (!cursor_queue_init(g_device.cursor))
    {
        virtio_log("cursor queue init failed (continuing without cursor)");
    }

    if (!get_display_info())
    {
        set_status_failed();
        return false;
    }

    if (!create_framebuffer_resource())
    {
        set_status_failed();
        return false;
    }

    g_device.common->device_status |= VIRTIO_CONFIG_S_DRIVER_OK;
    g_device.ready = true;

    virtio_log("initialized (%ux%u)%s", g_device.fb_width, g_device.fb_height, g_device.virgl ? " virgl" : "");
    return true;
}

// Gibt an, ob der Treiber vollständig initialisiert wurde.
bool virtio_gpu_is_ready()
{
    return g_device.ready;
}

// Liefert die zuletzt abgefragte Display-Konfiguration.
const VirtioGpuRespDisplayInfo *virtio_gpu_display_info()
{
    return g_device.ready ? &g_device.display_info : nullptr;
}
