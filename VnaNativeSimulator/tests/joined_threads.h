/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_TESTS_JOINED_THREADS_H
#define VNAPRO_TESTS_JOINED_THREADS_H

#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

namespace VnaTest {

// 持有并等待工作线程结束，避免部分线程创建失败后在栈展开期间遗留可等待线程。
class JoinedThreads final {
public:
    // 启动线程前预留全部工作线程槽位；beforeJoin 用于释放启动屏障。
    explicit JoinedThreads(std::size_t workerCapacity, std::function<void()> beforeJoin = {});
    JoinedThreads(const JoinedThreads &) = delete;
    JoinedThreads &operator=(const JoinedThreads &) = delete;
    JoinedThreads(JoinedThreads &&) = delete;
    JoinedThreads &operator=(JoinedThreads &&) = delete;
    ~JoinedThreads() noexcept;

    // 捕获内容仍由调用者持有，生命周期必须长于本线程组；工作线程不得等待自身线程组结束。
    void Launch(const std::function<void()> &function);
    // 先释放屏障，再等待所有已启动线程结束；成功完成后允许再次调用。
    void Join();

private:
    const std::size_t workerCapacity_;
    const std::function<void()> beforeJoin_;
    std::vector<std::thread> threads_;
};

}  // 命名空间 VnaTest
#endif  // VNAPRO_TESTS_JOINED_THREADS_H
