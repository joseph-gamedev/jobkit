#include "JobSystem.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

namespace
{
    struct TestRunner
    {
        int failures = 0;

        void Check(bool condition, const char* expr, const char* file, int line)
        {
            if (condition)
                return;

            ++failures;
            std::cerr << "FAIL " << file << ":" << line << " " << expr << "\n";
        }

        int Finish() const
        {
            if (failures == 0)
                std::cout << "All tests passed\n";
            return failures == 0 ? 0 : 1;
        }
    };
} // namespace

#define CHECK(expr) runner.Check((expr), #expr, __FILE__, __LINE__)

static void TestBasicSubmit(TestRunner& runner)
{
    core::JobSystem js;
    std::atomic<int> count{0};

    constexpr int kTasks = 100;
    for (int i = 0; i < kTasks; ++i)
    {
        CHECK(js.Submit([&count] {
            count.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    js.WaitIdle();

    CHECK(count.load(std::memory_order_relaxed) == kTasks);

    const core::JobSystem::Stats stats = js.GetStats();
    CHECK(stats.submitted == static_cast<uint64_t>(kTasks));
    CHECK(stats.completed == static_cast<uint64_t>(kTasks));
    CHECK(stats.queued == 0);
    CHECK(stats.inFlight == 0);
}

static void TestCancelPending(TestRunner& runner)
{
    core::JobSystem::Config cfg{};
    cfg.workerThreads = 1;

    core::JobSystem js(cfg);
    std::atomic<int> executed{0};
    std::mutex mtx;
    std::condition_variable cv;
    bool release = false;

    CHECK(js.Submit([&] {
        executed.fetch_add(1, std::memory_order_relaxed);
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&] { return release; });
    }));

    constexpr int kQueued = 20;
    for (int i = 0; i < kQueued; ++i)
    {
        CHECK(js.Submit([&] {
            executed.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (executed.load(std::memory_order_relaxed) == 0
        && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(executed.load(std::memory_order_relaxed) >= 1);

    std::thread stopper([&] {
        js.Stop(core::JobSystem::StopMode::CancelPending);
    });

    {
        std::lock_guard<std::mutex> lock(mtx);
        release = true;
    }
    cv.notify_all();

    stopper.join();

    CHECK(executed.load(std::memory_order_relaxed) == 1);
    CHECK(!js.Submit([] {}));
}

static void TestRejectEmpty(TestRunner& runner)
{
    core::JobSystem js;
    std::function<void()> empty;
    CHECK(!js.Submit(empty));
}

int main()
{
    TestRunner runner;

    TestBasicSubmit(runner);
    TestCancelPending(runner);
    TestRejectEmpty(runner);

    return runner.Finish();
}
