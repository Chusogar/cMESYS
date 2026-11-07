// hdmi_glue.cpp - the C interface of the HDMI (picture and sound) of the
// RP2350-PiZero port, over dvi/ (see README.md).
#include "hdmi_glue.h"
#include "dvi/dvi.h"
#include <hardware/pio.h>

namespace
{
    // D0 36/37, D1 34/35, D2 32/33, CLK 38/39
    const dvi::Config pizero_cfg = {
        .pinTMDS = {36, 34, 32},
        .pinClock = 38,
        .invert = false,
    };
    dvi::DVI *hdmi;
    constexpr int AUDIO_RING = 2048;        // stereo samples (46 ms at 44.1 kHz)
}

extern "C" void pzhdmi_init(int audio_hz, void (*line_cb)(void *), void *user)
{
    hdmi = new dvi::DVI(pio1, &pizero_cfg, dvi::getTiming640x480p60Hz());
    hdmi->setLineCallback(line_cb, user);
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
    hdmi->registerIRQThisCore();
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
