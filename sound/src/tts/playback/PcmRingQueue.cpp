#include "tts/playback/PcmRingQueue.hpp"

namespace hecquin::tts::playback {

void PcmRingQueue::push(const std::int16_t* data, std::size_t n_samples) {
    if (n_samples == 0) return;
    std::lock_guard<std::mutex> lock(mu_);
    queue_.insert(queue_.end(), data, data + n_samples);
}

void PcmRingQueue::pop_into(std::uint8_t* dst, int byte_len) {
    if (byte_len <= 0) return;
    auto* out = reinterpret_cast<std::int16_t*>(dst);
    const int requested =
        static_cast<int>(byte_len / sizeof(std::int16_t));

    bool became_drained = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        int filled = 0;
        while (filled < requested && !queue_.empty()) {
            out[filled++] = queue_.front();
            queue_.pop_front();
        }
        for (int i = filled; i < requested; ++i) out[i] = 0;
        if (eof_ && queue_.empty()) {
            became_drained = true;
        }
    }
    if (became_drained) {
        drained_cv_.notify_all();
    }
}

void PcmRingQueue::mark_eof() {
    bool already_drained = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        eof_ = true;
        already_drained = queue_.empty();
    }
    if (already_drained) {
        drained_cv_.notify_all();
    }
}

void PcmRingQueue::wait_until_drained() {
    std::unique_lock<std::mutex> lock(mu_);
    drained_cv_.wait(lock, [this] {
        return eof_ && queue_.empty();
    });
}

std::size_t PcmRingQueue::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.size();
}

bool PcmRingQueue::drained() const {
    std::lock_guard<std::mutex> lock(mu_);
    return eof_ && queue_.empty();
}

} // namespace hecquin::tts::playback
