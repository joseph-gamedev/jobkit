#include "JobSystem.h"

#include <algorithm>

namespace core
{
    static uint32_t ResolveThreadCount(uint32_t requested)
    {
        if (requested != 0)
            return requested;

        uint32_t hc = std::thread::hardware_concurrency();
        return (hc == 0) ? 1u : hc;
    }

    JobSystem::JobSystem(const Config& cfg)
        : m_cfg(cfg)
    {
        const uint32_t n = ResolveThreadCount(m_cfg.workerThreads);

        m_workers.reserve(n);

#if JOBSYS_TELEMETRY
        m_workerTel.resize(n);
#endif

        for (uint32_t i = 0; i < n; ++i)
        {
            m_workers.emplace_back([this, i](std::stop_token st) {
                WorkerLoop(st, i);
            });
        }
    }

    JobSystem::~JobSystem()
    {
        Stop(StopMode::Drain);
    }

    bool JobSystem::Submit(std::function<void()> task)
    {
        return SubmitLabeled(nullptr, std::move(task));
    }

    bool JobSystem::SubmitLabeled(const char* label, std::function<void()> task)
    {
        if (!task)
            return false;

        if (!m_accepting.load(std::memory_order_acquire))
            return false;

        TaskItem item{};
        item.fn = std::move(task);

#if JOBSYS_TELEMETRY
        item.id = m_nextTaskId.fetch_add(1, std::memory_order_relaxed);
        item.label = label;
#else
        (void)label;
#endif

        {
            std::lock_guard<std::mutex> lock(m_mtx);
            if (!m_accepting.load(std::memory_order_relaxed))
                return false;

            m_queue.push_back(std::move(item));
            m_submitted.fetch_add(1, std::memory_order_relaxed);
        }

        m_cvWork.notify_one();
        return true;
    }

    void JobSystem::WaitIdle()
    {
        std::unique_lock<std::mutex> lock(m_mtx);
        m_cvIdle.wait(lock, [this] {
            const bool empty = m_queue.empty();
            const bool noneInFlight = (m_inFlight.load(std::memory_order_acquire) == 0);
            return empty && noneInFlight;
        });
    }

    void JobSystem::Stop(StopMode mode)
    {
        bool expected = true;
        if (!m_accepting.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
            return; // already stopping/stopped

        {
            std::lock_guard<std::mutex> lock(m_mtx);
            if (mode == StopMode::CancelPending)
                m_queue.clear();
        }

        // Ask workers to stop and wake them.
        for (auto& t : m_workers)
            t.request_stop();

        m_cvWork.notify_all();

        // If draining, wait for queue+inFlight to become idle.
        if (mode == StopMode::Drain)
            WaitIdle();
        else
        {
            // CancelPending: only wait for in-flight to finish.
            std::unique_lock<std::mutex> lock(m_mtx);
            m_cvIdle.wait(lock, [this] {
                return (m_inFlight.load(std::memory_order_acquire) == 0);
            });
        }

        // Destroy threads (joins automatically).
        m_workers.clear();

#if JOBSYS_TELEMETRY
        m_workerTel.clear();
#endif
    }

    JobSystem::Stats JobSystem::GetStats() const
    {
        Stats s{};
        s.workerCount = (uint32_t)m_workers.size();

        s.inFlight = m_inFlight.load(std::memory_order_acquire);
        s.submitted = m_submitted.load(std::memory_order_relaxed);
        s.completed = m_completed.load(std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(m_mtx);
            s.queued = (uint64_t)m_queue.size();
        }

        return s;
    }

#if JOBSYS_TELEMETRY
    JobSystem::Diagnostics JobSystem::GetDiagnostics() const
    {
        Diagnostics d{};
        d.stats = GetStats();

        const uint32_t n = (uint32_t)m_workerTel.size();
        d.workers.resize(n);

        for (uint32_t i = 0; i < n; ++i)
        {
            Diagnostics::Worker w{};
            w.workerIndex = i;
            w.osThreadId = m_workerTel[i].osThreadId;
            w.running = m_workerTel[i].running.load(std::memory_order_acquire);
            w.runningTaskId = m_workerTel[i].runningTaskId.load(std::memory_order_acquire);
            w.runningLabel = m_workerTel[i].runningLabel.load(std::memory_order_acquire);
            d.workers[i] = w;
        }

        {
            std::lock_guard<std::mutex> lock(m_mtx);
            d.queuedTasks.reserve(m_queue.size());
            for (const TaskItem& t : m_queue)
            {
                Diagnostics::QueuedTask qt{};
                qt.id = t.id;
                qt.label = t.label;
                d.queuedTasks.push_back(qt);
            }
        }

        return d;
    }
#endif

    void JobSystem::WorkerLoop(std::stop_token st, uint32_t workerIndex)
    {
#if JOBSYS_TELEMETRY
        if (workerIndex < m_workerTel.size())
            m_workerTel[workerIndex].osThreadId = std::this_thread::get_id();
#endif

        while (true)
        {
            TaskItem task;

            {
                std::unique_lock<std::mutex> lock(m_mtx);
                m_cvWork.wait(lock, [&] {
                    return st.stop_requested() || !m_queue.empty();
                });

                if (st.stop_requested())
                {
                    // If draining, keep working until queue empty. If not draining, Stop() may clear queue.
                    if (m_queue.empty())
                        break;
                }

                if (m_queue.empty())
                    continue;

                task = std::move(m_queue.front());
                m_queue.pop_front();
                m_inFlight.fetch_add(1, std::memory_order_acq_rel);
            }

#if JOBSYS_TELEMETRY
            if (workerIndex < m_workerTel.size())
            {
                m_workerTel[workerIndex].running.store(true, std::memory_order_release);
                m_workerTel[workerIndex].runningTaskId.store(task.id, std::memory_order_release);
                m_workerTel[workerIndex].runningLabel.store(task.label, std::memory_order_release);
            }
#endif

            // Execute outside lock.
            try
            {
                task.fn();
            }
            catch (...)
            {
                // Swallow exceptions to avoid killing worker threads.
            }

#if JOBSYS_TELEMETRY
            if (workerIndex < m_workerTel.size())
            {
                m_workerTel[workerIndex].running.store(false, std::memory_order_release);
                m_workerTel[workerIndex].runningTaskId.store(0, std::memory_order_release);
                m_workerTel[workerIndex].runningLabel.store(nullptr, std::memory_order_release);
            }
#endif

            m_completed.fetch_add(1, std::memory_order_relaxed);

            {
                std::lock_guard<std::mutex> lock(m_mtx);
                m_inFlight.fetch_sub(1, std::memory_order_acq_rel);

                // Notify waiters when system becomes idle.
                if (m_queue.empty() && m_inFlight.load(std::memory_order_acquire) == 0)
                    m_cvIdle.notify_all();
                else
                    m_cvIdle.notify_all(); // safe and simple for V1.0
            }
        }

        // On exit, notify potential WaitIdle callers.
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            if (m_queue.empty() && m_inFlight.load(std::memory_order_acquire) == 0)
                m_cvIdle.notify_all();
            else
                m_cvIdle.notify_all();
        }
    }
} // namespace core
