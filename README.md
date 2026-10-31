# VirtIO-GPU Driver for hhuOS

**Bachelor's Thesis | Heinrich Heine University Düsseldorf | 2026**

## Project Overview

This project presents the design and implementation of a paravirtualized VirtIO-GPU driver in C++ for the experimental operating system hhuOS.

The driver enables 2D graphics output in a virtualized environment using QEMU and the VirtIO standard.

The implementation covers PCI device detection, VirtIO initialization, DMA memory management, Virtqueue communication, GPU resource management, framebuffer rendering, and dynamic animations.

## Technologies

- **Programming Language:** C++
- **Operating System:** hhuOS
- **Virtualization:** QEMU / KVM
- **Device Interface:** VirtIO 1.2, PCI
- **Memory Management:** DMA, MMIO
- **Graphics:** 2D Rendering, Framebuffer
- **Concurrency:** Multithreading, Atomic Operations

## Key Features

### PCI Device Detection

- Detection of VirtIO-GPU devices using PCI identifiers.
- Identification and mapping of PCI capabilities.
- Configuration of memory-mapped I/O regions.

### VirtIO Device Initialization

- Implementation of the VirtIO initialization state machine.
- Device feature negotiation.
- Configuration of device status registers.

### Virtqueue Communication

- Split Virtqueue implementation.
- Descriptor chaining for requests and responses.
- Control Queue communication.
- Synchronous command execution using polling and completion handling.

### DMA and Framebuffer Management

- Allocation of DMA-compatible memory.
- Creation of 2D GPU resources.
- Attachment of guest memory to GPU resources.
- Framebuffer initialization and display configuration.

### 2D Rendering

Implementation of the following VirtIO-GPU commands:

- `RESOURCE_CREATE_2D`
- `RESOURCE_ATTACH_BACKING`
- `SET_SCANOUT`
- `TRANSFER_TO_HOST_2D`
- `RESOURCE_FLUSH`

### Dynamic Rendering

- Animated color gradients and moving graphical objects.
- Dedicated animation thread.
- Frame-based rendering.
- Dirty-rectangle updates to reduce unnecessary framebuffer transfers.
- Interactive switching between static and dynamic rendering modes using the C key.

## Software Architecture

The driver consists of several components:

| Component | Responsibility |
|---|---|
| Device Probe | PCI detection and capability mapping |
| Initialization | VirtIO handshake and feature negotiation |
| Virtqueue | Communication between guest and host |
| Control Queue | GPU command transmission |
| Resource Management | Framebuffer and GPU resource management |
| Animation | Dynamic rendering and animation control |
| Hardware Abstraction | Integration with hhuOS kernel services |

## Testing and Validation

The implementation was evaluated using QEMU.

Three main test scenarios were documented:

**1. Static Rendering**

A checkerboard pattern was displayed to verify framebuffer operation and DMA memory consistency.

**2. Dynamic Rendering**

Animated gradients and moving graphical objects were used to validate continuous rendering and partial framebuffer updates.

**3. Interactive Mode Switching**

The C key allows switching between static and dynamic rendering without restarting the operating system.

## Performance Results

The bachelor's thesis reports the following benchmark results:

| Metric | Legacy LFB | VirtIO-GPU |
|---|---:|---:|
| Average FPS | 76 | 564 |
| Frame Latency | 13.08 ms | 1.77 ms |
| CPU Jitter | ±4.2 ms | ±0.15 ms |

The reported results indicate approximately 7.42 times higher rendering throughput in the evaluated test setup.

These measurements are taken from the bachelor's thesis and have not been independently reproduced for this repository.

## Limitations

- Currently focused on 2D graphics.
- Synchronous Control Queue completion handling.
- No VirGL-based 3D acceleration.
- Requires integration with hhuOS.

## Future Improvements

- Asynchronous command processing.
- Interrupt-driven completion handling.
- VirGL integration for 3D graphics.
- Further rendering performance optimization.

## Documentation

Technical documentation is available in the `docs/` directory.

## Author

**Safi Al Saloum**

Bachelor of Science in Computer Science  
Heinrich Heine University Düsseldorf

Bachelor's Thesis, 2026
