/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_TESTS_JOINED_THREADS_H
#define VNAPRO_TESTS_JOINED_THREADS_H

#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

namespace vna_test {

// Owns joined workers so partial thread creation cannot leave joinable threads during unwinding.
class JoinedThreads final {
public:
    // Reserves all worker slots before starting any thread. beforeJoin releases any start barriers.
    explicit JoinedThreads(std::size_t workerCapacity, std::function<void()> beforeJoin = {});
    JoinedThreads(const JoinedThreads &) = delete;
    JoinedThreads &operator=(const JoinedThreads &) = delete;
    JoinedThreads(JoinedThreads &&) = delete;
    JoinedThreads &operator=(JoinedThreads &&) = delete;
    ~JoinedThreads() noexcept;

    // Captures remain owned by the caller and must outlive this group; a worker must not join its own group.
    void Launch(const std::function<void()> &function);
    // Releases barriers, then joins every started worker. May be called again after successful completion.
    void Join();

private:
    const std::size_t workerCapacity_;
    const std::function<void()> beforeJoin_;
    std::vector<std::thread> threads_;
};

}  // namespace vna_test
#endif  // VNAPRO_TESTS_JOINED_THREADS_H
