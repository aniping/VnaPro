/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "joined_threads.h"

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <utility>

namespace VnaTest {

JoinedThreads::JoinedThreads(std::size_t workerCapacity, std::function<void()> beforeJoin)
    : workerCapacity_(workerCapacity), beforeJoin_(std::move(beforeJoin))
{
    threads_.reserve(workerCapacity_);
}

JoinedThreads::~JoinedThreads() noexcept
{
    try {
        Join();
    } catch (...) {
        // 等待线程结束失败时，不能安全分离仍持有测试作用域引用的工作线程。
        std::fputs("Failed to join a native test worker\n", stderr);
        std::terminate();
    }
}

void JoinedThreads::Launch(const std::function<void()> &function)
{
    if (threads_.size() >= workerCapacity_) {
        throw std::logic_error("The native test worker capacity is exhausted");
    }
    threads_.emplace_back(function);
}

void JoinedThreads::Join()
{
    if (beforeJoin_) {
        beforeJoin_();
    }
    for (std::thread &thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

}  // 命名空间 VnaTest
