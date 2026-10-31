#pragma once

#include <cstdint>

// PCI Vendor-ID für VirtIO-Geräte (QEMU/virtio-Spezifikation).
constexpr uint16_t VIRTIO_VENDOR_ID = 0x1AF4;

// PCI Device-ID für den VirtIO-GPU-Adapter im modern-PCI-Layout.
constexpr uint16_t VIRTIO_PCI_DEVICE_ID_GPU = 0x1050;
