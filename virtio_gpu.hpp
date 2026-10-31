/*
Diese Datei definiert Basis-Datentypen für PCI und GPU-Protokoll, die andere Module direkt nutzen dürfen.
((PciAddr)) und ((PciDeviceInfo)) beschreiben, 
wo das Gerät sitzt und welche BAR-Adressen es hat.

Danach kommen die Protokollkonstanten: GPU-Command-Codes und Response-Codes.

VirtioGpuRect, VirtioGpuCtrlHdr, VirtioGpuMemEntry, VirtioGpuDisplayOne und VirtioGpuRespDisplayInfo 
bilden die Datenstrukturen der VirtIO-GPU-Nachrichten.
Ganz unten ist die öffentliche API des Treibers:
 Initialisieren, Ready-Status abfragen, Display-Info holen.*/




#pragma once

#include <cstddef>
#include <cstdint>

#include "virtio_pci.hpp"
#include "virtio_ids.hpp"

// Kompakte PCI-Ortsinformation (Bus/Gerät/Funktion).
struct PciAddr
{
    uint8_t bus{0};
    uint8_t dev{0};
    uint8_t fn{0};
};

// Für die Initialisierung benötigte PCI-Metadaten.
struct PciDeviceInfo
{
    uint16_t vendor_id{0};
    uint16_t device_id{0};
    PciAddr loc{};
    uint64_t bar[6]{};
};

// VirtIO-GPU Kommandocodes (Control Queue).
constexpr uint32_t VIRTIO_GPU_CMD_GET_DISPLAY_INFO = 0x0100;
constexpr uint32_t VIRTIO_GPU_CMD_RESOURCE_CREATE_2D = 0x0101;
constexpr uint32_t VIRTIO_GPU_CMD_SET_SCANOUT = 0x0103;
constexpr uint32_t VIRTIO_GPU_CMD_RESOURCE_FLUSH = 0x0104;
constexpr uint32_t VIRTIO_GPU_CMD_RESOURCE_UNREF = 0x0105;
constexpr uint32_t VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING = 0x0106;
constexpr uint32_t VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING = 0x0109;
constexpr uint32_t VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D = 0x0105;

// Relevante Antwortcodes des Geräts.
constexpr uint32_t VIRTIO_GPU_RESP_OK_NODATA = 0x1100;
constexpr uint32_t VIRTIO_GPU_RESP_OK_DISPLAY_INFO = 0x1101;
constexpr uint32_t VIRTIO_GPU_RESP_ERR_UNSPEC = 0x1200;
constexpr uint32_t VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY = 0x1201;

constexpr size_t VIRTIO_GPU_MAX_SCANOUTS = 16;

// Pixelformate für 2D-Ressourcen.
enum class VirtioGpuFormat : uint32_t
{
    B8G8R8X8_UNORM = 1,
    B8G8R8A8_UNORM = 2,
};

// Generische Rechteckdarstellung der Spezifikation.
struct VirtioGpuRect
{
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

// Gemeinsamer Header für alle GPU-Kommandos/Antworten.
struct VirtioGpuCtrlHdr
{
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
};

// Eintrag für physisches Backing-Memory.
struct VirtioGpuMemEntry
{
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
};

// Beschreibung eines einzelnen Scanouts/Displays.
struct VirtioGpuDisplayOne
{
    VirtioGpuRect rect;
    VirtioGpuRect enabled_rect;
    uint32_t flags;
    uint32_t padding;
};

// Antwortstruktur für GET_DISPLAY_INFO.
struct VirtioGpuRespDisplayInfo
{
    VirtioGpuCtrlHdr hdr;
    VirtioGpuDisplayOne pmodes[VIRTIO_GPU_MAX_SCANOUTS];
};

// Öffentliche Treiber-API.
bool virtio_gpu_try_init(const PciDeviceInfo &device, const VirtioPciRegions &regions);
bool virtio_gpu_is_ready();
const VirtioGpuRespDisplayInfo *virtio_gpu_display_info();
void virtio_gpu_register_legacy_lfb(void *buffer, uint16_t width, uint16_t height, uint8_t color_depth, uint16_t pitch);
void virtio_gpu_run_benchmark();


//sorgt dafür , dass der Thread kontrolliert und nur einmal gestartet wird 
void virtio_gpu_start_demo_animation();
//schaltet den Modus um 
void virtio_gpu_toggle_checker_mode();
