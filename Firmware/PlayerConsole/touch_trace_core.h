#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>

// Decoded pre-transform coordinates, not a raw register frame. No extra I2C read.
struct TouchTraceSample {
    uint32_t atMs = 0, gapUs = 0, readUs = 0, generation = 0;
    uint32_t pendingAgeMs = 0, drawUs = 0, drawLagMs = 0, fallbackUs = 0, drawAtMs = 0;
    int16_t rawX = 0, rawY = 0, screenX = 0, screenY = 0;
    int16_t localX = 0, localY = 0, originX = 0, originY = 0;
    uint16_t id = 0, points = 0, drawn = 0, fallbackCount = 0;
    int8_t result = 0;
    uint8_t flags = 0, recognizerReady = 0;
};
// flags: 1 raw present; 2 valid sample; 4 pressed UI state; 8 canvas exists;
// 16 active canvas contact; 32 newest point connects; 64 invalid coordinates.
struct TouchTraceStats {
    uint32_t session = 0, startedMs = 0, endedMs = 0, samples = 0;
    uint32_t errors = 0, releases = 0, dropped = 0;
    uint32_t maxGapUs = 0, maxReadUs = 0, maxDrawUs = 0, maxDrawLagMs = 0;
    uint8_t queued = 0, reason = 0;
    bool active = false;
};
class TouchTraceCore {
public:
    static constexpr uint32_t kWindowMs = 30000;
    static constexpr uint8_t kCapacity = 8;
    uint32_t start(uint32_t nowMs) {
        uint32_t session = stats_.session + 1; if (!session) ++session;
        stats_ = {}; stats_.session = session; stats_.startedMs = nowMs; stats_.active = true;
        head_ = tail_ = count_ = 0; lastReadUs_ = 0; haveRead_ = false;
        return session;
    }
    void stop(uint32_t nowMs, uint8_t reason = 1) {
        if (!stats_.active) return;
        stats_.active = false; stats_.endedMs = nowMs; stats_.reason = reason;
    }
    void expire(uint32_t nowMs) {
        if (stats_.active && static_cast<int32_t>(nowMs - stats_.startedMs) >= static_cast<int32_t>(kWindowMs)) stop(nowMs, 2);
    }
    uint32_t token(uint32_t nowMs) {
        if (static_cast<int32_t>(nowMs - stats_.startedMs) < 0) return 0;
        expire(nowMs); return stats_.active ? stats_.session : 0;
    }
    void record(TouchTraceSample sample, uint32_t token, uint32_t readStartedUs) {
        if (!stats_.active || token != stats_.session) return;
        expire(sample.atMs);
        if (!stats_.active) return;
        sample.id = static_cast<uint16_t>(++stats_.samples);
        sample.gapUs = haveRead_ ? readStartedUs - lastReadUs_ : 0;
        haveRead_ = true; lastReadUs_ = readStartedUs;
        if (!(sample.flags & 2)) ++stats_.errors;
        if (sample.result == 0) ++stats_.releases;
        maximum(stats_.maxGapUs, sample.gapUs); maximum(stats_.maxReadUs, sample.readUs);
        maximum(stats_.maxDrawUs, sample.drawUs); maximum(stats_.maxDrawLagMs, sample.drawLagMs);
        if (count_ == kCapacity) { ++stats_.dropped; return; }
        records_[tail_] = sample; tail_ = (tail_ + 1) % kCapacity; ++count_;
    }
    bool pop(TouchTraceSample &out) {
        if (!count_) return false;
        out = records_[head_]; head_ = (head_ + 1) % kCapacity; --count_; return true;
    }
    TouchTraceStats snapshot(uint32_t nowMs) { expire(nowMs); auto copy = stats_; copy.queued = count_; return copy; }
private:
    static void maximum(uint32_t &a, uint32_t b) { if (b > a) a = b; }
    TouchTraceSample records_[kCapacity]{};
    TouchTraceStats stats_{};
    uint32_t lastReadUs_ = 0;
    uint8_t head_ = 0, tail_ = 0, count_ = 0;
    bool haveRead_ = false;
};

// Source.snapshot/pop perform their own short lock/copy. Sink I/O is always outside it.
// At most one whole <=63-byte line per tick. A delayed/blocked USB drops trace only.
class TouchTraceOutput {
public:
    static constexpr size_t kMaxLine = 63;
    bool closed() const { return closed_; }
    template<class Sink, class Source> void tick(Sink &sink, Source &source, uint32_t nowMs) {
        const auto status = source.snapshot(nowMs);
        if (!status.session) return;
        if (session_ != status.session) {
            session_ = status.session; ready_ = false; closed_ = false; row_ = 6;
            summaryRow_ = 3; txDropped_ = 0; lastSummaryMs_ = nowMs; finalSummary_ = false;
        }
        if (closed_) return;
        if (!status.active && static_cast<uint32_t>(nowMs - status.endedMs) >= 2000) {
            closed_ = true; return; // Do not keep logging forever to an absent USB host.
        }
        char line[kMaxLine + 1]{};
        int size = 0;
        if (!ready_) {
            size = std::snprintf(line, sizeof(line), "GP_TOUCH READY sid=%lu ms=%lu window=30000\n",
                ul(session_), ul(status.startedMs));
            if (write(sink, line, size)) ready_ = true;
            return;
        }
        if (row_ < 6 && static_cast<uint32_t>(nowMs - acquiredMs_) >= 250) { ++txDropped_; row_ = 6; }
        if (summaryRow_ == 3 && row_ == 6 &&
            (static_cast<uint32_t>(nowMs - lastSummaryMs_) >= 1000 ||
             (!status.active && !status.queued && !finalSummary_))) {
            summary_ = status; summaryRow_ = 0; lastSummaryMs_ = nowMs;
            finalSummary_ = !status.active && !status.queued;
        }
        if (summaryRow_ < 3) {
            if (summaryRow_ == 0) size = std::snprintf(line, sizeof(line), "GP_TS t=%lu n=%lu d=%lu tx=%lu\n",
                ul(nowMs), ul(summary_.samples), ul(summary_.dropped), ul(txDropped_));
            else if (summaryRow_ == 1) size = std::snprintf(line, sizeof(line), "GP_TS e=%lu u=%lu g=%lu r=%lu\n",
                ul(summary_.errors), ul(summary_.releases), ul(summary_.maxGapUs), ul(summary_.maxReadUs));
            else size = std::snprintf(line, sizeof(line), "GP_TS du=%lu lag=%lu\n",
                ul(summary_.maxDrawUs), ul(summary_.maxDrawLagMs));
            if (write(sink, line, size)) ++summaryRow_;
            return;
        }
        if (row_ == 6) {
            if (source.pop(sample_)) { row_ = 0; acquiredMs_ = nowMs; }
            else if (!status.active && finalSummary_) {
                size = std::snprintf(line, sizeof(line), "GP_TOUCH END sid=%lu why=%u ms=%lu\n",
                    ul(session_), status.reason, ul(status.endedMs));
                if (write(sink, line, size)) closed_ = true;
                return;
            } else return;
        }
        const auto &s = sample_;
        switch (row_) {
        case 0: size = std::snprintf(line, sizeof(line), "GP_T id=%u ms=%lu dt=%lu rd=%lu r=%d\n",
            s.id, ul(s.atMs), ul(s.gapUs), ul(s.readUs), s.result); break;
        case 1: size = std::snprintf(line, sizeof(line), "GP_T id=%u raw=%d,%d scr=%d,%d f=%u\n",
            s.id, s.rawX, s.rawY, s.screenX, s.screenY, s.flags); break;
        case 2: size = std::snprintf(line, sizeof(line), "GP_T id=%u loc=%d,%d org=%d,%d\n",
            s.id, s.localX, s.localY, s.originX, s.originY); break;
        case 3: size = std::snprintf(line, sizeof(line), "GP_T id=%u g=%lu n=%u d=%u age=%lu\n",
            s.id, ul(s.generation), s.points, s.drawn, ul(s.pendingAgeMs)); break;
        case 4: size = std::snprintf(line, sizeof(line), "GP_T id=%u du=%lu lag=%lu rec=%u fb=%u\n",
            s.id, ul(s.drawUs), ul(s.drawLagMs), s.recognizerReady, s.fallbackCount); break;
        case 5: size = std::snprintf(line, sizeof(line), "GP_T id=%u da=%lu fu=%lu\n", s.id, ul(s.drawAtMs), ul(s.fallbackUs)); break;
        }
        if (write(sink, line, size)) ++row_;
    }
private:
    static unsigned long ul(uint32_t value) { return static_cast<unsigned long>(value); }
    template<class Sink> bool write(Sink &sink, const char *line, int size) {
        if (size <= 0 || static_cast<size_t>(size) > kMaxLine || sink.availableForWrite() < size) return false;
        const size_t written = sink.write(reinterpret_cast<const uint8_t *>(line), static_cast<size_t>(size));
        if (written != static_cast<size_t>(size)) ++txDropped_;
        return true; // never retry/append the remainder of a partially accepted line
    }
    TouchTraceSample sample_{};
    TouchTraceStats summary_{};
    uint32_t session_ = 0, acquiredMs_ = 0, lastSummaryMs_ = 0, txDropped_ = 0;
    uint8_t row_ = 6, summaryRow_ = 3;
    bool ready_ = false, closed_ = false, finalSummary_ = false;
};
static_assert(sizeof(TouchTraceCore) + sizeof(TouchTraceOutput) <= 800, "keep trace state below the internal RAM budget");
