#include "VnaLogger.h"

#include <array>
#include <exception>
#include <mutex>
#include <utility>

#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace vna::detail {
namespace {

    std::mutex activeLoggerMutex;
    // weak_ptr 不延长管理器寿命；最后一个管理器销毁时日志后端随即刷盘关闭。
    std::weak_ptr<VnaLogger> activeLogger;

    VnaError loggerError(std::string description)
    {
        VnaError error;
        error.code = VnaErrorCode::ConfigurationRejected;
        error.description = std::move(description);
        return error;
    }

    spdlog::level::level_enum backendLevel(VnaLogLevel level)
    {
        static constexpr std::array<spdlog::level::level_enum, 7> levels{
            spdlog::level::trace, spdlog::level::debug,    spdlog::level::info, spdlog::level::warn,
            spdlog::level::err,   spdlog::level::critical, spdlog::level::off};
        return levels.at(static_cast<std::size_t>(level));
    }

    VnaLogLevel driverLevel(std::uint32_t level)
    {
        switch (level) {
            case VNA_LOG_TRACE:
                return VnaLogLevel::Trace;
            case VNA_LOG_DEBUG:
                return VnaLogLevel::Debug;
            case VNA_LOG_INFO:
                return VnaLogLevel::Info;
            case VNA_LOG_WARNING:
                return VnaLogLevel::Warning;
            case VNA_LOG_ERROR:
                return VnaLogLevel::Error;
            case VNA_LOG_CRITICAL:
                return VnaLogLevel::Critical;
            default:
                return VnaLogLevel::Warning;
        }
    }

}  // namespace

class VnaLogger::Impl final {
public:
    Impl(VnaLogConfiguration selected, std::shared_ptr<spdlog::logger> target)
        : configuration(std::move(selected)), backend(std::move(target))
    {
    }

    VnaLogConfiguration configuration;
    std::shared_ptr<spdlog::logger> backend;
};

Result<std::shared_ptr<VnaLogger>> VnaLogger::acquire(const std::string &configurationFile)
{
    auto configuration = loadVnaLogConfiguration(configurationFile);
    if (!configuration.hasValue()) {
        return Result<std::shared_ptr<VnaLogger>>::failure(configuration.error());
    }
    return acquire(configuration.takeValue());
}

Result<std::shared_ptr<VnaLogger>> VnaLogger::acquire(VnaLogConfiguration configuration)
{
    std::shared_ptr<VnaLogger> result;
    try {
        // 创建和复用在同一把锁下完成，避免两个线程各自打开并轮转同一个日志文件。
        std::lock_guard<std::mutex> lock(activeLoggerMutex);
        if (auto existing = activeLogger.lock()) {
            if (!existing->uses(configuration)) {
                return Result<std::shared_ptr<VnaLogger>>::failure(
                    loggerError("another VNA manager uses a different logging configuration"));
            }
            return Result<std::shared_ptr<VnaLogger>>::success(std::move(existing));
        }
        // 同步多线程 sink 使 warning/error 的 flush 语义明确，也避免额外后台线程影响关闭顺序。
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            configuration.logFile, configuration.maximumFileBytes, configuration.maximumFiles);
        auto backend = std::make_shared<spdlog::logger>("vnapro", std::move(sink));
        backend->set_level(backendLevel(configuration.level));
        backend->flush_on(backendLevel(configuration.flushLevel));
        backend->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] %v");
        result = std::shared_ptr<VnaLogger>(
            new VnaLogger(std::make_unique<Impl>(std::move(configuration), std::move(backend))));
        activeLogger = result;
    } catch (const std::exception &exception) {
        return Result<std::shared_ptr<VnaLogger>>::failure(
            loggerError(std::string{"logging backend cannot start: "} + exception.what()));
    }
    write(
        VnaLogLevel::Info, "日志系统已启动，文件=" + result->implementation_->configuration.logFile +
                               "，级别=" + logLevelName(result->implementation_->configuration.level));
    return Result<std::shared_ptr<VnaLogger>>::success(std::move(result));
}

VnaLogger::VnaLogger(std::unique_ptr<Impl> implementation) : implementation_(std::move(implementation))
{
}

VnaLogger::~VnaLogger()
{
    try {
        implementation_->backend->info("日志系统正在关闭");
        implementation_->backend->flush();
    } catch (...) {
    }
    std::lock_guard<std::mutex> lock(activeLoggerMutex);
    if (const auto current = activeLogger.lock(); current && (current.get() == this)) {
        activeLogger.reset();
    }
}

bool VnaLogger::uses(const VnaLogConfiguration &configuration) const
{
    return sameLogConfiguration(implementation_->configuration, configuration);
}

void VnaLogger::write(VnaLogLevel level, const std::string &message) noexcept
{
    try {
        std::shared_ptr<VnaLogger> logger;
        {
            // 锁内升级 weak_ptr，锁外写文件；shared_ptr 保证本次写入结束前后端不会析构。
            std::lock_guard<std::mutex> lock(activeLoggerMutex);
            logger = activeLogger.lock();
        }
        if (logger) {
            logger->implementation_->backend->log(backendLevel(level), "{}", message);
        }
    } catch (...) {
    }
}

void VnaLogger::writeDriver(std::uint32_t level, VnaStringViewV1 message) noexcept
{
    try {
        // ABI 消息设置硬上限，避免失控驱动借日志入口触发超大分配。
        constexpr std::size_t maximumDriverMessage = 4096;
        if ((message.size > maximumDriverMessage) || ((message.size > 0) && (message.data == nullptr))) {
            write(VnaLogLevel::Warning, "驱动提交了非法日志消息");
            return;
        }
        const std::string copied = message.size == 0 ? std::string{} : std::string(message.data, message.size);
        write(driverLevel(level), "[driver] " + copied);
    } catch (...) {
        // C ABI 日志入口必须吞掉分配和格式化异常，日志失败不能终止驱动线程。
    }
}

}  // namespace vna::detail
