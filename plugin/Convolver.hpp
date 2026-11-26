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
 *
 * Three stages of uniformly partitioned overlap-save convolution (FFT: pffft), each with its
 * frequency-domain delay lines of the inputs:
 * - head: blocks of B = 256 samples, response samples [0, 2 M1); latency B;
 * - stage 1: blocks of M1 = 4096 (fs <= 50 kHz), 8192 (<= 100 kHz) or 16384, samples
 *   [2 M1, 2 M2) (6 partitions);
 * - stage 2: blocks of M2 = 4 M1, samples [2 M2, N).
 * A tail stage of block M: block j of the input (M samples) is complete at the end of frame
 * (j + 1) R - 1 (R = M / B frames of B); its contribution starts at sample jM + 2M, i.e. frame
 * (j + 2) R.  The work is spread over the R frames in between: the two input FFTs on the first,
 * the products over frames 0 .. R - 3, the inverse FFT of output 0 on frame R - 2 and of output 1
 * on frame R - 1.
 * Cost per sample and output, complex products: head 2 M1 / B, stage 1 6, stage 2 (N - 2 M2) / M2
 * (twice with cross); 30 s at 48 kHz: 32 + 6 + 86, instead of 32 + 350 with one tail stage.
 *
 * Change of response: the new set of spectra (built elsewhere, IRSet) is taken at a frame A that
 * starts a block of stage 2 (A = 0 mod R2).  The delay lines hold the spectra of the inputs,
 * whatever the response, so the new response has the whole past of the inputs at once.  The
 * output is the old response until frame A + R2, then the crossfade (1 - g) old + g new over
 * the R1 frames [A + R2, A + R2 + R1) (g linear from 0 to 1, M1 samples), then the new one; the
 * old set is then handed back (retired) to be freed outside the audio thread.  A job (head frame
 * or tail block) computes a response when its output reaches a frame where that response is
 * heard: jobs started from A on compute the new one, so every stage has it from A + R2 on.
 * A change is thus heard after at most 2 R2 frames + the time to build it (0.7 s at 48 kHz).
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
static const int kStageRatio = 4;    // M2 = 4 M1

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

// FFT sizes 2B, 2 M1 and 2 M2 for one sample rate; read-only once made, shared by threads
class Setups
{
public:
    Setups(const int B, const int M1)
        : fB(B), fM { M1, kStageRatio * M1 }, fHead(pffft_new_setup(2 * B, PFFFT_REAL)),
          fTail { pffft_new_setup(2 * M1, PFFFT_REAL), pffft_new_setup(2 * kStageRatio * M1, PFFFT_REAL) } {}
    ~Setups()
    {
        pffft_destroy_setup(fHead);
        pffft_destroy_setup(fTail[0]);
        pffft_destroy_setup(fTail[1]);
    }
    Setups(const Setups&) = delete;
    Setups& operator=(const Setups&) = delete;
    int B() const { return fB; }
    int M(const int stage) const { return fM[stage]; }   // stage 0: stage 1, 1: stage 2
    const PFFFT_Setup* head() const { return fHead; }
    const PFFFT_Setup* tail(const int stage) const { return fTail[stage]; }

private:
    int fB, fM[2];
    PFFFT_Setup* fHead;
    PFFFT_Setup* fTail[2];
};

// Spectra of the partitions of the responses h[input][output] (scaled for the inverse FFT).
struct IRSet
{
    int B = 0, M[2] = { 0, 0 }, length = 0, headParts = 0, tailParts[2] = { 0, 0 };
    bool cross = false;              // h[1][0] and h[0][1] present
    Buffer head[2][2];               // headParts x 2B floats, pffft order
    Buffer tail[2][2][2];            // [stage][input][output]: tailParts x 2M floats

    bool has(const int i, const int o) const { return i == o || cross; }

    // partitions of each part of a response of n samples
    static int headPartsFor(const int n, const int B, const int M1) { return (std::min(n, 2 * M1) + B - 1) / B; }
    static int tailStart(const int stage, const int M1) { return 2 * M1 * (stage == 0 ? 1 : kStageRatio); }
    static int tailPartsFor(const int stage, const int n, const int M1)
    {
        const int M = M1 * (stage == 0 ? 1 : kStageRatio);
        const int from = tailStart(stage, M1), to = stage == 0 ? std::min(n, tailStart(1, M1)) : n;
        return to > from ? (to - from + M - 1) / M : 0;
    }

    // h[i][o]: n samples from input i to output o; the cross ones (i != o) may be null
    IRSet(const Setups& s, const float* const h[2][2], const int n)
        : B(s.B()), M { s.M(0), s.M(1) }, length(n), headParts(headPartsFor(n, s.B(), s.M(0))),
          tailParts { tailPartsFor(0, n, s.M(0)), tailPartsFor(1, n, s.M(0)) },
          cross(h[1][0] != nullptr && h[0][1] != nullptr)
    {
        Buffer tmp(2 * static_cast<size_t>(M[1])), work(2 * static_cast<size_t>(M[1]));
        for (int i = 0; i < 2; ++i)
            for (int o = 0; o < 2; ++o)
            {
                if (!has(i, o))
                    continue;
                head[i][o].allocate(static_cast<size_t>(headParts) * 2 * B);
                for (int p = 0; p < headParts; ++p)
                    partition(s.head(), h[i][o], n, p * B, B, head[i][o].data() + static_cast<size_t>(p) * 2 * B, tmp, work);
                for (int k = 0; k < 2; ++k)
                {
                    tail[k][i][o].allocate(static_cast<size_t>(tailParts[k]) * 2 * M[k]);
                    for (int p = 0; p < tailParts[k]; ++p)
                        partition(s.tail(k), h[i][o], n, tailStart(k, M[0]) + p * M[k], M[k],
                                  tail[k][i][o].data() + static_cast<size_t>(p) * 2 * M[k], tmp, work);
                }
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
        fMaxHead = IRSet::headPartsFor(maxLength, B, setups->M(0));
        for (int c = 0; c < 2; ++c)
        {
            fIn[c].allocate(B);
            fDry[c].allocate(B);
            fHeadWin[c].allocate(2 * B);
            fHeadFdl[c].allocate(static_cast<size_t>(fMaxHead) * 2 * B);
            for (int l = 0; l < 2; ++l)
                fHeadOut[l][c].allocate(B);
        }
        for (int k = 0; k < 2; ++k)
        {
            Stage& st = fStage[k];
            st.M = setups->M(k);
            st.R = st.M / B;
            st.maxParts = std::max(1, IRSet::tailPartsFor(k, maxLength, setups->M(0)));
            for (int c = 0; c < 2; ++c)
            {
                st.win[c].allocate(2 * st.M);
                st.collect[c].allocate(st.M);
                st.fdl[c].allocate(static_cast<size_t>(st.maxParts) * 2 * st.M);
                for (int l = 0; l < 2; ++l)
                {
                    st.acc[l][c].allocate(2 * st.M);
                    st.cur[l][c].allocate(st.M);
                    st.next[l][c].allocate(st.M);
                }
            }
        }
        R1 = fStage[0].R;
        R2 = fStage[1].R;
        fHeadAcc.allocate(2 * B);
        fTmp.allocate(2 * fStage[1].M);
        fWork.allocate(2 * fStage[1].M);
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
            for (int l = 0; l < 2; ++l)
                fHeadOut[l][c].clear();
        }
        for (Stage& st : fStage)
        {
            for (int c = 0; c < 2; ++c)
            {
                st.win[c].clear();
                st.collect[c].clear();
                st.fdl[c].clear();
                for (int l = 0; l < 2; ++l)
                {
                    st.acc[l][c].clear();
                    st.cur[l][c].clear();
                    st.next[l][c].clear();
                }
            }
            st.pos = 0;
            st.job[0] = st.job[1] = false;
        }
        fPos = 0;
        fFrame = 0;
        fHeadPos = 0;
        if (fFade)   // a change cut short: the new response stays
        {
            delete fSet[fActive];
            fSet[fActive] = nullptr;
            fActive = 1 - fActive;
        }
        fFade = false;
    }

    // Response used at once (not real-time; the previous one is freed)
    void setNow(IRSet* const set)
    {
        delete fSet[fActive];
        fSet[fActive] = set;
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
            delete fSet[l];
            fSet[l] = nullptr;
        }
        fFade = false;
    }

    bool crossfading() const { return fFade || fPending.load(std::memory_order_relaxed) != nullptr; }
    const IRSet* current() const { return fSet[fActive]; }
    int latency() const { return B; }
    int stageFrames(const int k) const { return fStage[k].R; }   // R1, R2

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
    // a tail stage: delay lines of the inputs, jobs per lane (response) and output
    struct Stage {
        int M = 0, R = 1, maxParts = 1, pos = 0;
        Buffer win[2], collect[2], fdl[2];   // per input
        Buffer acc[2][2];                    // [lane][output]: products of the running job
        Buffer cur[2][2];                    // output of the current block (M samples)
        Buffer next[2][2];                   // result of the running job, played from the next block
        bool job[2] = { false, false };      // the running job computes this lane
    };

    // is lane l heard in output frames [a, b)?
    bool heard(const int l, const long long a, const long long b) const
    {
        if (fSet[l] == nullptr)
            return false;
        if (!fFade)
            return l == fActive;
        const long long rampStart = fFadeFrame + R2, rampEnd = rampStart + R1;
        return l == fActive ? a < rampEnd : b > rampStart;
    }

    // samples [fPos, fPos + k) of output frame fFrame - 1 (being played)
    void mixOut(const int c, float* const y, const int k)
    {
        const long long frame = fFrame - 1;
        laneOut(fActive, c, frame, y, k);
        const long long rampStart = fFadeFrame + R2;
        if (!fFade || frame < rampStart)
            return;
        // ramp over the R1 frames from rampStart: g = t / M1
        float b[kHeadBlock];
        laneOut(1 - fActive, c, frame, b, k);
        const int t0 = static_cast<int>(frame - rampStart) * B + fPos;
        const float step = 1.0f / static_cast<float>(R1 * B);
        for (int i = 0; i < k; ++i)
            y[i] += (t0 + i) * step * (b[i] - y[i]);
    }

    // head and tails of lane l for output c, samples [fPos, fPos + k) of output frame `frame`
    void laneOut(const int l, const int c, const long long frame, float* const y, const int k) const
    {
        const float* h = fHeadOut[l][c].data() + fPos;
        for (int i = 0; i < k; ++i)
            y[i] = h[i];
        for (const Stage& st : fStage)
        {
            const int phase = static_cast<int>(((frame % st.R) + st.R) % st.R);
            const float* t = st.cur[l][c].data() + phase * B + fPos;
            for (int i = 0; i < k; ++i)
                y[i] += t[i];
        }
    }

    void retire(IRSet* const set)
    {
        if (set == nullptr)
            return;
        fToRetire = set;   // empty here: a change is taken only when it is (crossfadeStep)
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

    // change of response, at the start of a frame (see the head of the file)
    void crossfadeStep()
    {
        if (fFade && fFrame == fFadeFrame + R2 + R1)
        {
            fFade = false;
            retire(fSet[fActive]);
            fSet[fActive] = nullptr;
            fActive = 1 - fActive;
        }
        if (!fFade && fToRetire == nullptr && fFrame % R2 == 0)
        {
            IRSet* const set = fPending.exchange(nullptr, std::memory_order_acq_rel);
            if (set != nullptr)
            {
                if (set->B != B || set->M[0] != fStage[0].M || set->M[1] != fStage[1].M || set->headParts > fMaxHead
                    || set->tailParts[0] > fStage[0].maxParts || set->tailParts[1] > fStage[1].maxParts)
                    retire(set);   // made for another sample rate
                else
                {
                    fSet[1 - fActive] = set;
                    fFade = true;
                    fFadeFrame = fFrame;
                }
            }
        }
    }

    // a complete frame of B input samples (input frame fFrame): computes output frame fFrame
    void frame()
    {
        flushRetired();
        crossfadeStep();

        // dry path: this frame's input, played during the next one
        for (int c = 0; c < 2; ++c)
            std::memcpy(fDry[c].data(), fIn[c].data(), sizeof(float) * B);

        for (int k = 0; k < 2; ++k)
            tailFrame(k);

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
            const IRSet* const set = fSet[l];
            const bool needed = heard(l, fFrame, fFrame + 1);
            for (int c = 0; c < 2; ++c)   // output
            {
                if (!needed)
                {
                    fHeadOut[l][c].clear();
                    continue;
                }
                float* const acc = fHeadAcc.data();
                std::memset(acc, 0, sizeof(float) * 2 * B);
                for (int i = 0; i < 2; ++i)   // input
                {
                    if (!set->has(i, c))
                        continue;
                    for (int p = 0; p < set->headParts; ++p)
                    {
                        const int slot = (fHeadPos - p + fMaxHead) % fMaxHead;
                        pffft_zconvolve_accumulate(fS->head(), fHeadFdl[i].data() + static_cast<size_t>(slot) * 2 * B,
                                                   set->head[i][c].data() + static_cast<size_t>(p) * 2 * B, acc, 1.0f);
                    }
                }
                pffft_transform(fS->head(), acc, fTmp.data(), fWork.data(), PFFFT_BACKWARD);
                std::memcpy(fHeadOut[l][c].data(), fTmp.data() + B, sizeof(float) * B);
            }
        }
        ++fFrame;
    }

    // one frame of tail stage k: start of a job, share of the products, inverse FFTs, collection
    void tailFrame(const int k)
    {
        Stage& st = fStage[k];
        const int R = st.R, M = st.M;
        const int phase = static_cast<int>(fFrame % R);
        const PFFFT_Setup* const setup = fS->tail(k);
        if (phase == 0)
        {
            // the results of the previous job are played; the block just completed is transformed
            for (int l = 0; l < 2; ++l)
                for (int c = 0; c < 2; ++c)
                {
                    st.cur[l][c].swap(st.next[l][c]);
                    st.acc[l][c].clear();
                }
            for (int l = 0; l < 2; ++l)   // the output of this job is played in [fFrame + R, fFrame + 2R)
                st.job[l] = heard(l, fFrame + R, fFrame + 2 * R);
            st.pos = (st.pos + 1) % st.maxParts;
            for (int c = 0; c < 2; ++c)
            {
                float* const win = st.win[c].data();
                std::memmove(win, win + M, sizeof(float) * M);
                std::memcpy(win + M, st.collect[c].data(), sizeof(float) * M);
                pffft_transform(setup, win, st.fdl[c].data() + static_cast<size_t>(st.pos) * 2 * M, fWork.data(),
                                PFFFT_FORWARD);
            }
        }
        // products over frames 0 .. R - 3, inverse FFT of output 0 on R - 2, of output 1 on R - 1
        const int shares = R - 2;
        for (int l = 0; l < 2; ++l)
        {
            const IRSet* const set = fSet[l];
            if (!st.job[l] || set == nullptr)
                continue;
            if (phase < shares)
            {
                const int P = set->tailParts[k];
                const int p0 = phase * P / shares, p1 = (phase + 1) * P / shares;
                for (int c = 0; c < 2; ++c)   // output
                    for (int i = 0; i < 2; ++i)   // input
                    {
                        if (!set->has(i, c))
                            continue;
                        for (int p = p0; p < p1; ++p)
                        {
                            const int slot = (st.pos - p + st.maxParts) % st.maxParts;
                            pffft_zconvolve_accumulate(setup, st.fdl[i].data() + static_cast<size_t>(slot) * 2 * M,
                                                       set->tail[k][i][c].data() + static_cast<size_t>(p) * 2 * M,
                                                       st.acc[l][c].data(), 1.0f);
                        }
                    }
            }
            else
            {
                const int c = phase - shares;   // output
                pffft_transform(setup, st.acc[l][c].data(), fTmp.data(), fWork.data(), PFFFT_BACKWARD);
                std::memcpy(st.next[l][c].data(), fTmp.data() + M, sizeof(float) * M);
            }
        }
        // collect this frame for the next block
        for (int c = 0; c < 2; ++c)
            std::memcpy(st.collect[c].data() + static_cast<size_t>(phase) * B, fIn[c].data(), sizeof(float) * B);
    }

    const Setups* fS = nullptr;
    int B = kHeadBlock, R1 = 16, R2 = 64;
    int fMaxHead = 1;
    Buffer fIn[2], fDry[2], fHeadWin[2], fHeadFdl[2];
    Buffer fHeadOut[2][2];   // [lane][output]: head output of the frame being played
    Buffer fHeadAcc, fTmp, fWork;
    Stage fStage[2];
    IRSet* fSet[2] = { nullptr, nullptr };   // lanes: the response heard, and the next one
    int fActive = 0;
    bool fFade = false;
    long long fFadeFrame = 0;   // frame A where the next response was taken
    int fPos = 0;
    long long fFrame = 0;
    int fHeadPos = 0;
    std::atomic<IRSet*> fPending { nullptr }, fRetired { nullptr };
    IRSet* fToRetire = nullptr;
};

}   // namespace PadifierConv

#endif
