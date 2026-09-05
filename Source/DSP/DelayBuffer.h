/*
    DelayBuffer.h
    ---------------------------------------------------------------------------
    Three storage primitives the rest of the engine is built from.

      DelayLine     — mono, addressed by "how far behind the write head".
                      Used inside allpasses, the reverb FDN and the shifter.

      TapeBuffer    — stereo, addressed by ABSOLUTE sample position. This is
                      the tape. Grains need to name a point on the tape and
                      keep reading from it at their own speed while the write
                      head runs on ahead, and a "delay of N samples" address
                      cannot express that. An absolute clock can.

      ReverseReader — plays the tape backwards at unity speed, forever, with
                      no click at the loop point.

    Both buffers are power-of-two sized so wrapping is a mask, not a modulo.
*/

#pragma once

#include "DspCommon.h"

#include <array>
#include <vector>

namespace gush
{

//==============================================================================
class DelayLine
{
public:
    void prepare (double sampleRate, float maxSeconds)
    {
        const int needed = (int) std::ceil (sampleRate * (double) maxSeconds) + 8;
        int size = 8;
        while (size < needed) size <<= 1;

        buffer.assign ((size_t) size, 0.0f);
        mask       = size - 1;
        writeIndex = 0;
    }

    void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); writeIndex = 0; }

    void push (float x) noexcept
    {
        buffer[(size_t) writeIndex] = x;
        writeIndex = (writeIndex + 1) & mask;
    }

    /** Reads `delaySamples` behind the most recently written sample.
        Fractional, Hermite-interpolated, so it can be modulated smoothly. */
    float read (float delaySamples) const noexcept
    {
        const float d = clampf (delaySamples, 1.0f, (float) (mask - 3));
        const int   i = (int) d;
        const float f = d - (float) i;

        // Larger delay == further back in time, so the neighbours walk backwards.
        const int base = (writeIndex - 1 - i) & mask;
        const float ym1 = buffer[(size_t) ((base + 1) & mask)];
        const float y0  = buffer[(size_t)   base];
        const float y1  = buffer[(size_t) ((base - 1) & mask)];
        const float y2  = buffer[(size_t) ((base - 2) & mask)];

        return hermite (ym1, y0, y1, y2, f);
    }

    int capacity() const noexcept { return mask + 1; }

private:
    std::vector<float> buffer;
    int mask = 0, writeIndex = 0;
};

//==============================================================================
/** The tape. Stereo, absolutely addressed, fractionally readable. */
class TapeBuffer
{
public:
    void prepare (double sampleRate, float maxSeconds)
    {
        const int needed = (int) std::ceil (sampleRate * (double) maxSeconds) + 16;
        int size = 16;
        while (size < needed) size <<= 1;

        for (auto& ch : data) ch.assign ((size_t) size, 0.0f);
        mask       = size - 1;
        sizeInSamples = size;
        writeCount = 0;
    }

    void reset()
    {
        for (auto& ch : data) std::fill (ch.begin(), ch.end(), 0.0f);
        writeCount = 0;
    }

    void write (float l, float r) noexcept
    {
        const size_t i = (size_t) (writeCount & (int64_t) mask);
        data[0][i] = flush (l);
        data[1][i] = flush (r);
        ++writeCount;
    }

    /** Absolute position of the next sample to be written. */
    double writePosition() const noexcept { return (double) writeCount; }
    int    length()        const noexcept { return sizeInSamples; }

    /** Reads at an absolute (fractional) tape position. Positions outside the
        window that is still on the tape are clamped rather than wrapped: a
        grain that drifts too far gets stuck at the edge instead of suddenly
        playing something from eight seconds ago. */
    float read (int channel, double absolutePos) const noexcept
    {
        const double newest = (double) writeCount - 3.0;
        const double oldest = (double) writeCount - (double) sizeInSamples + 4.0;

        double p = absolutePos;
        if (p > newest) p = newest;
        if (p < oldest) p = oldest;

        const int64_t i = (int64_t) std::floor (p);
        const float   f = (float) (p - (double) i);
        const auto&   b = data[(size_t) channel];
        const int64_t m = (int64_t) mask;

        const float ym1 = b[(size_t) ((i - 1) & m)];
        const float y0  = b[(size_t) ( i      & m)];
        const float y1  = b[(size_t) ((i + 1) & m)];
        const float y2  = b[(size_t) ((i + 2) & m)];

        return hermite (ym1, y0, y1, y2, f);
    }

private:
    std::array<std::vector<float>, 2> data;
    int     mask = 0, sizeInSamples = 0;
    int64_t writeCount = 0;
};

//==============================================================================
/** Backwards playback with no click at the wrap.

    A single reverse pointer has to jump somewhere when it runs out of tape,
    and that jump is a click. So we run TWO pointers half a cycle apart and
    crossfade them with Hann windows, which sum to exactly 1: whenever one
    pointer is jumping, its window is at zero, so the jump is inaudible.

    Read position moves at -1 sample per sample: the write head advances by 1
    while our offset grows by 2, so the net motion through the material is
    exactly reverse at unity speed and unity pitch.
*/
class ReverseReader
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr     = sampleRate;
        len    = (float) sampleRate * 0.5f;
        target = len;
        phase  = 0.0f;
    }

    void reset() noexcept { phase = 0.0f; }

    void setLength (float samples) noexcept
    {
        target = clampf (samples, 512.0f, (float) sr * 3.0f);
    }

    float process (const TapeBuffer& tape, int channel, double writePos) noexcept
    {
        // Only retune at a window boundary, so a moving delay knob never
        // snaps the sweep mid-flight.
        if (phase < 0.0008f) len = target;

        phase += 1.0f / len;
        if (phase >= 1.0f) phase -= 1.0f;

        const float p2 = (phase >= 0.5f) ? phase - 0.5f : phase + 0.5f;

        const double a = writePos - 6.0 - 2.0 * (double) len * (double) phase;
        const double b = writePos - 6.0 - 2.0 * (double) len * (double) p2;

        return tape.read (channel, a) * hann (phase)
             + tape.read (channel, b) * hann (p2);
    }

private:
    double sr = 48000.0;
    float  len = 24000.0f, target = 24000.0f, phase = 0.0f;
};

} // namespace gush
