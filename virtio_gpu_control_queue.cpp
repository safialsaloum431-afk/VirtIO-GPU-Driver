//Das Modul kapselt den kompletten Request/Response-Transport zur GPU über Virtqueue, 
//inklusive Speicherverwaltung, Synchronisation und Timeout-Handling.

#include "virtio_gpu_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

// Hilfsstruktur für Kommandos ohne Nutzdaten.
struct VirtioGpuCmdNoData
{
    VirtioGpuCtrlHdr hdr{};
};

// Initialisiert die Control Queue und reserviert einen gemeinsamen
// Anfrage-/Antwort-Puffer (jeweils eine Seite).
bool control_queue_init(ControlQueue &ctrl)
{
    if (!virtqueue_setup(ctrl.vq, queue_context(), CONTROL_QUEUE_INDEX, CONTROL_QUEUE_TARGET_SIZE))
    {
        return false;
    }

    const size_t page_count = 2;
    uint64_t phys = 0;
    void *area = alloc_dma_pages(page_count, &phys);
    if (!area)
    {
        virtio_log("control queue message buffer allocation failed");
        return false;
    }
    std::memset(area, 0, page_count * VIRTQUEUE_PAGE_SIZE);

    ctrl.request = area;
    ctrl.request_phys = phys;
    ctrl.request_capacity = VIRTQUEUE_PAGE_SIZE;
    ctrl.response = static_cast<uint8_t *>(area) + VIRTQUEUE_PAGE_SIZE;
    ctrl.response_phys = phys + VIRTQUEUE_PAGE_SIZE;
    ctrl.response_capacity = VIRTQUEUE_PAGE_SIZE;
    ctrl.dma_pages = page_count;
    return true;
}

// Initialisiert die Cursor Queue; ein Ausfall ist nicht fatal.
bool cursor_queue_init(Virtqueue &cursor)
{
    return virtqueue_setup(cursor, queue_context(), CURSOR_QUEUE_INDEX, CURSOR_QUEUE_TARGET_SIZE);
}

// Überträgt synchron ein Control-Queue-Kommando und wartet auf Completion.
bool control_queue_send(const void *req, size_t req_len, void *resp, size_t resp_len)
{
    auto &ctrl = g_device.control;
    auto &vq = ctrl.vq;

    if (req_len > ctrl.request_capacity || resp_len > ctrl.response_capacity)
    {
        virtio_log("control queue payload too large");
        return false;
    }

    std::memcpy(ctrl.request, req, req_len);
    std::memset(static_cast<uint8_t *>(ctrl.request) + req_len, 0, ctrl.request_capacity - req_len);
    std::memset(ctrl.response, 0, ctrl.response_capacity);

    vq.desc[0].addr = ctrl.request_phys;
    vq.desc[0].len = static_cast<uint32_t>(req_len);
    vq.desc[0].flags = VIRTQ_DESC_F_NEXT;
    vq.desc[0].next = 1;

    vq.desc[1].addr = ctrl.response_phys;
    vq.desc[1].len = static_cast<uint32_t>(resp_len);
    vq.desc[1].flags = VIRTQ_DESC_F_WRITE;
    vq.desc[1].next = 0;

    const uint16_t slot = vq.avail_idx % vq.size;
    vq.avail->ring[slot] = 0;
    memory_fence_release();

    ++vq.avail_idx;
    vq.avail->idx = vq.avail_idx;
    memory_fence_release();

    virtqueue_notify(queue_context(), vq);

    if (!virtqueue_wait_used(vq, static_cast<uint16_t>(vq.used_idx + 1), CTRL_TIMEOUT_MS))
    {
        virtio_log("control queue completion timeout");
        return false;
    }

    const uint16_t used_slot = static_cast<uint16_t>((vq.used_idx - 1) % vq.size);
    const VirtqUsedElem &used_elem = vq.used->ring[used_slot];
    if (used_elem.id != 0)
    {
        virtio_log("control queue unexpected used id %u", used_elem.id);
    }

    std::memcpy(resp, ctrl.response, resp_len);
    return true;
}

// Vereinfacht das Senden von Kommandos mit nur Header.
static bool send_simple_command(uint32_t type, void *response, size_t response_size)
{
    VirtioGpuCmdNoData cmd{};
    cmd.hdr.type = type;
    return control_queue_send(&cmd, sizeof(cmd), response, response_size);
}

// Fragt die Display-Topologie vom Gerät ab.
bool get_display_info()
{
    VirtioGpuRespDisplayInfo resp{};
    if (!send_simple_command(VIRTIO_GPU_CMD_GET_DISPLAY_INFO, &resp, sizeof(resp)))
    {
        virtio_log("GET_DISPLAY_INFO failed");
        return false;
    }
    if (resp.hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO)
    {
        virtio_log("GET_DISPLAY_INFO unexpected response 0x%x", resp.hdr.type);
        return false;
    }
    g_device.display_info = resp;
    return true;
}
