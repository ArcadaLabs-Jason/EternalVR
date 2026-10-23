#include "vkcore/readback_worker.hpp"

#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

using namespace evr::vkcore::readback;
using namespace std::chrono_literals;

namespace {

struct Job {
    std::uint32_t slot = kNoSlot;
    std::uint64_t number = 0;
};

struct Result {
    std::uint64_t number = 0;
    std::thread::id thread;
};

// A gate a measurement waits at until the test opens it (or `timeout` passes).
class Gate {
public:
    void open() {
        {
            std::lock_guard lock(lock_);
            open_ = true;
        }
        cv_.notify_all();
    }
    bool wait(std::chrono::milliseconds timeout) {
        std::unique_lock lock(lock_);
        return cv_.wait_for(lock, timeout, [this] { return open_; });
    }

private:
    std::mutex lock_;
    std::condition_variable cv_;
    bool open_ = false;
};

// Waits without sleeping (a sleep on Windows can take a scheduler tick, longer than a measurement here).
void spin(std::chrono::microseconds d) {
    const auto until = std::chrono::steady_clock::now() + d;
    while (std::chrono::steady_clock::now() < until) {
    }
}

template <class Predicate>
bool eventually(Predicate p, std::chrono::milliseconds timeout = 5000ms) {
    const auto until = std::chrono::steady_clock::now() + timeout;
    while (!p()) {
        if (std::chrono::steady_clock::now() > until) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

} // namespace

TEST_CASE("a handed reading is measured on the worker's thread; the present hook does not wait for it") {
    Gate gate;
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    Worker<Job, Result> worker([&](const Job& job) {
        started = true;
        gate.wait(2000ms); // a slow measurement: as long as the gate stays shut
        finished = true;
        return Result{job.number, std::this_thread::get_id()};
    });
    const auto before = std::chrono::steady_clock::now();
    worker.hand(Job{0, 1});
    const auto handTook = std::chrono::steady_clock::now() - before;
    // hand() came back while the measurement still runs (it waits up to 2 s at the gate).
    CHECK_FALSE(finished.load());
    CHECK(handTook < 1000ms);
    CHECK(worker.busy() == 1u);
    CHECK(eventually([&] { return started.load(); }));
    std::vector<Result> results;
    worker.take(results);
    CHECK(results.empty()); // nothing measured yet
    gate.open();
    worker.drain();
    CHECK(finished.load());
    CHECK(worker.busy() == 0u);
    worker.take(results);
    REQUIRE(results.size() == 1);
    CHECK(results[0].number == 1);
    CHECK(results[0].thread != std::this_thread::get_id());
}

TEST_CASE("a slot the worker holds is offered to no new reading until its measurement returns") {
    // Slots of bytes the "GPU" writes and the worker reads. Each measurement checks that its slot keeps the
    // reading it was handed with, from start to end; a slot handed out again while measured is overwritten.
    constexpr std::uint32_t kSlots = 3;
    std::array<std::atomic<std::uint64_t>, kSlots> bytes{};
    std::atomic<std::uint64_t> torn{0};
    std::atomic<std::uint64_t> measured{0};
    Worker<Job, Result> worker([&](const Job& job) {
        const std::uint64_t first = bytes[job.slot].load();
        std::this_thread::sleep_for(2ms); // reading and measuring the grid: slower than a present
        const std::uint64_t last = bytes[job.slot].load();
        if (first != job.number || last != job.number) {
            ++torn;
        }
        ++measured;
        return Result{job.number, std::this_thread::get_id()};
    });
    SlotRing<kSlots> ring;
    std::uint64_t value = 0;
    std::uint64_t handed = 0;
    std::uint64_t noSlot = 0;
    std::array<std::uint32_t, kSlots> done{};
    // Presents: each records a reading into a free slot (the copy writes it), the copy completes at once,
    // and the next present's poll hands it to the worker.
    const auto until = std::chrono::steady_clock::now() + 20s;
    while (handed < 60 && std::chrono::steady_clock::now() < until) {
        const std::uint32_t n = ring.completed(value, done);
        for (std::uint32_t k = 0; k < n; ++k) {
            worker.hand(Job{done[k], bytes[done[k]].load()});
            ++handed;
        }
        const std::uint32_t slot = ring.pick(worker.busy());
        if (slot == kNoSlot) {
            ++noSlot;
        } else {
            bytes[slot].store(++value + 1000); // the copy into the slot
            ring.recorded(slot);
            ring.submitted(value);
            bytes[slot].store(value);
        }
        spin(100us); // a present: far quicker than a measurement
    }
    worker.drain();
    CHECK(torn.load() == 0);
    CHECK(measured.load() == handed);
    CHECK(handed >= 60);
    CHECK(noSlot > 0); // the worker was slower than the presents: slots were held
}

TEST_CASE("every slot is busy while the worker holds it, and free again once measured") {
    Gate gate;
    Worker<Job, Result> worker([&](const Job& job) {
        gate.wait(2000ms);
        return Result{job.number, std::this_thread::get_id()};
    });
    SlotRing<3> ring;
    for (std::uint64_t v = 1; v <= 3; ++v) {
        const std::uint32_t slot = ring.pick(worker.busy());
        REQUIRE(slot != kNoSlot);
        ring.recorded(slot);
        ring.submitted(v);
    }
    CHECK(ring.pick(worker.busy()) == kNoSlot); // all three in flight
    std::array<std::uint32_t, 3> done{};
    REQUIRE(ring.completed(3, done) == 3);
    CHECK(done == std::array<std::uint32_t, 3>{0, 1, 2});
    for (const std::uint32_t slot : done) {
        worker.hand(Job{slot, slot});
    }
    // Done on the GPU and idle in the ring, but the worker holds them: none is offered, the buffer stays.
    CHECK(worker.busy() == 7u);
    CHECK(ring.pick(worker.busy()) == kNoSlot);
    CHECK(ring.inUse(worker.busy()));
    gate.open();
    worker.drain();
    CHECK(worker.busy() == 0u);
    CHECK_FALSE(ring.inUse(worker.busy()));
    CHECK(ring.pick(worker.busy()) == 0);
}

TEST_CASE("a recorded reading not submitted is idle again, a submitted one waits for its value") {
    SlotRing<3> ring;
    const std::uint32_t a = ring.pick(0);
    ring.recorded(a);
    // Never marked submitted or cancelled (its command buffer was dropped): the next pick takes it again.
    CHECK(ring.pick(0) == a);
    ring.recorded(a);
    ring.cancel();
    CHECK(ring.state(a) == SlotRing<3>::State::Idle);
    ring.recorded(a);
    ring.submitted(10);
    CHECK(ring.inUse(0));
    std::array<std::uint32_t, 3> done{};
    CHECK(ring.completed(9, done) == 0);
    CHECK(ring.pick(0) != a);
    CHECK(ring.completed(10, done) == 1);
    CHECK(done[0] == a);
    CHECK_FALSE(ring.inUse(0));
    CHECK(ring.inUse(1u << a)); // handed to the worker: still in use
}

TEST_CASE("completed readings come oldest first, and their results in the order they were handed") {
    SlotRing<3> ring;
    std::array<std::uint32_t, 3> order{};
    for (std::uint64_t v : {30, 10, 20}) {
        const std::uint32_t slot = ring.pick(0);
        ring.recorded(slot);
        ring.submitted(v);
    }
    REQUIRE(ring.completed(30, order) == 3);
    CHECK(order == std::array<std::uint32_t, 3>{1, 2, 0});

    // Older readings take longer to measure than newer ones: the results still come in order, so the
    // newest published is the newest reading.
    std::mutex published;
    std::uint64_t latest = 0;
    std::uint64_t backwards = 0;
    Worker<Job, Result> worker([&](const Job& job) {
        std::this_thread::sleep_for(std::chrono::microseconds(50 * (40 - job.number)));
        std::lock_guard lock(published);
        backwards += job.number < latest ? 1 : 0;
        latest = job.number;
        return Result{job.number, std::this_thread::get_id()};
    });
    for (std::uint64_t n = 1; n <= 40; ++n) {
        worker.hand(Job{kNoSlot, n});
    }
    worker.drain();
    std::vector<Result> results;
    worker.take(results);
    REQUIRE(results.size() == 40);
    for (std::size_t i = 0; i < results.size(); ++i) {
        CHECK(results[i].number == i + 1);
    }
    CHECK(backwards == 0);
    CHECK(latest == 40);
}

TEST_CASE("stop() returns once the measurement under way is done; the waiting ones are dropped") {
    Gate gate;
    std::atomic<bool> started{false};
    std::atomic<std::uint64_t> finished{0};
    Worker<Job, Result> worker([&](const Job& job) {
        started = true;
        gate.wait(150ms); // the gate stays shut: each measurement takes 150 ms
        ++finished;
        return Result{job.number, std::this_thread::get_id()};
    });
    worker.hand(Job{0, 1});
    worker.hand(Job{1, 2});
    worker.hand(Job{2, 3});
    REQUIRE(eventually([&] { return started.load(); }));
    worker.stop();
    // The first measurement finished before stop() came back; nothing reads a slot from here on.
    CHECK(finished.load() == 1);
    CHECK(worker.busy() == 0u);
    std::vector<Result> results;
    worker.take(results);
    CHECK(results.empty());
    std::this_thread::sleep_for(400ms);
    CHECK(finished.load() == 1); // the two waiting jobs were dropped, not measured later

    // A later hand() starts the thread again (a new GUI target after a swapchain change).
    gate.open();
    worker.hand(Job{0, 4});
    worker.drain();
    worker.take(results);
    REQUIRE(results.size() == 1);
    CHECK(results[0].number == 4);
    worker.stop();
    worker.stop(); // twice is fine
}

TEST_CASE("results are taken once, and a job without a slot holds none") {
    Worker<Job, Result> worker([](const Job& job) { return Result{job.number, std::this_thread::get_id()}; });
    worker.hand(Job{kNoSlot, 7});
    CHECK(worker.busy() == 0u);
    worker.drain();
    std::vector<Result> results;
    worker.take(results);
    REQUIRE(results.size() == 1);
    CHECK(results[0].number == 7);
    worker.take(results);
    CHECK(results.empty());
}

TEST_CASE("a measurement that throws frees its slot, and the thread goes on with the next one") {
    Worker<Job, Result> worker([](const Job& job) {
        if (job.number == 1) {
            throw std::bad_alloc();
        }
        return Result{job.number, std::this_thread::get_id()};
    });
    CHECK(worker.hand(Job{0, 1}));
    CHECK(worker.hand(Job{1, 2}));
    worker.drain();
    CHECK(worker.busy() == 0u);
    std::vector<Result> results;
    worker.take(results);
    REQUIRE(results.size() == 2);
    CHECK(results[0].number == 0); // the throwing one counts as a measurement that found nothing
    CHECK(results[1].number == 2);
}
