// Stand-in for Eglantine's video_core/textures/workers.h when astc.cpp is built alone (astc_decode.cpp):
// queued work runs on a few std::threads at WaitForRequests.
#pragma once
#include <algorithm>
#include <atomic>
#include <functional>
#include <thread>
#include <vector>

namespace Common {
struct ThreadWorker {
    std::vector<std::function<void()>> work;
    template <class F> void QueueWork(F&& f) { work.emplace_back(std::forward<F>(f)); }
    void WaitForRequests() {
        std::atomic<size_t> next{0};
        std::vector<std::thread> threads(std::max(1u, std::thread::hardware_concurrency()));
        for (auto& t : threads)
            t = std::thread([&] { for (size_t i; (i = next++) < work.size();) work[i](); });
        for (auto& t : threads) t.join();
        work.clear();
    }
};
} // namespace Common

namespace Tegra::Texture {
inline Common::ThreadWorker& GetThreadWorkers() { static Common::ThreadWorker w; return w; }
}
