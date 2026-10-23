#pragma once

// The GUI readback's hand-off between the present hook and the thread that measures it (wheel_probe.hpp).
//
// The present hook records a reading's copy into a slot of a small ring (SlotRing), notes the timeline value
// it completes at, and once the shared timeline shows it done hands the slot to the measuring thread
// (Worker::hand). From then until its measurement returns, the slot is the worker's (Worker::busy): the ring
// offers it to no new copy, and the host buffer behind it is neither resized nor freed. The worker measures
// one job at a time, in the order they were handed, and keeps each result for the present hook, which takes
// them at its next poll (Worker::take). stop() returns once the thread is gone: the job being measured
// finishes, the ones still waiting are dropped, and nothing reads a slot after it.
//
// Pure: no Vulkan. Unit tested (tests/vkcore/readback_worker_tests.cpp).

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace evr::vkcore::readback {

inline constexpr std::uint32_t kNoSlot = UINT32_MAX;

// The present hook's side of the slots: which one a new reading goes to, which are in flight on the GPU.
template <std::uint32_t N>
class SlotRing {
public:
    static_assert(N > 0 && N <= 32, "one bit of a busy mask a slot");
    enum class State : std::uint8_t { Idle, Recorded, Submitted };

    // The slot a new reading goes to: the first idle one the worker does not hold (`busy`: one bit a slot). A
    // slot recorded but never marked submitted or cancelled is idle again. kNoSlot when none is free.
    std::uint32_t pick(std::uint32_t busy) {
        std::uint32_t slot = kNoSlot;
        for (std::uint32_t i = 0; i < N; ++i) {
            if (states_[i] == State::Recorded) {
                states_[i] = State::Idle;
            }
            if (slot == kNoSlot && states_[i] == State::Idle && (busy & (1u << i)) == 0) {
                slot = i;
            }
        }
        return slot;
    }
    void recorded(std::uint32_t slot) { states_[slot] = State::Recorded; }
    // The recorded readings were submitted and complete at `value`.
    void submitted(std::uint64_t value) {
        for (std::uint32_t i = 0; i < N; ++i) {
            if (states_[i] == State::Recorded) {
                states_[i] = State::Submitted;
                values_[i] = value;
            }
        }
    }
    // The recorded readings were not submitted after all.
    void cancel() {
        for (State& s : states_) {
            if (s == State::Recorded) {
                s = State::Idle;
            }
        }
    }
    // The slots whose copies are done (`completedValue` reached), oldest first, into `out`; they are marked
    // idle (the caller hands them to the worker, which then holds them). Returns how many.
    std::uint32_t completed(std::uint64_t completedValue, std::array<std::uint32_t, N>& out) {
        std::uint32_t count = 0;
        for (std::uint32_t i = 0; i < N; ++i) {
            if (states_[i] == State::Submitted && values_[i] <= completedValue) {
                out[count++] = i;
            }
        }
        for (std::uint32_t a = 1; a < count; ++a) { // insertion sort by value: at most N entries
            for (std::uint32_t b = a; b > 0 && values_[out[b]] < values_[out[b - 1]]; --b) {
                std::swap(out[b], out[b - 1]);
            }
        }
        for (std::uint32_t k = 0; k < count; ++k) {
            states_[out[k]] = State::Idle;
        }
        return count;
    }
    // A copy is in flight or the worker holds a slot: the buffer behind the slots must stay.
    [[nodiscard]] bool inUse(std::uint32_t busy) const {
        for (std::uint32_t i = 0; i < N; ++i) {
            if (states_[i] == State::Submitted || (busy & (1u << i)) != 0) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] State state(std::uint32_t slot) const { return states_[slot]; }
    void reset() {
        states_ = {};
        values_ = {};
    }

private:
    std::array<State, N> states_{};
    std::array<std::uint64_t, N> values_{};
};

// The measuring thread. `Job` has a `std::uint32_t slot` (kNoSlot for a job without a reading, e.g. a log
// request); `Result` is what the present hook takes back.
template <class Job, class Result>
class Worker {
public:
    using Measure = std::function<Result(const Job&)>;

    Worker() = default;
    explicit Worker(Measure measure) : measure_(std::move(measure)) {}
    ~Worker() { stop(); }
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    // Before the first hand().
    void setMeasure(Measure measure) { measure_ = std::move(measure); }

    // Present hook: `job` goes to the thread (started on first use, and again after stop()). Its slot is the
    // worker's from here until its measurement returns. False when it could not be handed (the thread could
    // not start, or no memory for the queue): the job is dropped and its slot stays free. Nothing throws
    // into the game's present call.
    bool hand(const Job& job) noexcept {
        try {
            {
                std::lock_guard lock(lock_);
                if (!thread_.joinable()) {
                    stop_ = false;
                    thread_ = std::thread([this] { run(); });
                }
                jobs_.push_back(job);
                // Under the lock, once queued: the thread cannot take the job (and free the slot) before.
                if (job.slot != kNoSlot) {
                    busy_.fetch_or(1u << job.slot, std::memory_order_relaxed);
                }
            }
            wake_.notify_one();
            return true;
        } catch (...) {
            return false;
        }
    }

    // Any thread: the slots the worker holds, one bit each.
    [[nodiscard]] std::uint32_t busy() const { return busy_.load(std::memory_order_acquire); }

    // Present hook: the results measured since the last call, in the order their jobs were handed. `out` is
    // cleared first; the two vectors are swapped, so neither side allocates once they have grown.
    void take(std::vector<Result>& out) {
        out.clear();
        std::lock_guard lock(lock_);
        std::swap(out, results_);
    }

    // The thread is gone on return: the job being measured finished, the ones waiting are dropped (and their
    // slots free), and nothing reads a slot until the next hand(). No-op without a thread.
    void stop() {
        if (!thread_.joinable()) { // only the present hook's calls change it: no lock (see abandon())
            return;
        }
        {
            std::lock_guard lock(lock_);
            stop_ = true;
        }
        wake_.notify_all();
        thread_.join();
        std::lock_guard lock(lock_);
        jobs_.clear();
        results_.clear();
        stop_ = false;
        busy_.store(0, std::memory_order_release);
        idle_.notify_all();
    }

    // Process exit: every other thread is already gone (and may have died holding the lock), so nothing is
    // locked or waited for; the thread object lets go of it.
    void abandon() {
        if (thread_.joinable()) {
            thread_.detach();
        }
    }

    // Blocks until every job handed so far is measured (tests, and nothing on the present hook).
    void drain() {
        std::unique_lock lock(lock_);
        idle_.wait(lock, [this] { return jobs_.empty() && !measuring_; });
    }

private:
    void run() {
        std::unique_lock lock(lock_);
        for (;;) {
            wake_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) {
                break;
            }
            const Job job = jobs_.front();
            jobs_.pop_front();
            measuring_ = true;
            lock.unlock();
            Result result{};
            try {
                result = measure_(job);
            } catch (...) {
                // A measurement that throws (no memory for its grid) counts as one that found nothing; the
                // thread and the slot go on.
                result = Result{};
            }
            lock.lock();
            measuring_ = false;
            results_.push_back(std::move(result));
            if (job.slot != kNoSlot) {
                // Released only once the measurement no longer reads the slot.
                busy_.fetch_and(~(1u << job.slot), std::memory_order_release);
            }
            if (jobs_.empty()) {
                idle_.notify_all();
            }
        }
        measuring_ = false;
        idle_.notify_all();
    }

    Measure measure_;
    std::mutex lock_;
    std::condition_variable wake_; // a job or stop
    std::condition_variable idle_; // every job measured
    std::deque<Job> jobs_;
    std::vector<Result> results_;
    bool stop_ = false;
    bool measuring_ = false;
    std::atomic<std::uint32_t> busy_{0};
    std::thread thread_;
};

} // namespace evr::vkcore::readback
