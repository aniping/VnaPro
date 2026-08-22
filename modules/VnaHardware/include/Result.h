#ifndef VNA_HARDWARE_RESULT_H
#define VNA_HARDWARE_RESULT_H

#include <stdexcept>
#include <utility>
#include <variant>

#include "VnaError.h"

namespace vna {

// 显式表示“值或错误”，避免跨 ABI 使用异常；误取另一分支属于宿主编程错误并抛异常。
template <typename T>
class Result final {
public:
    // 工厂函数保证 Result 不存在“既无值也无错误”的第三种状态。
    static Result success(T value)
    {
        return Result(std::move(value));
    }
    static Result failure(VnaError error)
    {
        return Result(std::move(error));
    }

    bool hasValue() const
    {
        return std::holds_alternative<T>(storage_);
    }

    // value 适用于需要继续保留结果的场景；失败分支会抛 logic_error。
    T &value()
    {
        if (!hasValue()) {
            throw std::logic_error("Result does not contain a value");
        }
        return std::get<T>(storage_);
    }

    const T &value() const
    {
        if (!hasValue()) {
            throw std::logic_error("Result does not contain a value");
        }
        return std::get<T>(storage_);
    }

    // takeValue 只移动值，不改变 variant 的分支；调用后不应再次依赖被移动对象内容。
    T &&takeValue()
    {
        if (!hasValue()) {
            throw std::logic_error("Result does not contain a value");
        }
        return std::move(std::get<T>(storage_));
    }

    // error 只能在 hasValue()==false 时访问。
    const VnaError &error() const
    {
        if (hasValue()) {
            throw std::logic_error("Result does not contain an error");
        }
        return std::get<VnaError>(storage_);
    }

private:
    explicit Result(T value) : storage_(std::move(value))
    {
    }
    explicit Result(VnaError error) : storage_(std::move(error))
    {
    }

    std::variant<T, VnaError> storage_;
};

}  // namespace vna

#endif
