/*
Virtqueue
Hält den kompletten Laufzeitzustand einer Queue (Ring-Pointer, Indizes, physische Adressen, DMA-Bereich).

VirtqueueContext
Enthält den Hardware-Zugriffskontext (common, notify_base, notify_multiplier).

VIRTQUEUE_PAGE_SIZE
Definiert die Seitengröße für DMA-Allokationen (4096).

Funktions-API
virtqueue_bytes_to_pages, virtqueue_notify, virtqueue_wait_used, virtqueue_setup sind die öffentliche Queue-Hilfsschnittstelle.


*/

#pragma once

#include "virtio_pci.hpp"

#include <cstddef>
#include <cstdint>

// Laufzeitrepräsentation einer initialisierten Virtqueue.
struct Virtqueue
{
    uint16_t index{0};
    uint16_t size{0};
    VirtqDesc *desc{nullptr};
    VirtqAvail *avail{nullptr};
    VirtqUsed *used{nullptr};
    uint16_t avail_idx{0};
    uint16_t used_idx{0};
    uint64_t desc_phys{0};
    uint64_t avail_phys{0};
    uint64_t used_phys{0};
    uint16_t notify_off{0};
    void *dma_area{nullptr};
    size_t dma_pages{0};
    size_t dma_bytes{0};
};

// Hardware-Kontext für Queue-Operationen.
struct VirtqueueContext
{
    volatile VirtioPciCommonCfg *common{nullptr};
    volatile uint8_t *notify_base{nullptr};
    uint32_t notify_multiplier{0};
};

// Seitengröße der DMA-Allokationen im Treiber.
constexpr uint32_t VIRTQUEUE_PAGE_SIZE = 4096;

// Öffentliche Queue-Hilfsfunktionen.
size_t virtqueue_bytes_to_pages(size_t bytes);
void virtqueue_notify(const VirtqueueContext &context, const Virtqueue &queue);
bool virtqueue_wait_used(Virtqueue &queue, uint16_t expected, uint32_t timeout_ms);
bool virtqueue_setup(Virtqueue &queue,
                     const VirtqueueContext &context,
                     uint16_t queue_index,
                     uint16_t preferred_size);
