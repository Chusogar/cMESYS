// hdmi_glue.cpp - the C interface of the HDMI (picture and sound) of the
// RP2350-PiZero port, over dvi/ (see README.md).
#include "hdmi_glue.h"
#include "dvi/dvi.h"
#include <hardware/pio.h>
#include <hardware/irq.h>
#include <hardware/structs/nvic.h>
#include <pico/platform.h>

namespace
{
    // D0 36/37, D1 34/35, D2 32/33, CLK 38/39
    const dvi::Config pizero_cfg = {
        .pinTMDS = {36, 34, 32},
        .pinClock = 38,
        .invert = false,
    };
    dvi::DVI *hdmi;
    // The rows are TMDS-encoded in a low priority interrupt that the DMA's
    // raises after every line: the DMA's own (every line, the data islands
    // with the sound) is never late behind an encode, and the encode is
    // never late behind core 1's drawing.
    void (*rowCb)(void *);
    void *rowUser;
    int encIrq = -1;

    void __not_in_flash_func(lineHook)(void *)
    {
        nvic_hw->ispr[encIrq / 32] = 1u << (encIrq % 32);   // (irq_set_pending is in flash)
    }

    void __not_in_flash_func(encodeIRQ)()
    {
        rowCb(rowUser);
    }
    constexpr int AUDIO_RING = 2048;        // stereo samples (46 ms at 44.1 kHz)
}

extern "C" void pzhdmi_init(int audio_hz, void (*line_cb)(void *), void *user)
{
    hdmi = new dvi::DVI(pio1, &pizero_cfg, dvi::getTiming640x480p60Hz());
    rowCb = line_cb;
    rowUser = user;
    if (audio_hz > 0)
    {
        // N for the rate (HDMI 1.4 table 7-1); the CTS is worked out from the pixel clock
        const int N = audio_hz == 48000 ? 6144 : audio_hz == 32000 ? 4096 : 6272;
        hdmi->setAudioFreq(audio_hz, 0, N);
        hdmi->allocateAudioBuffer(AUDIO_RING);
        hdmi->getAudioRingBuffer().advanceWritePointer(255);       // a little silence first
    }
}

extern "C" void __not_in_flash_func(pzhdmi_core1_start)(void)
{
    encIrq = user_irq_claim_unused(true);
    irq_set_exclusive_handler(encIrq, encodeIRQ);
    irq_set_priority(encIrq, PICO_LOWEST_IRQ_PRIORITY);
    irq_set_enabled(encIrq, true);
    hdmi->setLineCallback(lineHook, nullptr);
    hdmi->registerIRQThisCore();
    irq_set_priority(DMA_IRQ_0, PICO_HIGHEST_IRQ_PRIORITY);
    hdmi->start();
}

extern "C" bool __not_in_flash_func(pzhdmi_try_row)(int first, int count, const uint16_t *src, int width)
{
    return hdmi->tryConvertRow565(first, count, src, width);
}

extern "C" unsigned pzhdmi_audio_free(void)
{
    return hdmi ? hdmi->getAudioRingBuffer().getFullWritableSize() : 0;
}

extern "C" void pzhdmi_audio_put(int16_t left, int16_t right)
{
    auto &ring = hdmi->getAudioRingBuffer();
    if (!ring.getWritableSize())
        return;
    *ring.getWritePointer() = {left, right};
    ring.advanceWritePointer(1);
}

extern "C" unsigned pzhdmi_missed_lines(void)
{
    return hdmi ? hdmi->getMissedLines() : 0;
}

extern "C" int __not_in_flash_func(pzhdmi_active_line)(void)
{
    return hdmi->getActiveLine();
}

// std::vector's only exception: without this libstdc++'s exception support
// comes in, and with it a 70 KB emergency pool malloc'ed before main()
// (SRAM the games need: OutRun did not start)
namespace std
{
    void __throw_length_error(const char *)
    {
        panic("hdmi: vector length");
    }
}
