/*
 * Noise Padifier - stereo convolution with long impulse responses, low latency, no allocation in the
 * audio thread.
 *
 * Copyright (C) 2026 Padifier authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Output o = input o convolved with h[o][o], plus, with cross responses (true stereo), the other
 * input convolved with h[1 - o][o].  Without them the cost is that of two convolutions; with
 * them the products double (the spectra of the inputs are shared).
 * Two stages of uniformly partitioned overlap-save convolution (FFT: pffft), sharing the
 * frequency-domain delay lines of the input:
 * - head: blocks of B = 256 samples, response samples [0, 2M); latency B;
 * - tail: blocks of M = 4096 (fs <= 50 kHz), 8192 (<= 100 kHz) or 16384 samples, response
 *   samples [2M, N).  Block j of the input (M samples) is complete at the end of frame
 *   (j + 1) R - 1 (R = M / B frames of B); its contribution starts at sample jM + 2M, i.e.
 *   frame (j + 2) R.  The work (one FFT, the products of the P tail partitions, one inverse
 *   FFT) is spread over the R frames in between: P / R partitions per frame.
 * Cost per sample and output, 10 s at 48 kHz: 32 + 116 complex products (twice with cross).
 *
 * Change of response: the new set of spectra (built elsewhere, IRSet) is taken at the start of
 * a tail block (frame A, A = 0 mod R).  The delay lines hold the spectra of the input, whatever
 * the response, so the new response has the whole past of the input at once.  The tail job
 * started at A is computed with both responses; during frames [A + R, A + 2R) the output is
 * the crossfade (1 - g) old + g new, g linear from 0 to 1 over those M samples (both head and
 * tail computed for both); from A + 2R on: the new response only, the old set is handed back
 * (retired) to be freed outside the audio thread.
 */
#ifndef PADIFIER_CONVOLVER_HPP_INCLUDED
#define PADIFIER_CONVOLVER_HPP_INCLUDED

#include "pffft/pffft.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

namespace PadifierConv {

static const int kHeadBlock = 256;   // B: latency, samples

static inline int tailBlock(const double fs)
{
    return fs <= 50000.0 ? 4096 : fs <= 100000.0 ? 8192 : 16384;
}

// float array aligned for pffft, zeroed
class Buffer
{
public:
    Buffer() = default;
    explicit Buffer(const size_t n) { allocate(n); }
    ~Buffer() { pffft_aligned_free(fData); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    void allocate(const size_t n)
    {
        pffft_aligned_free(fData);
        fData = n ? static_cast<float*>(pffft_aligned_malloc(sizeof(float) * n)) : nullptr;
        fSize = n;
        clear();
    }
    void clear()
    {
        if (fData)
            std::memset(fData, 0, sizeof(float) * fSize);
    }
    float* data() { return fData; }
    const float* data() const { return fData; }
    float& operator[](const size_t i) { return fData[i]; }
    size_t size() const { return fSize; }
    void swap(Buffer& o)
    {
        std::swap(fData, o.fData);
        std::swap(fSize, o.fSize);
    }

private:
    float* fData = nullptr;
    size_t fSize = 0;
};

// FFT sizes 2B and 2M for one sample rate; read-only once made, shared by threads
class Setups
{
public:
    Setups(const int B, const int M)
        : fB(B), fM(M), fHead(pffft_new_setup(2 * B, PFFFT_REAL)), fTail(pffft_new_setup(2 * M, PFFFT_REAL)) {}
    ~Setups()
    {
        pffft_destroy_setup(fHead);
        pffft_destroy_setup(fTail);
    }
    Setups(const Setups&) = delete;
    Setups& operator=(const Setups&) = delete;
    int B() const { return fB; }
    int M() const { return fM; }
    const PFFFT_Setup* head() const { return fHead; }
    const PFFFT_Setup* tail() const { return fTail; }

private:
    int fB, fM;
    PFFFT_Setup* fHead;
    PFFFT_Setup* fTail;
};

// Spectra of the partitions of the responses h[input][output] (scaled for the inverse FFT).
struct IRSet
{
    int B = 0, M = 0, length = 0, headParts = 0, tailParts = 0;
    bool cross = false;           // h[1][0] and h[0][1] present
    Buffer head[2][2], tail[2][2];   // headParts x 2B, tailParts x 2M floats, pffft order

    bool has(const int i, const int o) const { return i == o || cross; }

    static int headPartsFor(const int n, const int B, const int M) { return (std::min(n, 2 * M) + B - 1) / B; }
    static int tailPartsFor(const int n, const int M) { return n > 2 * M ? (n - 2 * M + M - 1) / M : 0; }

    // h[i][o]: n samples from input i to output o; the cross ones (i != o) may be null
    IRSet(const Setups& s, const float* const h[2][2], const int n)
        : B(s.B()), M(s.M()), length(n), headParts(headPartsFor(n, B, M)), tailParts(tailPartsFor(n, M)),
          cross(h[1][0] != nullptr && h[0][1] != nullptr)
    {
        Buffer tmp(2 * static_cast<size_t>(M)), work(2 * static_cast<size_t>(M));
        for (int i = 0; i < 2; ++i)
            for (int o = 0; o < 2; ++o)
            {
                if (!has(i, o))
                    continue;
                head[i][o].allocate(static_cast<size_t>(headParts) * 2 * B);
                tail[i][o].allocate(static_cast<size_t>(tailParts) * 2 * M);
                for (int p = 0; p < headParts; ++p)
                    partition(s.head(), h[i][o], n, p * B, B, head[i][o].data() + static_cast<size_t>(p) * 2 * B, tmp, work);
                for (int p = 0; p < tailParts; ++p)
                    partition(s.tail(), h[i][o], n, 2 * M + p * M, M, tail[i][o].data() + static_cast<size_t>(p) * 2 * M, tmp,
                              work);
            }
    }

private:
    // spectrum of h[from, from + size) zero-padded to 2 size, divided by 2 size
    static void partition(const PFFFT_Setup* setup, const float* h, const int n, const int from, const int size,
                          float* out, Buffer& tmp, Buffer& work)
    {
        const int count = std::max(0, std::min(size, n - from));
        std::memset(tmp.data(), 0, sizeof(float) * 2 * size);
        const float scale = 1.0f / (2 * size);
        for (int i = 0; i < count; ++i)
            tmp[i] = h[from + i] * scale;
        pffft_transform(setup, tmp.data(), out, work.data(), PFFFT_FORWARD);
    }
};

class Engine
{
public:
    // Allocates everything for responses up to maxLength samples.  Not real-time.
    void configure(const Setups* setups, const int maxLength)
    {
        discard();
        fS = setups;
        B = setups->B();
        M = setups->M();
        R = M / B;
        fMaxHead = IRSet::headPartsFor(maxLength, B, M);
        fMaxTail = std::max(1, IRSet::tailPartsFor(maxLength, M));
        for (int c = 0; c < 2; ++c)
        {
            fIn[c].allocate(B);
            fDry[c].allocate(B);
            fHeadWin[c].allocate(2 * B);
            fHeadFdl[c].allocate(static_cast<size_t>(fMaxHead) * 2 * B);
            fTailWin[c].allocate(2 * M);
            fCollect[c].allocate(M);
            fTailFdl[c].allocate(static_cast<size_t>(fMaxTail) * 2 * M);
            for (int l = 0; l < 2; ++l)
            {
                Lane& ln = fLane[l];
                ln.out[c].allocate(B);
                ln.tailAcc[c].allocate(2 * M);
                ln.tailCur[c].allocate(M);
                ln.tailNext[c].allocate(M);
            }
        }
        fHeadAcc.allocate(2 * B);
        fTmp.allocate(2 * M);
        fWork.allocate(2 * M);
        reset();
    }

    // Clears the signal state (not the responses).  Not real-time.
    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            fIn[c].clear();
            fDry[c].clear();
            fHeadWin[c].clear();
            fHeadFdl[c].clear();
            fTailWin[c].clear();
            fCollect[c].clear();
            fTailFdl[c].clear();
            for (int l = 0; l < 2; ++l)
            {
                fLane[l].out[c].clear();
                fLane[l].tailAcc[c].clear();
                fLane[l].tailCur[c].clear();
                fLane[l].tailNext[c].clear();
            }
        }
        fPos = 0;
        fFrame = 0;
        fHeadPos = fTailPos = 0;
        if (fFade != kNone)   // a change cut short: the new response stays
        {
            delete fLane[fActive].set;
            fLane[fActive].set = nullptr;
            fActive = 1 - fActive;
        }
        fFade = kNone;
    }

    // Response used at once (not real-time; the previous one is retired)
    void setNow(IRSet* const set)
    {
        delete fLane[fActive].set;
        fLane[fActive].set = set;
    }

    // ---- exchange with the thread building the responses (single producer, single consumer)

    // builder: offers a new response; an older one not yet taken is returned (to be freed)
    IRSet* offer(IRSet* const set) { return fPending.exchange(set, std::memory_order_acq_rel); }

    // builder: a response no longer used, to be freed (nullptr: none)
    IRSet* takeRetired() { return fRetired.exchange(nullptr, std::memory_order_acq_rel); }

    // not real-time, no concurrent run: frees everything pending (end, change of sample rate)
    void discard()
    {
        delete fPending.exchange(nullptr);
        delete fRetired.exchange(nullptr);
        delete fToRetire;
        fToRetire = nullptr;
        for (int l = 0; l < 2; ++l)
        {
            delete fLane[l].set;
            fLane[l].set = nullptr;
        }
        fFade = kNone;
    }

    bool crossfading() const { return fFade != kNone || fPending.load(std::memory_order_relaxed) != nullptr; }
    const IRSet* current() const { return fLane[fActive].set; }
    int latency() const { return B; }

    // ---- audio

    // wet: the convolution; dry: the input delayed as much (latency B).  Any n.
    void process(const float* const in[2], float* const wet[2], float* const dry[2], const int n)
    {
        int done = 0;
        while (done < n)
        {
            const int k = std::min(n - done, B - fPos);
            for (int c = 0; c < 2; ++c)
            {
                std::memcpy(fIn[c].data() + fPos, in[c] + done, sizeof(float) * k);
                std::memcpy(dry[c] + done, fDry[c].data() + fPos, sizeof(float) * k);
                mixOut(c, wet[c] + done, k);
            }
            fPos += k;
            done += k;
            if (fPos == B)
            {
                frame();
                fPos = 0;
            }
        }
    }

private:
    enum { kNone = 0, kWaiting, kRamping };

    struct Lane {
        IRSet* set = nullptr;
        bool job = false;           // the tail job running computes this lane
        Buffer out[2];              // head output of the frame being played
        Buffer tailAcc[2];          // products of the running tail job
        Buffer tailCur[2];          // tail output of the current tail block (M samples)
        Buffer tailNext[2];         // result of the running job, played from the next block
    };

    // samples [fPos, fPos + k) of the frame being played
    void mixOut(const int c, float* const y, const int k)
    {
        const int phase = static_cast<int>((fFrame + R - 1) % R);   // output frame fFrame - 1
        const Lane& a = fLane[fActive];
        const Lane& b = fLane[1 - fActive];
        const float* ha = a.out[c].data() + fPos;
        const float* ta = a.tailCur[c].data() + phase * B + fPos;
        if (fFade != kRamping)
        {
            for (int i = 0; i < k; ++i)
                y[i] = ha[i] + ta[i];
            return;
        }
        // ramp over the R frames of the tail block, phase * B + fPos .. : g = t / M
        const float* hb = b.out[c].data() + fPos;
        const float* tb = b.tailCur[c].data() + phase * B + fPos;
        const float step = 1.0f / M;
        const int t0 = phase * B + fPos;
        for (int i = 0; i < k; ++i)
        {
            const float g = (t0 + i) * step;
            y[i] = (ha[i] + ta[i]) + g * ((hb[i] + tb[i]) - (ha[i] + ta[i]));
        }
    }

    void retire(IRSet* const set)
    {
        if (set == nullptr)
            return;
        fToRetire = set;   // empty here: a change is taken only when it is (tailBlockStart)
        flushRetired();
    }

    void flushRetired()
    {
        if (fToRetire && fRetired.load(std::memory_order_acquire) == nullptr)
        {
            fRetired.store(fToRetire, std::memory_order_release);
            fToRetire = nullptr;
        }
    }

    // a complete frame of B input samples: computes the output of the next frame
    void frame()
    {
        const int phase = static_cast<int>(fFrame % R);   // input frame fFrame, output frame fFrame
        flushRetired();

        // dry path: this frame's input, played during the next one
        for (int c = 0; c < 2; ++c)
            std::memcpy(fDry[c].data(), fIn[c].data(), sizeof(float) * B);

        if (phase == 0)
            tailBlockStart();

        // tail: products of this frame's share, inverse FFT on the last frame of the block
        for (int l = 0; l < 2; ++l)
        {
            Lane& ln = fLane[l];
            if (!ln.job || ln.set == nullptr)
                continue;
            const int P = ln.set->tailParts;
            const int p0 = phase * P / R, p1 = (phase + 1) * P / R;
            for (int c = 0; c < 2; ++c)   // output
            {
                float* const acc = ln.tailAcc[c].data();
                for (int i = 0; i < 2; ++i)   // input
                {
                    if (!ln.set->has(i, c))
                        continue;
                    for (int p = p0; p < p1; ++p)
                    {
                        const int slot = (fTailPos - p + fMaxTail) % fMaxTail;
                        pffft_zconvolve_accumulate(fS->tail(), fTailFdl[i].data() + static_cast<size_t>(slot) * 2 * M,
                                                   ln.set->tail[i][c].data() + static_cast<size_t>(p) * 2 * M, acc, 1.0f);
                    }
                }
                if (phase == R - 1)
                {
                    pffft_transform(fS->tail(), acc, fTmp.data(), fWork.data(), PFFFT_BACKWARD);
                    std::memcpy(ln.tailNext[c].data(), fTmp.data() + M, sizeof(float) * M);
                }
            }
        }

        // collect this frame for the next tail block
        for (int c = 0; c < 2; ++c)
            std::memcpy(fCollect[c].data() + static_cast<size_t>(phase) * B, fIn[c].data(), sizeof(float) * B);

        // head: spectrum of the last 2B samples, products, inverse FFT
        fHeadPos = (fHeadPos + 1) % fMaxHead;
        for (int c = 0; c < 2; ++c)
        {
            float* const win = fHeadWin[c].data();
            std::memmove(win, win + B, sizeof(float) * B);
            std::memcpy(win + B, fIn[c].data(), sizeof(float) * B);
            pffft_transform(fS->head(), win, fHeadFdl[c].data() + static_cast<size_t>(fHeadPos) * 2 * B, fWork.data(),
                            PFFFT_FORWARD);
        }
        for (int l = 0; l < 2; ++l)
        {
            Lane& ln = fLane[l];
            const bool audible = l == fActive || fFade == kRamping;
            for (int c = 0; c < 2; ++c)   // output
            {
                if (!audible || ln.set == nullptr)
                {
                    ln.out[c].clear();
                    continue;
                }
                float* const acc = fHeadAcc.data();
                std::memset(acc, 0, sizeof(float) * 2 * B);
                for (int i = 0; i < 2; ++i)   // input
                {
                    if (!ln.set->has(i, c))
                        continue;
                    for (int p = 0; p < ln.set->headParts; ++p)
                    {
                        const int slot = (fHeadPos - p + fMaxHead) % fMaxHead;
                        pffft_zconvolve_accumulate(fS->head(), fHeadFdl[i].data() + static_cast<size_t>(slot) * 2 * B,
                                                   ln.set->head[i][c].data() + static_cast<size_t>(p) * 2 * B, acc, 1.0f);
                    }
                }
                pffft_transform(fS->head(), acc, fTmp.data(), fWork.data(), PFFFT_BACKWARD);
                std::memcpy(ln.out[c].data(), fTmp.data() + B, sizeof(float) * B);
            }
        }
        ++fFrame;
    }

    // first frame of a tail block (fFrame = 0 mod R): the block just completed is transformed,
    // the results of the previous job are played, a change of response goes one step further
    void tailBlockStart()
    {
        for (int l = 0; l < 2; ++l)
            for (int c = 0; c < 2; ++c)
                fLane[l].tailCur[c].swap(fLane[l].tailNext[c]);

        // crossfade state machine (see the head of the file)
        if (fFade == kRamping)
        {
            fFade = kNone;
            retire(fLane[fActive].set);
            fLane[fActive].set = nullptr;
            fActive = 1 - fActive;
        }
        else if (fFade == kWaiting)
            fFade = kRamping;
        if (fFade == kNone && fToRetire == nullptr)
        {
            IRSet* const set = fPending.exchange(nullptr, std::memory_order_acq_rel);
            if (set != nullptr)
            {
                if (set->B != B || set->M != M || set->tailParts > fMaxTail || set->headParts > fMaxHead)
                    retire(set);   // made for another sample rate
                else
                {
                    fLane[1 - fActive].set = set;
                    fFade = kWaiting;
                }
            }
        }
        // which lanes the job started now must compute: its output is played in [A + R, A + 2R)
        fLane[fActive].job = fFade != kRamping;    // the old response is gone by then
        fLane[1 - fActive].job = fFade != kNone;   // the new one, from the start of a change

        fTailPos = (fTailPos + 1) % fMaxTail;
        for (int c = 0; c < 2; ++c)
        {
            float* const win = fTailWin[c].data();
            std::memmove(win, win + M, sizeof(float) * M);
            std::memcpy(win + M, fCollect[c].data(), sizeof(float) * M);
            pffft_transform(fS->tail(), win, fTailFdl[c].data() + static_cast<size_t>(fTailPos) * 2 * M, fWork.data(),
                            PFFFT_FORWARD);
            for (int l = 0; l < 2; ++l)
                fLane[l].tailAcc[c].clear();
        }
    }

    const Setups* fS = nullptr;
    int B = kHeadBlock, M = 4096, R = 16;
    int fMaxHead = 1, fMaxTail = 1;
    Buffer fIn[2], fDry[2], fHeadWin[2], fHeadFdl[2], fTailWin[2], fCollect[2], fTailFdl[2];
    Buffer fHeadAcc, fTmp, fWork;
    Lane fLane[2];
    int fActive = 0;
    int fFade = kNone;
    int fPos = 0;
    long long fFrame = 0;
    int fHeadPos = 0, fTailPos = 0;
    std::atomic<IRSet*> fPending { nullptr }, fRetired { nullptr };
    IRSet* fToRetire = nullptr;
};

}   // namespace PadifierConv

#endif
