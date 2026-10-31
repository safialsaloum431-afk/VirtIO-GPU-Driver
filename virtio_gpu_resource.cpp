/*
Diese Datei ist der „Bildaufbau“-Teil des Treibers —
sie erstellt den Framebuffer,
 verbindet ihn mit der virtuellen GPU
 und stößt das erste sichtbare Bild an

Die Kernfunktion ist virtio_gpu_demo_render_frame(), die für jeden Frame aufgerufen wird.
Sie ist die Grundlage für die gesamte Demo, da sie die Ressource anlegt, das Backing bindet,
den Hintergrund zeichnet,
das animierte Objekt bewegt und schließlich die aktualisierten Bereiche präsentiert.

*/
#include "virtio_gpu_internal.hpp"

#include "lib/util/time/Timestamp.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Kommando: 2D-Ressource anlegen.
struct VirtioGpuResourceCreate2dCmd
{
    VirtioGpuCtrlHdr hdr{};
    uint32_t resource_id{0};
    uint32_t format{0};
    uint32_t width{0};
    uint32_t height{0};
};

// Kommando: Physisches Backing-Memory an Ressource binden.
struct VirtioGpuResourceAttachBackingCmd
{
    VirtioGpuCtrlHdr hdr{};
    uint32_t resource_id{0};
    uint32_t nr_entries{0};
    VirtioGpuMemEntry entries[1]{};
};

// Kommando: Ressource einem Scanout zuordnen.
struct VirtioGpuSetScanoutCmd
{
    VirtioGpuCtrlHdr hdr{};
    VirtioGpuRect rect{};
    uint32_t scanout_id{0};
    uint32_t resource_id{0};
};

// Kommando: Pixelinhalt in die Host-Darstellung übertragen.
struct VirtioGpuTransferToHost2dCmd
{
    VirtioGpuCtrlHdr hdr{};
    VirtioGpuRect rect{};
    uint64_t offset{0};
    uint32_t resource_id{0};
    uint32_t padding{0};
};

// Kommando: Aktualisierte Region sichtbar machen.
struct VirtioGpuResourceFlushCmd
{
    VirtioGpuCtrlHdr hdr{};
    VirtioGpuRect rect{};
    uint32_t resource_id{0};
    uint32_t padding{0};
};

// Einführung eines echten Animationszustands mit Position, Geschwindigkeit und HUD-Layout,
// um die Fähigkeiten von Dirty-Rect-Updates zu demonstrieren.

// HUD : Ein halbtransparenter Bereich in der oberen linken Ecke, der die aktuelle Frame-Nummer anzeigt.
// Dirty-Rect-Updates : Nur die Bereiche des Bildes werden aktualisiert, die sich geändert haben (Objekt und HUD),
// anstatt das gesamte Bild neu zu zeichnen, um die Effizienz zu demonstrieren.
struct DemoState
{
    bool initialized{false};
    int32_t box_x{20};
    int32_t box_y{20};
    int32_t velocity_x{5};
    int32_t velocity_y{4};
    uint32_t box_size{88};
    uint32_t prev_box_x{0};
    uint32_t prev_box_y{0};
    uint32_t hud_x{8};
    uint32_t hud_y{8};
    uint32_t hud_w{220};
    uint32_t hud_h{38};
};

DemoState g_demo{};                      // Globaler Demo-Zustand für die animierte Szene.
std::atomic<bool> g_checker_mode{false}; // Globaler Flag für den alternativen Checkerboard-Demo-Modus.

struct BenchmarkSurface
{
    uint32_t *pixels{nullptr};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t pitch_pixels{0};
};

struct BenchmarkScene
{
    int32_t box_x{20};
    int32_t box_y{20};
    int32_t velocity_x{5};
    int32_t velocity_y{4};
    uint32_t box_size{88};
    uint32_t prev_box_x{20};
    uint32_t prev_box_y{20};
};

struct BenchmarkStats
{
    bool ok{false};
    uint32_t frames{0};
    uint64_t elapsed_us{0};
    uint32_t min_frame_us{UINT32_MAX};
    uint32_t max_frame_us{0};
    bool uses_virtqueue{false};
};

// Überträgt die gesamte Frame-Region zum Host und macht sie sichtbar.
static bool present_full_frame();

// Überträgt genau ein Rechteck zum Host und macht es sichtbar.
static bool present_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

// Deterministische, kachelbare Hintergrundfarbe für ein stabiles Redraw
// einzelner Teilbereiche (wichtig für Dirty-Rect-Updates).
static uint32_t background_color(uint32_t x, uint32_t y)
{
    const uint32_t tile = 192;
    const uint32_t local_x = x % tile;
    const uint32_t local_y = y % tile;
    const bool diagonal = local_x > local_y;

    const uint8_t r = static_cast<uint8_t>((local_x * 255u) / (tile - 1));
    const uint8_t g = static_cast<uint8_t>((local_y * 255u) / (tile - 1));
    const uint8_t b = diagonal ? static_cast<uint8_t>(255u - r / 2u) : static_cast<uint8_t>(255u - g / 2u);

    return 0xFF000000u |
           (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(b);
}

static bool benchmark_surface_from_virtio(BenchmarkSurface &surface)
{
    if (!g_device.framebuffer || g_device.fb_width == 0 || g_device.fb_height == 0)
    {
        return false;
    }

    surface.pixels = static_cast<uint32_t *>(g_device.framebuffer);
    surface.width = g_device.fb_width;
    surface.height = g_device.fb_height;
    surface.pitch_pixels = g_device.fb_width;
    return true;
}

static bool benchmark_surface_from_legacy(BenchmarkSurface &surface)
{
    if (!g_legacy_lfb.buffer || g_legacy_lfb.width == 0 || g_legacy_lfb.height == 0)
    {
        return false;
    }

    if (g_legacy_lfb.color_depth != 32 || g_legacy_lfb.pitch < g_legacy_lfb.width * BYTES_PER_PIXEL)
    {
        return false;
    }

    surface.pixels = static_cast<uint32_t *>(g_legacy_lfb.buffer);
    surface.width = g_legacy_lfb.width;
    surface.height = g_legacy_lfb.height;
    surface.pitch_pixels = g_legacy_lfb.pitch / BYTES_PER_PIXEL;
    return true;
}

static void benchmark_draw_background_region(BenchmarkSurface &surface, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (!surface.pixels)
    {
        return;
    }

    const uint32_t x_end = (x + width > surface.width) ? surface.width : (x + width);
    const uint32_t y_end = (y + height > surface.height) ? surface.height : (y + height);

    for (uint32_t py = y; py < y_end; ++py)
    {
        uint32_t *row = surface.pixels + py * surface.pitch_pixels;
        for (uint32_t px = x; px < x_end; ++px)
        {
            row[px] = background_color(px, py);
        }
    }
}

static void benchmark_draw_filled_rect(BenchmarkSurface &surface, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    if (!surface.pixels)
    {
        return;
    }

    const uint32_t x_end = (x + width > surface.width) ? surface.width : (x + width);
    const uint32_t y_end = (y + height > surface.height) ? surface.height : (y + height);

    for (uint32_t py = y; py < y_end; ++py)
    {
        uint32_t *row = surface.pixels + py * surface.pitch_pixels;
        for (uint32_t px = x; px < x_end; ++px)
        {
            row[px] = color;
        }
    }
}

static void benchmark_draw_box(BenchmarkSurface &surface, uint32_t x, uint32_t y, uint32_t size)
{
    benchmark_draw_filled_rect(surface, x, y, size, size, 0xFF111111u);
    benchmark_draw_filled_rect(surface, x, y, size, 3, 0xFFFFFFFFu);
    benchmark_draw_filled_rect(surface, x, y + size - 3, size, 3, 0xFFFFFFFFu);
    benchmark_draw_filled_rect(surface, x, y, 3, size, 0xFFFFFFFFu);
    benchmark_draw_filled_rect(surface, x + size - 3, y, 3, size, 0xFFFFFFFFu);
    benchmark_draw_filled_rect(surface, x + 10, y + size / 2 - 2, size - 20, 4, 0xFF00C8FFu);
    benchmark_draw_filled_rect(surface, x + size / 2 - 2, y + 10, 4, size - 20, 0xFF00C8FFu);
}

static void benchmark_reset_scene(BenchmarkScene &scene)
{
    scene = BenchmarkScene{};
}

static void benchmark_advance_scene(BenchmarkScene &scene, const BenchmarkSurface &surface)
{
    const int32_t max_x = static_cast<int32_t>((surface.width > scene.box_size) ? (surface.width - scene.box_size) : 0);
    const int32_t max_y = static_cast<int32_t>((surface.height > scene.box_size) ? (surface.height - scene.box_size) : 0);

    scene.box_x += scene.velocity_x;
    scene.box_y += scene.velocity_y;

    if (scene.box_x <= 0)
    {
        scene.box_x = 0;
        scene.velocity_x = -scene.velocity_x;
    }
    else if (scene.box_x >= max_x)
    {
        scene.box_x = max_x;
        scene.velocity_x = -scene.velocity_x;
    }

    if (scene.box_y <= 0)
    {
        scene.box_y = 0;
        scene.velocity_y = -scene.velocity_y;
    }
    else if (scene.box_y >= max_y)
    {
        scene.box_y = max_y;
        scene.velocity_y = -scene.velocity_y;
    }
}

static bool present_benchmark_regions(uint32_t old_x, uint32_t old_y, uint32_t new_x, uint32_t new_y, uint32_t size)
{
    uint32_t min_x = (old_x < new_x) ? old_x : new_x;
    uint32_t min_y = (old_y < new_y) ? old_y : new_y;
    uint32_t max_x = ((old_x + size) > (new_x + size)) ? (old_x + size) : (new_x + size);
    uint32_t max_y = ((old_y + size) > (new_y + size)) ? (old_y + size) : (new_y + size);

    if (max_x > g_device.fb_width)
    {
        max_x = g_device.fb_width;
    }
    if (max_y > g_device.fb_height)
    {
        max_y = g_device.fb_height;
    }

    return present_rect(min_x, min_y,
                        (max_x > min_x) ? (max_x - min_x) : 0,
                        (max_y > min_y) ? (max_y - min_y) : 0);
}

static void log_benchmark_stats(const char *label, const BenchmarkStats &stats)
{
    if (!stats.ok || stats.frames == 0 || stats.elapsed_us == 0)
    {
        virtio_log("benchmark %s: failed", label);
        return;
    }

    const uint32_t elapsed_us = static_cast<uint32_t>(stats.elapsed_us);
    const uint32_t avg_us = elapsed_us / stats.frames;
    const uint32_t fps_x100 = (static_cast<uint32_t>(stats.frames) * 100000u) / (elapsed_us / 1000u + 1u);
    const uint32_t jitter_us = (stats.min_frame_us != UINT32_MAX && stats.max_frame_us >= stats.min_frame_us)
                                   ? (stats.max_frame_us - stats.min_frame_us) / 2u
                                   : 0u;

    virtio_log("benchmark %s: frames=%u elapsed=%uus avg=%uus/frame fps=%u.%02u jitter=+/-%uus interrupt-mode=%s",
               label,
               stats.frames,
               elapsed_us,
               avg_us,
               fps_x100 / 100u,
               fps_x100 % 100u,
               jitter_us,
               stats.uses_virtqueue ? "Virtqueue" : "Trap");
}

static void log_benchmark_ratio(const char *label, const BenchmarkStats &baseline, const BenchmarkStats &candidate)
{
    if (!baseline.ok || !candidate.ok || baseline.elapsed_us == 0 || candidate.elapsed_us == 0)
    {
        return;
    }

    const uint32_t ratio_x100 = (static_cast<uint32_t>(baseline.elapsed_us) * 100u) /
                                static_cast<uint32_t>(candidate.elapsed_us > 0 ? candidate.elapsed_us : 1);
    virtio_log("benchmark %s speedup: %u.%02ux", label, ratio_x100 / 100u, ratio_x100 % 100u);
}

static BenchmarkStats benchmark_run_full_redraw(BenchmarkSurface &surface, uint32_t frames, bool present_via_virtio)
{
    BenchmarkStats stats{};
    BenchmarkScene scene{};
    benchmark_reset_scene(scene);
    stats.uses_virtqueue = present_via_virtio;

    const auto start = Util::Time::Timestamp::getSystemTime();
    for (uint32_t frame = 0; frame < frames; ++frame)
    {
        const auto frame_start = Util::Time::Timestamp::getSystemTime();
        benchmark_draw_background_region(surface, 0, 0, surface.width, surface.height);
        benchmark_advance_scene(scene, surface);

        const uint32_t box_x = static_cast<uint32_t>(scene.box_x);
        const uint32_t box_y = static_cast<uint32_t>(scene.box_y);
        benchmark_draw_box(surface, box_x, box_y, scene.box_size);

        scene.prev_box_x = box_x;
        scene.prev_box_y = box_y;

        if (present_via_virtio && !present_full_frame())
        {
            return stats;
        }

        const uint32_t frame_us = static_cast<uint32_t>((Util::Time::Timestamp::getSystemTime() - frame_start).toMicroseconds());
        if (frame_us < stats.min_frame_us)
        {
            stats.min_frame_us = frame_us;
        }
        if (frame_us > stats.max_frame_us)
        {
            stats.max_frame_us = frame_us;
        }
    }
    const auto end = Util::Time::Timestamp::getSystemTime();

    stats.ok = true;
    stats.frames = frames;
    stats.elapsed_us = (end - start).toMicroseconds();
    return stats;
}

static BenchmarkStats benchmark_run_dirty_rect(BenchmarkSurface &surface, uint32_t frames, bool present_via_virtio)
{
    BenchmarkStats stats{};
    BenchmarkScene scene{};
    benchmark_reset_scene(scene);
    stats.uses_virtqueue = present_via_virtio;

    benchmark_draw_background_region(surface, 0, 0, surface.width, surface.height);
    benchmark_draw_box(surface, scene.prev_box_x, scene.prev_box_y, scene.box_size);

    if (present_via_virtio && !present_full_frame())
    {
        return stats;
    }

    const auto start = Util::Time::Timestamp::getSystemTime();
    for (uint32_t frame = 0; frame < frames; ++frame)
    {
        const auto frame_start = Util::Time::Timestamp::getSystemTime();
        const uint32_t old_x = scene.prev_box_x;
        const uint32_t old_y = scene.prev_box_y;
        benchmark_draw_background_region(surface, old_x, old_y, scene.box_size, scene.box_size);

        benchmark_advance_scene(scene, surface);
        const uint32_t new_x = static_cast<uint32_t>(scene.box_x);
        const uint32_t new_y = static_cast<uint32_t>(scene.box_y);
        benchmark_draw_box(surface, new_x, new_y, scene.box_size);

        scene.prev_box_x = new_x;
        scene.prev_box_y = new_y;

        if (present_via_virtio && !present_benchmark_regions(old_x, old_y, new_x, new_y, scene.box_size))
        {
            return stats;
        }

        const uint32_t frame_us = static_cast<uint32_t>((Util::Time::Timestamp::getSystemTime() - frame_start).toMicroseconds());
        if (frame_us < stats.min_frame_us)
        {
            stats.min_frame_us = frame_us;
        }
        if (frame_us > stats.max_frame_us)
        {
            stats.max_frame_us = frame_us;
        }
    }
    const auto end = Util::Time::Timestamp::getSystemTime();

    stats.ok = true;
    stats.frames = frames;
    stats.elapsed_us = (end - start).toMicroseconds();
    return stats;
}

void virtio_gpu_run_benchmark()
{
    static bool g_benchmark_ran = false;
    constexpr uint32_t BENCHMARK_FRAMES = 120;

    if (g_benchmark_ran)
    {
        return;
    }
    g_benchmark_ran = true;

    if (!virtio_gpu_is_ready())
    {
        virtio_log("benchmark skipped: device not ready");
        return;
    }

    BenchmarkSurface virtio_surface{};
    if (!benchmark_surface_from_virtio(virtio_surface))
    {
        virtio_log("benchmark skipped: virtio surface unavailable");
        return;
    }

    virtio_log("starting benchmark (%u frames, %ux%u)", BENCHMARK_FRAMES, virtio_surface.width, virtio_surface.height);

    BenchmarkStats legacy_full{};
    BenchmarkStats legacy_dirty{};
    BenchmarkSurface legacy_surface{};
    if (benchmark_surface_from_legacy(legacy_surface) &&
        legacy_surface.width == virtio_surface.width &&
        legacy_surface.height == virtio_surface.height)
    {
        legacy_full = benchmark_run_full_redraw(legacy_surface, BENCHMARK_FRAMES, false);
        legacy_dirty = benchmark_run_dirty_rect(legacy_surface, BENCHMARK_FRAMES, false);
        log_benchmark_stats("legacy-lfb/full-redraw", legacy_full);
        log_benchmark_stats("legacy-lfb/dirty-rect", legacy_dirty);
    }
    else
    {
        virtio_log("benchmark legacy-lfb: skipped (missing, unsupported or resolution mismatch)");
    }

    const BenchmarkStats virtio_full = benchmark_run_full_redraw(virtio_surface, BENCHMARK_FRAMES, true);
    const BenchmarkStats virtio_dirty = benchmark_run_dirty_rect(virtio_surface, BENCHMARK_FRAMES, true);
    log_benchmark_stats("virtio/full-redraw", virtio_full);
    log_benchmark_stats("virtio/dirty-rect", virtio_dirty);

    log_benchmark_ratio("full-redraw legacy->virtio", legacy_full, virtio_full);
    log_benchmark_ratio("dirty-rect legacy->virtio", legacy_dirty, virtio_dirty);
    log_benchmark_ratio("virtio full->dirty", virtio_full, virtio_dirty);
}

// Zeichnet nur den angegebenen Bereich des Hintergrunds neu.

static void draw_background_region(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (!g_device.framebuffer)
    {
        return;
    }

    const uint32_t fb_width = g_device.fb_width;
    const uint32_t fb_height = g_device.fb_height;
    auto *pixels = static_cast<uint32_t *>(g_device.framebuffer);

    const uint32_t x_end = (x + width > fb_width) ? fb_width : (x + width);
    const uint32_t y_end = (y + height > fb_height) ? fb_height : (y + height);

    for (uint32_t py = y; py < y_end; ++py)
    {
        for (uint32_t px = x; px < x_end; ++px)
        {
            pixels[py * fb_width + px] = background_color(px, py);
        }
    }
}

// Einfacher Software-Fill für Rechtecke im ARGB-Framebuffer.
// Diese Funktion ist die Grundlage für das Zeichnen von animierten Objekten und HUD-Elementen,
// und damit für die Demonstration von Dirty-Rect-Updates.
static void draw_filled_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    if (!g_device.framebuffer)
    {
        return;
    }

    const uint32_t fb_width = g_device.fb_width;
    const uint32_t fb_height = g_device.fb_height;
    auto *pixels = static_cast<uint32_t *>(g_device.framebuffer);

    const uint32_t x_end = (x + width > fb_width) ? fb_width : (x + width);
    const uint32_t y_end = (y + height > fb_height) ? fb_height : (y + height);

    for (uint32_t py = y; py < y_end; ++py)
    {
        for (uint32_t px = x; px < x_end; ++px)
        {
            pixels[py * fb_width + px] = color;
        }
    }
}

// Minimaler 7-Segment-Renderer fuer HUD-Ziffern ohne Font-Abhaengigkeit.
// Zeichnet eine einzelne Ziffer an der angegebenen Position mit der gegebenen Farbe.
// Diese Funktion ist die Grundlage für die Anzeige von Frame-Index und anderen Informationen im HUD.
// Die Ziffern werden aus 7 Segmenten zusammengesetzt, die je nach Wert ein- oder ausgeschaltet werden.
// Die Segmentanordnung ist wie folgt (A-G):
//  ---A---
// |       |
// F       B
// |       |
//  ---G---
// |       |
// E       C
// |       |
//  ---D---
static void draw_digit(uint32_t x, uint32_t y, uint32_t digit, uint32_t color)
{
    static constexpr uint8_t SEG_A = 1u << 0;
    static constexpr uint8_t SEG_B = 1u << 1;
    static constexpr uint8_t SEG_C = 1u << 2;
    static constexpr uint8_t SEG_D = 1u << 3;
    static constexpr uint8_t SEG_E = 1u << 4;
    static constexpr uint8_t SEG_F = 1u << 5;
    static constexpr uint8_t SEG_G = 1u << 6;

    static constexpr uint8_t DIGIT_MASKS[10] = {
        static_cast<uint8_t>(SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F),
        static_cast<uint8_t>(SEG_B | SEG_C),
        static_cast<uint8_t>(SEG_A | SEG_B | SEG_G | SEG_E | SEG_D),
        static_cast<uint8_t>(SEG_A | SEG_B | SEG_C | SEG_D | SEG_G),
        static_cast<uint8_t>(SEG_F | SEG_G | SEG_B | SEG_C),
        static_cast<uint8_t>(SEG_A | SEG_F | SEG_G | SEG_C | SEG_D),
        static_cast<uint8_t>(SEG_A | SEG_F | SEG_E | SEG_D | SEG_C | SEG_G),
        static_cast<uint8_t>(SEG_A | SEG_B | SEG_C),
        static_cast<uint8_t>(SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G),
        static_cast<uint8_t>(SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G)};

    if (digit > 9)
    {
        return;
    }

    const uint8_t mask = DIGIT_MASKS[digit];
    const uint32_t thickness = 2;
    const uint32_t segment_w = 12;
    const uint32_t segment_h = 12;

    if (mask & SEG_A)
        draw_filled_rect(x + 2, y, segment_w, thickness, color);
    if (mask & SEG_B)
        draw_filled_rect(x + 2 + segment_w, y + 1, thickness, segment_h, color);
    if (mask & SEG_C)
        draw_filled_rect(x + 2 + segment_w, y + 1 + segment_h, thickness, segment_h, color);
    if (mask & SEG_D)
        draw_filled_rect(x + 2, y + 2 * segment_h, segment_w, thickness, color);
    if (mask & SEG_E)
        draw_filled_rect(x, y + 1 + segment_h, thickness, segment_h, color);
    if (mask & SEG_F)
        draw_filled_rect(x, y + 1, thickness, segment_h, color);
    if (mask & SEG_G)
        draw_filled_rect(x + 2, y + segment_h, segment_w, thickness, color);
}

// Rendert eine Ganzzahl links-nach-rechts aus einzelnen Segment-Ziffern.
static void draw_number(uint32_t x, uint32_t y, uint32_t value, uint32_t color)
{
    uint32_t digits[10]{};
    uint32_t count = 0;

    do
    {
        digits[count++] = value % 10;
        value /= 10;
    } while (value != 0 && count < 10);

    for (uint32_t i = 0; i < count; ++i)
    {
        const uint32_t digit = digits[count - 1 - i];
        draw_digit(x + i * 20u, y, digit, color);
    }
}

// HUD (Head Up Display) mit halbtransparentem Hintergrund und Frame-Zaehler.
// Zeichnet den Hintergrund neu, damit er mit der animierten Szene konsistent bleibt,
// und damit die HUD-Region korrekt per Dirty-Rect aktualisiert werden kann.
static void draw_hud(uint32_t frame_index)
{
    draw_background_region(g_demo.hud_x, g_demo.hud_y, g_demo.hud_w, g_demo.hud_h);
    draw_filled_rect(g_demo.hud_x, g_demo.hud_y, g_demo.hud_w, g_demo.hud_h, 0xCC0A0A0Au);
    draw_filled_rect(g_demo.hud_x, g_demo.hud_y, g_demo.hud_w, 2, 0xFFFFFFFFu);
    draw_filled_rect(g_demo.hud_x, g_demo.hud_y + g_demo.hud_h - 2, g_demo.hud_w, 2, 0xFFFFFFFFu);
    draw_filled_rect(g_demo.hud_x, g_demo.hud_y, 2, g_demo.hud_h, 0xFFFFFFFFu);
    draw_filled_rect(g_demo.hud_x + g_demo.hud_w - 2, g_demo.hud_y, 2, g_demo.hud_h, 0xFFFFFFFFu);

    draw_number(g_demo.hud_x + 12, g_demo.hud_y + 7, frame_index, 0xFFFFFFFFu);
}

// Statisches Checkerboard-Muster als alternativer Demo-Modus.
// Diese Funktion ist die Grundlage für den alternativen Demo-Modus, der ein statisches Schachbrettmuster anzeigt,
// um die Fähigkeit zu demonstrieren, die gesamte Frame-Region effizient zu aktualisieren (ohne Dirty-Rect).

static void draw_checkerboard_full()
{
    if (!g_device.framebuffer)
    {
        return;
    }

    auto *pixels = static_cast<uint32_t *>(g_device.framebuffer);
    for (uint32_t y = 0; y < g_device.fb_height; ++y)
    {
        for (uint32_t x = 0; x < g_device.fb_width; ++x)
        {
            pixels[y * g_device.fb_width + x] = ((x / 50u + y / 50u) % 2u) ? 0xFFFFFFFFu : 0xFF000000u;
        }
    }
}
// schaltet den Modus um
void virtio_gpu_toggle_checker_mode()
{
    const bool current = g_checker_mode.load(std::memory_order_relaxed);
    g_checker_mode.store(!current, std::memory_order_relaxed);
}

// Überträgt genau ein Rechteck zum Host und macht es sichtbar.
// Diese Funktion ist die Grundlage fuer das Presentieren per Dirty-Rect.
static bool present_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return true;
    }

    if (x >= g_device.fb_width || y >= g_device.fb_height)
    {
        return true;
    }

    const uint32_t clipped_width = (x + width > g_device.fb_width) ? (g_device.fb_width - x) : width;
    const uint32_t clipped_height = (y + height > g_device.fb_height) ? (g_device.fb_height - y) : height;

    VirtioGpuCtrlHdr response{};

    VirtioGpuTransferToHost2dCmd transfer{};
    transfer.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    transfer.rect = {x, y, clipped_width, clipped_height};
    transfer.resource_id = FRAME_RESOURCE_ID;

    if (!control_queue_send(&transfer, sizeof(transfer), &response, sizeof(response)) || response.type != VIRTIO_GPU_RESP_OK_NODATA)
    {
        virtio_log("TRANSFER_TO_HOST_2D failed");
        return false;
    }

    VirtioGpuResourceFlushCmd flush{};
    flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    flush.rect = {x, y, clipped_width, clipped_height};
    flush.resource_id = FRAME_RESOURCE_ID;

    if (!control_queue_send(&flush, sizeof(flush), &response, sizeof(response)) || response.type != VIRTIO_GPU_RESP_OK_NODATA)
    {
        virtio_log("RESOURCE_FLUSH failed");
        return false;
    }

    return true;
}

// Überträgt die alte/neue Objektregion und danach den HUD-Bereich separat.
static bool present_demo_regions(uint32_t old_x, uint32_t old_y, uint32_t new_x, uint32_t new_y)
{
    const uint32_t size = g_demo.box_size;

    uint32_t min_x = (old_x < new_x) ? old_x : new_x;
    uint32_t min_y = (old_y < new_y) ? old_y : new_y;
    uint32_t max_x = ((old_x + size) > (new_x + size)) ? (old_x + size) : (new_x + size);
    uint32_t max_y = ((old_y + size) > (new_y + size)) ? (old_y + size) : (new_y + size);

    if (max_x > g_device.fb_width)
    {
        max_x = g_device.fb_width;
    }
    if (max_y > g_device.fb_height)
    {
        max_y = g_device.fb_height;
    }

    const uint32_t object_width = (max_x > min_x) ? (max_x - min_x) : 0;
    const uint32_t object_height = (max_y > min_y) ? (max_y - min_y) : 0;

    if (!present_rect(min_x, min_y, object_width, object_height))
    {
        return false;
    }

    return present_rect(g_demo.hud_x, g_demo.hud_y, g_demo.hud_w, g_demo.hud_h);
}
// 1
// wechsel von statischem Checkerboard-Muster zu animierter Demo mit bewegtem Objekt und HUD.
/// vorher : einmaliges Zeichnen des SchachbrettMusters,Bildes
// nachher : kontinuierliches Rendern von Frames mit animiertem Objekt und HUD.
bool virtio_gpu_demo_render_frame(uint32_t frame_index)
{
    static bool last_checker_mode = false;
    static bool checker_presented = false;

    if (!g_device.framebuffer)
    {
        return false;
    }

    const bool checker_mode = g_checker_mode.load(std::memory_order_relaxed);
    if (checker_mode)
    {
        if (!last_checker_mode)
        {
            checker_presented = false;
        }

        if (!checker_presented)
        {
            draw_checkerboard_full();
            if (!present_full_frame())
            {
                return false;
            }
            checker_presented = true;
        }

        last_checker_mode = true;
        return true;
    }

    if (last_checker_mode)
    {
        // Beim Verlassen des Checker-Modus Szene vollständig neu initialisieren.
        g_demo.initialized = false;
        last_checker_mode = false;
    }

    if (!g_demo.initialized)
    {
        // Einmalige Initialisierung des Szenenzustands.
        g_demo.initialized = true;
        g_demo.box_x = 20;
        g_demo.box_y = 20;
        g_demo.prev_box_x = static_cast<uint32_t>(g_demo.box_x);
        g_demo.prev_box_y = static_cast<uint32_t>(g_demo.box_y);
        draw_background_region(0, 0, g_device.fb_width, g_device.fb_height);
    }

    const uint32_t old_x = g_demo.prev_box_x;
    const uint32_t old_y = g_demo.prev_box_y;

    // Vorherige Objektposition zurück auf Hintergrund setzen.
    draw_background_region(old_x, old_y, g_demo.box_size, g_demo.box_size);

    const int32_t max_x = static_cast<int32_t>((g_device.fb_width > g_demo.box_size) ? (g_device.fb_width - g_demo.box_size) : 0);
    const int32_t max_y = static_cast<int32_t>((g_device.fb_height > g_demo.box_size) ? (g_device.fb_height - g_demo.box_size) : 0);

    g_demo.box_x += g_demo.velocity_x;
    g_demo.box_y += g_demo.velocity_y;

    // Abprall-Physik: bei Randkontakt Position begrenzen und Richtung invertieren.
    if (g_demo.box_x <= 0)
    {
        g_demo.box_x = 0;
        g_demo.velocity_x = -g_demo.velocity_x;
    }
    else if (g_demo.box_x >= max_x)
    {
        g_demo.box_x = max_x;
        g_demo.velocity_x = -g_demo.velocity_x;
    }

    if (g_demo.box_y <= 0)
    {
        g_demo.box_y = 0;
        g_demo.velocity_y = -g_demo.velocity_y;
    }
    else if (g_demo.box_y >= max_y)
    {
        g_demo.box_y = max_y;
        g_demo.velocity_y = -g_demo.velocity_y;
    }

    const uint32_t new_x = static_cast<uint32_t>(g_demo.box_x);
    const uint32_t new_y = static_cast<uint32_t>(g_demo.box_y);

    // Objekt mit Rahmen und Kreuz für bessere Sichtbarkeit zeichnen.
    draw_filled_rect(new_x, new_y, g_demo.box_size, g_demo.box_size, 0xFF111111u);
    draw_filled_rect(new_x, new_y, g_demo.box_size, 3, 0xFFFFFFFFu);
    draw_filled_rect(new_x, new_y + g_demo.box_size - 3, g_demo.box_size, 3, 0xFFFFFFFFu);
    draw_filled_rect(new_x, new_y, 3, g_demo.box_size, 0xFFFFFFFFu);
    draw_filled_rect(new_x + g_demo.box_size - 3, new_y, 3, g_demo.box_size, 0xFFFFFFFFu);
    draw_filled_rect(new_x + 10, new_y + g_demo.box_size / 2 - 2, g_demo.box_size - 20, 4, 0xFF00C8FFu);
    draw_filled_rect(new_x + g_demo.box_size / 2 - 2, new_y + 10, 4, g_demo.box_size - 20, 0xFF00C8FFu);

    draw_hud(frame_index);

    // Neue Position als Ausgangspunkt für den nächsten Frame speichern.
    g_demo.prev_box_x = new_x;
    g_demo.prev_box_y = new_y;

    return present_demo_regions(old_x, old_y, new_x, new_y);
}

// presentiert das statischen Modus Bild vollflächig
static bool present_full_frame()
{
    return present_rect(0, 0, g_device.fb_width, g_device.fb_height);
}

// Erzeugt den Framebuffer als VirtIO-2D-Ressource und stellt eine
// Initialausgabe (animierter Demo-Frame) dar.
// Diese Funktion ist die Grundlage für die gesamte Demo, da sie die Ressource anlegt, das Backing bindet,
// sie dem Scanout zuordnet und den ersten Frame rendert und präsentiert.
// Alle weiteren Demo-Frames bauen auf diesem Setup auf, um die Fähigkeiten von Dirty-Rect-Updates und vollständigen Frame-Updates zu demonstrieren.
// Bei Fehlern wird false zurückgegeben, damit die Initialisierung abgebrochen werden kann.

bool create_framebuffer_resource()
{
    g_device.fb_width = FRAME_WIDTH;
    g_device.fb_height = FRAME_HEIGHT;
    g_device.framebuffer_bytes = static_cast<size_t>(FRAME_WIDTH) * FRAME_HEIGHT * BYTES_PER_PIXEL;
    g_device.framebuffer_pages = virtqueue_bytes_to_pages(g_device.framebuffer_bytes);

    uint64_t fb_phys = 0;
    void *fb = alloc_dma_pages(g_device.framebuffer_pages, &fb_phys);
    if (!fb)
    {
        virtio_log("framebuffer allocation failed");
        return false;
    }

    std::memset(fb, 0, g_device.framebuffer_pages * VIRTQUEUE_PAGE_SIZE);
    g_device.framebuffer = fb;
    g_device.framebuffer_phys = fb_phys;

    VirtioGpuResourceCreate2dCmd create{};
    create.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    create.resource_id = FRAME_RESOURCE_ID;
    create.format = static_cast<uint32_t>(VirtioGpuFormat::B8G8R8X8_UNORM);
    create.width = FRAME_WIDTH;
    create.height = FRAME_HEIGHT;

    VirtioGpuCtrlHdr response{};
    if (!control_queue_send(&create, sizeof(create), &response, sizeof(response)) || response.type != VIRTIO_GPU_RESP_OK_NODATA)
    {
        virtio_log("RESOURCE_CREATE_2D failed");
        return false;
    }

    VirtioGpuResourceAttachBackingCmd attach{};
    attach.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    attach.resource_id = FRAME_RESOURCE_ID;
    attach.nr_entries = 1;
    attach.entries[0].addr = g_device.framebuffer_phys;
    attach.entries[0].length = static_cast<uint32_t>(g_device.framebuffer_bytes);

    if (!control_queue_send(&attach, sizeof(attach), &response, sizeof(response)) || response.type != VIRTIO_GPU_RESP_OK_NODATA)
    {
        virtio_log("RESOURCE_ATTACH_BACKING failed");
        return false;
    }

    VirtioGpuSetScanoutCmd scanout{};
    scanout.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    scanout.rect = {0, 0, FRAME_WIDTH, FRAME_HEIGHT};
    scanout.scanout_id = 0;
    scanout.resource_id = FRAME_RESOURCE_ID;

    if (!control_queue_send(&scanout, sizeof(scanout), &response, sizeof(response)) || response.type != VIRTIO_GPU_RESP_OK_NODATA)
    {
        virtio_log("SET_SCANOUT failed");
        return false;
    }

    if (!virtio_gpu_demo_render_frame(0))
    {
        return false;
    }

    if (!present_full_frame())
    {
        return false;
    }

    return true;
}