/*

VirtioPciCap / VirtioPciNotifyCap
Beschreiben die VirtIO-PCI-Capabilities aus der PCI-Capability-List.

MappedRegion und VirtioPciRegions
Halten die gemappten Bereiche (common_cfg, notify, isr, optional device_cfg) plus notify_off_multiplier.

VirtioPciCommonCfg
Ist das zentrale Register-Layout für Feature-Negotiation, Queue-Auswahl, Queue-Adressen und Statusbits.

Status- und Descriptor-Flags (VIRTIO_CONFIG_S_*, VIRTQ_DESC_F_*)
Sind als Konstanten definiert und steuern Handshake/Descriptor-Verhalten.

VirtqDesc, VirtqAvail, VirtqUsedElem, VirtqUsed
Sind die Ringpuffer-Strukturen für den Datenaustausch Treiber ↔ Gerät.

virtq_desc_size, virtq_avail_size, virtq_used_size
Sind Compile-time-Helfer zur Berechnung der benötigten Speichergrößen pro Queue-Größe.

*/

#pragma once

#include <cstddef>
#include <cstdint>

// Typen der VirtIO-PCI-Capabilities gemäß Spezifikation.
constexpr uint8_t VIRTIO_PCI_CAP_COMMON_CFG = 1;
constexpr uint8_t VIRTIO_PCI_CAP_NOTIFY_CFG = 2;
constexpr uint8_t VIRTIO_PCI_CAP_ISR_CFG = 3;
constexpr uint8_t VIRTIO_PCI_CAP_DEVICE_CFG = 4;

// Gerätestatus-Bits (Handshake zwischen Treiber und Gerät).
constexpr uint8_t VIRTIO_CONFIG_S_ACKNOWLEDGE = 1u;
constexpr uint8_t VIRTIO_CONFIG_S_DRIVER = 2u;
constexpr uint8_t VIRTIO_CONFIG_S_DRIVER_OK = 4u;
constexpr uint8_t VIRTIO_CONFIG_S_FEATURES_OK = 8u;
constexpr uint8_t VIRTIO_CONFIG_S_FAILED = 0x80u;

// Descriptor-Flags in der Virtqueue.
constexpr uint16_t VIRTQ_DESC_F_NEXT = 1u;
constexpr uint16_t VIRTQ_DESC_F_WRITE = 2u;

// Generisches Capability-Layout für VirtIO über PCI.
struct VirtioPciCap
{
    uint8_t cap_vndr;
    uint8_t cap_next;
    uint8_t cap_len;
    uint8_t cfg_type;
    uint8_t bar;
    uint8_t padding[3];
    uint32_t offset;
    uint32_t length;
};

// Erweiterung der Notify-Capability um den Multiplikator für Doorbells.
struct VirtioPciNotifyCap
{
    VirtioPciCap cap;
    uint32_t notify_off_multiplier;
};

// Gemappter MMIO-Bereich (virtuelle Adresse + physische Basis + Länge).
struct MappedRegion
{
    void *base{nullptr};
    uint64_t phys{0};
    uint32_t length{0};
};

// Alle relevanten VirtIO-PCI-Regionen, die der Treiber benötigt.
struct VirtioPciRegions
{
    MappedRegion common_cfg;
    MappedRegion notify;
    MappedRegion isr;
    MappedRegion device_cfg;
    uint32_t notify_off_multiplier{0};
};

// Common Configuration Struktur aus der VirtIO-PCI-Spezifikation.
struct VirtioPciCommonCfg
{
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t device_status;
    uint8_t config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint32_t queue_desc_lo;
    uint32_t queue_desc_hi;
    uint32_t queue_avail_lo;
    uint32_t queue_avail_hi;
    uint32_t queue_used_lo;
    uint32_t queue_used_hi;
};

// Virtqueue-Deskriptor.
struct VirtqDesc
{
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

// Available-Ring (Treiber -> Gerät).
struct VirtqAvail
{
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
};

// Used-Element (Gerät -> Treiber).
struct VirtqUsedElem
{
    uint32_t id;
    uint32_t len;
};

// Used-Ring (Gerät -> Treiber).
struct VirtqUsed
{
    uint16_t flags;
    uint16_t idx;
    VirtqUsedElem ring[];
};

// Hilfsfunktionen zur Berechnung der Ringgrößen.
constexpr size_t virtq_desc_size(uint16_t qsize)
{
    return sizeof(VirtqDesc) * qsize;
}

constexpr size_t virtq_avail_size(uint16_t qsize)
{
    return sizeof(uint16_t) * 2 + sizeof(uint16_t) * qsize + sizeof(uint16_t);
}

constexpr size_t virtq_used_size(uint16_t qsize)
{
    return sizeof(uint16_t) * 2 + sizeof(VirtqUsedElem) * qsize + sizeof(uint16_t);
}
