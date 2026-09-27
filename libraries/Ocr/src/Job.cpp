#include <VLMS/Ocr/Ocr.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <utility>

namespace VLMS::Ocr {

struct Job::Impl {
    std::atomic<bool> cancelRequested{false};
    std::atomic<int> progress{0};

    /// Release/acquire on this one flag publishes `result` to the owning
    /// thread. Nothing else reads `result` before it is set, so no mutex is
    /// needed and a poll costs one relaxed-ish load.
    std::atomic<bool> finished{false};
    Result result;

    std::thread worker;
};

Job::Job(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

std::unique_ptr<Job> Job::start(Request request)
{
    auto impl = std::make_unique<Impl>();
    Impl* raw = impl.get();

    // `raw` outlives the thread: ~Job joins before the Impl is destroyed, and
    // Job is non-movable so the Impl cannot be relocated out from under it.
    raw->worker = std::thread([raw, request = std::move(request)]() mutable {
        Result result = recognize(request, [raw](int percent) {
            raw->progress.store(std::clamp(percent, 0, 100), std::memory_order_relaxed);
            return !raw->cancelRequested.load(std::memory_order_relaxed);
        });

        raw->result = std::move(result);
        raw->finished.store(true, std::memory_order_release);
    });

    return std::unique_ptr<Job>(new Job(std::move(impl)));
}

Job::~Job()
{
    // Cancel first, then join. Without the cancel this would block the closing
    // dialog for the rest of the page; with it, Tesseract stops at its next
    // check and the join is short.
    cancel();
    if (m_impl->worker.joinable()) {
        m_impl->worker.join();
    }
}

int Job::progressPercent() const noexcept
{
    return m_impl->progress.load(std::memory_order_relaxed);
}

bool Job::done() const noexcept
{
    return m_impl->finished.load(std::memory_order_acquire);
}

void Job::cancel() noexcept
{
    m_impl->cancelRequested.store(true, std::memory_order_relaxed);
}

Result Job::take()
{
    if (m_impl->worker.joinable()) {
        m_impl->worker.join();
    }
    return std::move(m_impl->result);
}

}  // namespace VLMS::Ocr
