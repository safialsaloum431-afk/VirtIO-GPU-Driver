/*
(((Hauptablauf in virtio_gpu_probe_all_devices(): )))

1-GPU finden
2-PciDeviceInfo + BARs aufbauen
3-VirtIO-Regionen aus Caps mappen
4-dann virtio_gpu_try_init(...) aufrufen
5-Erfolg/Fehler loggen (init OK / init failed)
*/



#include "device/graphic/virtio/virtio_gpu.hpp"
#include "device/graphic/virtio/virtio_pci.hpp"
#include "device/graphic/virtio/virtio_ids.hpp"
#include "device/bus/pci/PciDevice.h"
#include "kernel/service/Service.h"
#include "kernel/service/MemoryService.h"
#include "kernel/log/Log.h"
#include "lib/util/base/Constants.h"
#include "lib/util/base/String.h"

#include <cstdarg>
#include <cstdint>

using Kernel::MemoryService;
using Kernel::Service;

namespace
{
    // PCI-Konfigurationsregister.
    constexpr uint16_t PCI_CONFIG_BAR0 = 0x10;
    constexpr uint16_t PCI_STATUS_REG = 0x06;
    constexpr uint16_t PCI_CAP_PTR_REG = 0x34;
    constexpr uint16_t PCI_INVALID_VENDOR = 0xFFFF;
    constexpr uint8_t PCI_MAX_DEVICES_PER_BUS = 32;
    constexpr uint8_t PCI_MAX_FUNCTIONS_PER_DEVICE = 8;

    // Umrechnung Bytegröße -> Seitenanzahl.
    inline uint32_t pages_for(size_t bytes)
    {
        return static_cast<uint32_t>((bytes + Util::PAGESIZE - 1) / Util::PAGESIZE);
    }

    // Lokaler Logger für den Probe-Pfad.
    void log_vgpu(const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        const auto msg = Util::String::format(fmt, args);
        va_end(args);
        LOG_INFO("[virtio-gpu] %s", static_cast<const char *>(msg));
    }

    // Liest BAR-Information und bestimmt, ob es sich um Memory-BAR handelt.
    bool get_bar(const Device::PciDevice &dev, uint8_t index, uint64_t &base_out, bool &is_mem)
    {
        if (index >= 6)
        {
            return false;
        }

        const uint8_t offset = static_cast<uint8_t>(PCI_CONFIG_BAR0 + index * 4);
        uint32_t low = dev.readDoubleWord(offset);
        if ((low & 0x1u) != 0)
        {
            is_mem = false;
            base_out = static_cast<uint64_t>(low & ~0x3u);
            return true;
        }

        is_mem = true;
        const uint32_t type = (low >> 1) & 0x3u;
        bool is_64 = (type == 0x2u);
        uint64_t base = static_cast<uint64_t>(low & ~0xFu);
        if (is_64)
        {
            uint32_t high = dev.readDoubleWord(static_cast<uint8_t>(offset + 4));
            base |= static_cast<uint64_t>(high) << 32;
        }

        base_out = base;
        return true;
    }

    // Sucht das erste passende VirtIO-GPU-Gerät auf dem gesamten PCI-Bus.
    bool find_first_virtio_gpu(PciAddr &out)
    {
        for (uint16_t bus = 0; bus < 256; ++bus)
        {
            for (uint8_t dev_index = 0; dev_index < PCI_MAX_DEVICES_PER_BUS; ++dev_index)
            {
                for (uint8_t fn = 0; fn < PCI_MAX_FUNCTIONS_PER_DEVICE; ++fn)
                {
                    Device::PciDevice dev(static_cast<uint8_t>(bus), dev_index, fn);
                    const uint16_t vendor = dev.getVendorId();
                    if (vendor == PCI_INVALID_VENDOR)
                    {
                        if (fn == 0)
                        {
                            break;
                        }
                        continue;
                    }

                    if (vendor == VIRTIO_VENDOR_ID && dev.getDeviceId() == VIRTIO_PCI_DEVICE_ID_GPU)
                    {
                        out.bus = static_cast<uint8_t>(bus);
                        out.dev = dev_index;
                        out.fn = fn;
                        return true;
                    }
                }
            }
        }

        return false;
    }
}

// Mappt eine einzelne VirtIO-Capability in den Kernel-Adressraum.
static bool map_cap_region(const PciAddr &loc, const VirtioPciCap &cap, MappedRegion &out)
{
    Device::PciDevice dev(loc.bus, loc.dev, loc.fn);
    uint64_t bar_base = 0;
    bool is_mem = false;
    if (!get_bar(dev, cap.bar, bar_base, is_mem))
    {
        return false;
    }
    if (!is_mem)
    {
        return false;
    }
    uint64_t phys = bar_base + cap.offset;
    auto &mem = Service::getService<MemoryService>();
    void *base = mem.mapIO(reinterpret_cast<void *>(phys), pages_for(cap.length), true);

    if (!base)
    {
        return false;
    }
    out.base = base;
    out.length = cap.length;
    out.phys = phys;

    return true;
}

// Läuft die PCI-Capability-Liste ab und baut daraus die benötigten Regionen auf.
static bool build_regions_from_caps(const PciAddr &loc, VirtioPciRegions &regs)
{
    Device::PciDevice dev(loc.bus, loc.dev, loc.fn);
    uint16_t status = dev.readWord(PCI_STATUS_REG);
    if ((status & (1u << 4)) == 0)
    {
        log_vgpu("no capability list");
        return false;
    }

    uint8_t ptr = dev.readByte(PCI_CAP_PTR_REG);
    int guard = 64;
    while (ptr && guard--)
    {
        uint8_t cap_id = dev.readByte(static_cast<uint8_t>(ptr + 0));
        uint8_t next = dev.readByte(static_cast<uint8_t>(ptr + 1));
        if (cap_id == 0x09)
        {
            VirtioPciCap c{};

            c.cap_vndr = cap_id;
            c.cap_next = next;
            c.cap_len = dev.readByte(static_cast<uint8_t>(ptr + 2));
            c.cfg_type = dev.readByte(static_cast<uint8_t>(ptr + 3));
            c.bar = dev.readByte(static_cast<uint8_t>(ptr + 4));
            c.offset = dev.readDoubleWord(static_cast<uint8_t>(ptr + 8));
            c.length = dev.readDoubleWord(static_cast<uint8_t>(ptr + 12));

            switch (c.cfg_type)
            {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                if (!map_cap_region(loc, c, regs.common_cfg))
                {
                    return false;
                }
                break;
            case VIRTIO_PCI_CAP_ISR_CFG:
                if (!map_cap_region(loc, c, regs.isr))
                {
                    return false;
                }
                break;
            case VIRTIO_PCI_CAP_DEVICE_CFG:
                if (!map_cap_region(loc, c, regs.device_cfg))
                {
                    return false;
                }
                break;
            case VIRTIO_PCI_CAP_NOTIFY_CFG:
            {
                VirtioPciNotifyCap nc{};
                nc.cap = c;
                nc.notify_off_multiplier = dev.readDoubleWord(static_cast<uint8_t>(ptr + 16));

                if (!map_cap_region(loc, nc.cap, regs.notify))
                {
                    return false;
                }
                regs.notify_off_multiplier = nc.notify_off_multiplier;
                break;
            }
            default:
                break;
            }
        }
        ptr = next;
    }

    if (!regs.common_cfg.base || !regs.notify.base || !regs.isr.base)
    {
        log_vgpu("missing mandatory virtio caps");
        return false;
    }
    return true;
}

// Öffentlicher Einstieg: Gerät suchen, Regionen mappen und Initialisierung starten.
void virtio_gpu_probe_all_devices()
{
    PciAddr loc{};
    if (!find_first_virtio_gpu(loc))
    {
        log_vgpu("not found");
        return;
    }

    PciDeviceInfo dev{};
    dev.vendor_id = VIRTIO_VENDOR_ID;
    dev.device_id = VIRTIO_PCI_DEVICE_ID_GPU;
    dev.loc.bus = loc.bus;
    dev.loc.dev = loc.dev;
    dev.loc.fn = loc.fn;

    Device::PciDevice pci(loc.bus, loc.dev, loc.fn);
    for (int i = 0; i < 6; ++i)
    {
        uint64_t base = 0;
        bool m = false;
        if (get_bar(pci, static_cast<uint8_t>(i), base, m) && m)
        {
            dev.bar[i] = base;
        }
    }

    VirtioPciRegions regs{};
    if (!build_regions_from_caps(loc, regs))
    {
        log_vgpu("caps failed");
        return;
    }

    if (virtio_gpu_try_init(dev, regs))
    {
        log_vgpu("init OK");
    }
    else
    {
        log_vgpu("init failed");
    }
}
