/*
 Die Datei sorgt dafür,
 dass die VirtIO-GPU-Initialisierung asynchron 
 und leicht verzögert nach dem Boot passiert.

 Startet nur, wenn ProcessService bereits registriert ist.
 */

#include "kernel/service/Service.h"
#include "kernel/service/ProcessService.h"
#include "kernel/process/Thread.h"
#include "lib/util/async/BasicRunnable.h"
#include "lib/util/async/Thread.h"
#include "lib/util/time/Timestamp.h"
#include "device/graphic/virtio/virtio_gpu.hpp"

// Probe-Funktion aus dem PCI/Initialisierungspfad.
extern void virtio_gpu_probe_all_devices();
// Render-Funktion für die Demo-Animation, die von der Animations-Task aufgerufen wird.
// Diese Funktion ist die Grundlage für die gesamte Demo, da sie die Ressource anlegt, das Backing bindet,
extern bool virtio_gpu_demo_render_frame(uint32_t frame_index);


static bool g_animation_started = false;

// Animations-Task, die kontinuierlich Frames rendert und präsentiert, bis der GPU-Treiber nicht mehr bereit ist oder zu viele aufeinanderfolgende Fehler auftreten.


static void virtio_gpu_animation_task()
{
    constexpr uint32_t FRAME_INTERVAL_US = 33'000;
    constexpr uint32_t RECOVERY_SLEEP_US = 100'000;
    constexpr uint32_t MAX_CONSECUTIVE_FAILS = 30;

    uint32_t frame = 1;
    uint32_t consecutive_failures = 0;
    while (virtio_gpu_is_ready())
    {
        if (!virtio_gpu_demo_render_frame(frame))
        {
            ++consecutive_failures;

            if (consecutive_failures >= MAX_CONSECUTIVE_FAILS)
            {
                break;
            }

            Util::Async::Thread::sleep(Util::Time::Timestamp::ofMicroseconds(RECOVERY_SLEEP_US));
            continue;
        }

        consecutive_failures = 0;
        ++frame;
        Util::Async::Thread::sleep(Util::Time::Timestamp::ofMicroseconds(FRAME_INTERVAL_US));
    }
}
// Struktur für den globalen Demo-Zustand, der die Position, Geschwindigkeit und Größe des animierten Objekts sowie die HUD-Position und -Größe enthält.
// Diese Struktur ermöglicht es, die animierte Szene über mehrere Frames hinweg konsistent zu halten und die Dirty-Rect-Updates korrekt zu berechnen.
// Alle Werte sind in Pixeln oder Pixeln pro Frame, da die Demo direkt im Framebuffer rendert.
void virtio_gpu_start_demo_animation()
{
    if (!virtio_gpu_is_ready())
    {
        return;
    }

    if (g_animation_started)
    {
        return;
    }
    g_animation_started = true;

    if (!Kernel::Service::isServiceRegistered(Kernel::ProcessService::SERVICE_ID))
    {
        g_animation_started = false;
        return;
    }

    auto &processService = Kernel::Service::getService<Kernel::ProcessService>();

    auto *runnable = new Util::Async::BasicRunnable(&virtio_gpu_animation_task);
    auto &thread = Kernel::Thread::createKernelThread(
        Util::String("virtio-gpu-demo"),
        processService.getKernelProcess(),
        runnable);
    processService.getScheduler().ready(thread);
}

// Startaufgabe: kurze Verzögerung, damit Kerneldienste bereits initialisiert sind.
static void virtio_gpu_init_task()
{
    Util::Async::Thread::sleep(Util::Time::Timestamp::ofMicroseconds(100'000));
    virtio_gpu_probe_all_devices();
    virtio_gpu_run_benchmark();
    virtio_gpu_start_demo_animation();
}

// Legt einen dedizierten Kernel-Thread für die GPU-Initialisierung an.
static void spawn_vgpu_init_thread()
{
    if (!Kernel::Service::isServiceRegistered(Kernel::ProcessService::SERVICE_ID))
    {
        return;
    }

    auto &processService = Kernel::Service::getService<Kernel::ProcessService>();

    auto *runnable = new Util::Async::BasicRunnable(&virtio_gpu_init_task);
    auto &thread = Kernel::Thread::createKernelThread(
        Util::String("virtio-gpu-init"),
        processService.getKernelProcess(),
        runnable);
    processService.getScheduler().ready(thread);
}

// Öffentlicher Hook, der beim Systemstart eingebunden werden kann.
void virtio_gpu_autostart_hook()
{
    spawn_vgpu_init_thread();
}
