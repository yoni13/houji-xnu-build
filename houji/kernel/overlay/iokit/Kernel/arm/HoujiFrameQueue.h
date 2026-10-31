#ifndef HOUJI_FRAME_QUEUE_H
#define HOUJI_FRAME_QUEUE_H
#include <stdint.h>

/* State only. The caller holds its display mutex and owns every queued
 * surface reference until finish() succeeds. No GPU or panel fences here. */
template <typename Frame, unsigned Capacity> class HoujiFrameQueue {
    Frame frames_[Capacity]{};
    unsigned head_ = 0, count_ = 0;
    uint32_t retired_ = 0;
    bool have_retired_ = false;
public:
    unsigned count() const { return count_; }
    bool full() const { return count_ == Capacity; }
    const Frame *front() const { return count_ ? &frames_[head_] : nullptr; }
    uint32_t latest() const {
        return count_ ? frames_[(head_ + count_ - 1) % Capacity].id : retired_;
    }
    bool contains(uint32_t id) const {
        for (unsigned n = 0; n < count_; ++n)
            if (frames_[(head_ + n) % Capacity].id == id) return true;
        return false;
    }
    bool push(const Frame &frame) {
        if (full() || contains(frame.id)) return false;
        frames_[(head_ + count_) % Capacity] = frame;
        ++count_;
        return true;
    }
    bool complete(uint32_t id) const {
        /* Check the actual queue first: independent clients can finish their
         * SwapBegin/SwapEnd calls out of allocation order. */
        if (contains(id)) return false;
        return have_retired_ && (int32_t)(id - retired_) <= 0;
    }
    bool finish(uint32_t id) {
        if (!count_ || frames_[head_].id != id) return false;
        frames_[head_] = Frame{};
        head_ = (head_ + 1) % Capacity;
        --count_;
        if (!have_retired_ || (int32_t)(id - retired_) > 0) retired_ = id;
        have_retired_ = true;
        return true;
    }
};
#endif
