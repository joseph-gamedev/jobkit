#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

#ifndef JOBSYS_TELEMETRY
    #define JOBSYS_TELEMETRY 0
#endif

namespace core
{
    class JobSystem
    {
    public:
        enum class StopMode : uint8_t
        {
            Drain,        // finish queued work then stop
            CancelPending // drop queued work, finish only in-flight
        };

        struct Config
        {
            uint32_t workerThreads = 0; // 0 = hardware_concurrency (fallback to 1)
        };

        struct Stats
        {
            uint32_t workerCount = 0;

            uint64_t queued = 0;
            uint64_t inFlight = 0;

            uint64_t submitted = 0;
            uint64_t completed = 0;
        };

#if JOBSYS_TELEMETRY
        struct Diagnostics
        {
            struct Worker
            {
                uint32_t workerIndex = 0;
                std::thread::id osThreadId{};
                bool running = false;

                uint64_t runningTaskId = 0;
                const char* runningLabel = nullptr;
            };

            Stats stats;

            std::vector<Worker> workers;

            struct QueuedTask
            {
                uint64_t id = 0;
                const char* label = nullptr;
            };
            std::vector<QueuedTask> queuedTasks;
        };
#endif

    public:
        explicit JobSystem(const Config& cfg = {});
        ~JobSystem();

        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        // Returns false if the system is stopping or stopped.
        bool Submit(std::function<void()> task);

        // Telemetry-friendly submission. Label is ignored if JOBSYS_TELEMETRY == 0.
        bool SubmitLabeled(const char* label, std::function<void()> task);

        void WaitIdle();

        void Stop(StopMode mode = StopMode::Drain);

        Stats GetStats() const;

#if JOBSYS_TELEMETRY
        Diagnostics GetDiagnostics() const;
#endif

    private:
        struct TaskItem
        {
            std::function<void()> fn;

#if JOBSYS_TELEMETRY
            uint64_t id = 0;
            const char* label = nullptr;
#endif
        };

        void WorkerLoop(std::stop_token st, uint32_t workerIndex);

    private:
        Config m_cfg{};

        mutable std::mutex m_mtx;
        std::condition_variable m_cvWork;
        std::condition_variable m_cvIdle;

        std::deque<TaskItem> m_queue;

        std::atomic<bool> m_accepting{true};

        std::atomic<uint64_t> m_inFlight{0};
        std::atomic<uint64_t> m_submitted{0};
        std::atomic<uint64_t> m_completed{0};

        std::vector<std::jthread> m_workers;

#if JOBSYS_TELEMETRY
        std::atomic<uint64_t> m_nextTaskId{1};

        struct alignas(64) WorkerTelemetry
        {
            std::thread::id osThreadId{};
            std::atomic<uint64_t> runningTaskId{0};
            std::atomic<const char*> runningLabel{nullptr};
            std::atomic<bool> running{false};
        };
        std::vector<WorkerTelemetry> m_workerTel;
#endif
    };
} // namespace core

