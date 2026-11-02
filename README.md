# VirtIO-GPU Driver for hhuOS

**Bachelor's Thesis Project | Heinrich Heine University Düsseldorf | 2026**

**Language:** C++ | **Platform:** hhuOS / QEMU | **Focus:** Operating Systems, Device Drivers, Virtualization

## Project Overview

This project documents the design and implementation of a **paravirtualized VirtIO-GPU driver** for the experimental operating system **hhuOS**.

Developed in C++, the driver communicates with a virtual graphics device exposed by QEMU using the VirtIO protocol.

The implementation covers PCI device discovery, modern VirtIO initialization, DMA-backed memory management, split Virtqueue communication, 2D GPU resource management, framebuffer output, and an animated rendering demonstration.

**Scope:** This repository contains driver-related source files and technical documentation. The driver is **not a standalone executable**; building and running it requires integration with a compatible hhuOS source tree.

---

## Technologies

- **Programming Language:** C++
- **Operating System:** hhuOS
- **Virtualization and Testing:** QEMU / KVM
- **Standards and Interfaces:** VirtIO 1.2, PCI, MMIO
- **Memory Management:** DMA, Guest Physical Addresses, Framebuffer Memory
- **Communication:** Split Virtqueues, Descriptor Chains, Control Queue
- **Graphics:** 2D Rendering, Framebuffer, Partial Screen Updates
- **Concurrency:** Multithreading, Atomic Operations

---

## Key Features

### 1. PCI Device Discovery

- Detection of VirtIO-GPU devices using PCI identifiers.
- Identification and mapping of PCI capabilities.
- Configuration of memory-mapped I/O regions.
- Dynamic detection of device configuration registers.

The implementation identifies the virtual GPU using:

- **Vendor ID:** `0x1AF4`
- **Device ID:** `0x1050`

### 2. VirtIO Device Initialization

- Implementation of the VirtIO initialization state machine.
- Device feature negotiation.
- Configuration of device status registers.
- Validation of device initialization states.

The initialization follows the VirtIO handshake:

1. Device Reset
2. ACKNOWLEDGE
3. DRIVER
4. FEATURES_OK
5. Virtqueue Configuration
6. DRIVER_OK

### 3. Virtqueue Communication

The driver implements split Virtqueues to manage communication between the guest operating system and the virtual GPU.

Main functionalities include:

- Descriptor table management.
- Available Ring and Used Ring handling.
- Descriptor chaining for requests and responses.
- Control Queue communication.
- Synchronous command execution.
- Polling-based completion handling.
- Error detection and timeout management.

### 4. DMA and Memory Management

The driver uses Direct Memory Access (DMA) to enable efficient communication between hhuOS and the virtual GPU.

Implemented functionality includes:

- Allocation of DMA-compatible memory.
- Physical memory alignment.
- Management of guest physical addresses.
- Configuration of device-accessible memory regions.
- Allocation and management of framebuffer memory.

### 5. 2D Framebuffer Rendering

The driver implements a complete 2D graphics pipeline using the VirtIO-GPU protocol.

The following commands are supported:

| Command | Description |
|---|---|
| `RESOURCE_CREATE_2D` | Creates a 2D GPU resource |
| `RESOURCE_ATTACH_BACKING` | Associates guest memory with GPU resources |
| `SET_SCANOUT` | Connects the GPU resource to the virtual display |
| `TRANSFER_TO_HOST_2D` | Transfers framebuffer content to the host |
| `RESOURCE_FLUSH` | Updates the displayed image |

This command sequence allows hhuOS to generate and display graphical output inside QEMU.

### 6. Dynamic Rendering and Animation

The project extends basic framebuffer rendering with dynamic graphical output.

Implemented features include:

- Animated color gradients.
- Moving graphical elements.
- Frame-based rendering.
- Dedicated animation thread.
- Real-time framebuffer updates.
- On-screen frame counter (HUD).

The rendering loop operates in a dedicated thread to avoid blocking other operating-system tasks.

### 7. Dirty-Rectangle Optimization

To improve rendering efficiency, the implementation uses a dirty-rectangle approach.

Instead of transferring the entire framebuffer after each modification, the driver identifies changed screen regions and updates only the relevant areas.

This approach reduces unnecessary framebuffer transfers and improves rendering performance.

### 8. Interactive Mode Switching

The driver supports switching between two rendering modes:

**Static Mode**
- Displays a checkerboard pattern.
- Used to validate framebuffer initialization and memory consistency.

**Dynamic Mode**
- Displays animated graphical elements.
- Uses a dedicated rendering thread.

Pressing the **C key** switches between both modes without restarting hhuOS.

---

## Software Architecture

The driver follows a modular architecture that separates hardware communication, device initialization, memory management, and graphics rendering.

| Component | Responsibility |
|---|---|
| PCI Device Probe | Device discovery and capability mapping |
| Device Initialization | VirtIO handshake and feature negotiation |
| Virtqueue Transport | Descriptor management and communication rings |
| Control Queue | GPU command transmission and response handling |
| Resource Management | Framebuffer creation and GPU resource management |
| Rendering Engine | Static and dynamic graphics generation |
| Animation Thread | Continuous frame rendering |
| Hardware Abstraction Layer | Integration with hhuOS kernel services |

This separation improves maintainability and makes the implementation easier to understand and extend.

---

## Project Structure

The implementation is divided into several source files.

| Source File | Responsibility |
|---|---|
| `virtio_gpu_probe.cpp` | PCI device detection and capability mapping |
| `virtio_gpu_init.cpp` | VirtIO initialization and feature negotiation |
| `virtqueue.cpp` | Virtqueue configuration and communication |
| `virtio_gpu_control_queue.cpp` | GPU command transmission |
| `virtio_gpu_resource.cpp` | Framebuffer and GPU resource management |
| `virtio_gpu_autostart.cpp` | Initialization tasks and animation thread |
| `virtio_gpu.hpp` | Public driver interface |
| `virtio_gpu_internal.hpp` | Internal driver declarations |
| `os_hal_hhu.cpp` | Hardware abstraction for hhuOS |
| `docs/` | Technical documentation |
| `docs/img/` | Screenshots and architecture diagrams |

**Integration Note:** Some modifications to hhuOS, including keyboard event handling in `Keyboard.cpp`, are located outside the extracted driver directory and may require additional integration.

---

## Screenshots and Demonstrations

Screenshots of the running VirtIO-GPU driver, as well as architecture diagrams, are available in the **[docs/img/](docs/img/)** directory.

The images document:

- **Static Rendering:** Checkerboard pattern displayed through VirtIO-GPU.
- **Dynamic Rendering:** Animated graphics and moving elements.
- **Frame Counter (HUD):** Visualization of continuous rendering.
- **Interactive Mode Switching:** Switching between static and dynamic output.
- **Device Initialization:** Architecture diagram of PCI discovery and driver initialization.

These screenshots were captured during testing with QEMU and demonstrate the graphical capabilities of the implemented driver.

---

## Testing and Validation

The driver was evaluated using QEMU and several graphical test scenarios.

### Test 1: Static Rendering

A checkerboard pattern was rendered to verify:

- Framebuffer initialization.
- DMA memory allocation.
- GPU resource configuration.
- Correct graphics output.

### Test 2: Dynamic Rendering

An animated graphical scene was implemented to evaluate:

- Continuous framebuffer updates.
- Animation thread stability.
- Dirty-rectangle rendering.
- Frame-based graphical output.

### Test 3: Interactive Mode Switching

The driver was tested by switching between static and dynamic rendering using the C key.

The test verified:

- Correct keyboard event handling.
- Thread-safe mode switching.
- Successful rendering transitions.
- Operation without restarting the operating system.

---

## Performance Results

The bachelor's thesis reports benchmark measurements collected over **10,000 frames**.

| Metric | Legacy LFB | VirtIO-GPU |
|---|---:|---:|
| Average FPS | 76 | 564 |
| Frame Latency | 13.08 ms | 1.77 ms |
| CPU Jitter | ±4.2 ms | ±0.15 ms |

### Performance Improvements

The reported results indicate:

- Approximately **7.42× higher rendering throughput**.
- Approximately **86.4% lower frame latency**.
- Reduced CPU jitter during graphical updates.

The improvements are associated with Virtqueue-based communication, DMA memory management, and partial framebuffer updates.

**Note:** These measurements are reported in the bachelor's thesis and have not been independently reproduced for this standalone repository. Results depend on the test environment and workload.

---

## Technical Challenges

### PCI Configuration and Device Discovery

Implementing the device discovery mechanism required handling PCI capabilities and memory-mapped device configuration regions.

### Memory Management

The driver requires correctly aligned and device-accessible DMA buffers to ensure reliable communication with the virtual GPU.

### Synchronization

Virtqueue communication requires correct synchronization between the guest operating system and the virtual device.

The implementation handles descriptor publication, device notification, and completion processing.

### Rendering Efficiency

Updating an entire framebuffer can introduce unnecessary overhead.

The dirty-rectangle approach reduces data transfers by updating only changed screen regions where possible.

### Operating System Integration

The implementation required interaction with hhuOS-specific services, including memory management, PCI access, threading, and keyboard handling.

---

## Building and Running

The VirtIO-GPU driver was developed for integration into the hhuOS operating system.

It is not a standalone executable and depends on the corresponding hhuOS infrastructure.

To reproduce the implementation, the driver must be integrated into a compatible hhuOS source tree, including the required initialization hooks and hardware abstraction dependencies.

The exact build and QEMU launch commands have not yet been verified for this extracted repository and are therefore not included here.

---

## Limitations

The current implementation has several limitations:

- Focus on 2D rendering functionality.
- Synchronous Control Queue command handling.
- No VirGL-based 3D acceleration.
- Dependency on the hhuOS operating system.
- No verified standalone build process.

---

## Future Improvements

Potential extensions include:

- Asynchronous GPU command processing.
- Interrupt-driven completion handling.
- VirGL integration for hardware-accelerated 3D rendering.
- Further rendering optimizations.
- Improved integration and testing automation.
- Reproducible benchmarking and build instructions.

---

## Documentation

Additional documentation is available in the **[docs/](docs/)** directory.

The project is based on the bachelor's thesis:

**Entwicklung eines Virtio-Grafikkartentreibers für hhuOS**

Heinrich Heine University Düsseldorf, 2026.

The documentation covers:

- VirtIO architecture.
- PCI device discovery.
- Virtqueue communication.
- DMA memory management.
- GPU resource management.
- Rendering implementation.
- Testing and performance evaluation.

---

## Author

**Safi Al Saloum**

Bachelor of Science in Computer Science  
Heinrich Heine University Düsseldorf

**Bachelor's Thesis Project — 2026**
