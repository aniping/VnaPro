#include <fstream>
#include <stdexcept>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "VnaLogger.h"

namespace {

class TemporaryLogDirectory final {
public:
    TemporaryLogDirectory()
    {
        char pattern[] = "/tmp/vnapro-log-test-XXXXXX";
        char *created = mkdtemp(pattern);
        if (created == nullptr) {
            throw std::runtime_error("cannot create temporary log directory");
        }
        path_ = created;
    }

    ~TemporaryLogDirectory()
    {
        // 测试只删除自己创建且名称完全确定的配置、活动日志和两个轮转文件。
        unlink(configurationFile().c_str());
        unlink(logFile().c_str());
        unlink(rotatedFile(1).c_str());
        unlink(rotatedFile(2).c_str());
        rmdir(path_.c_str());
    }

    std::string configurationFile() const
    {
        return path_ + "/vnapro.conf";
    }

    std::string logFile() const
    {
        return path_ + "/cycle-" + std::to_string(getpid()) + ".log";
    }

    std::string rotatedFile(std::size_t index) const
    {
        return path_ + "/cycle-" + std::to_string(getpid()) + "." + std::to_string(index) + ".log";
    }

private:
    std::string path_;
};

void writeConfiguration(const TemporaryLogDirectory &directory, const char *maximumBytes)
{
    std::ofstream output(directory.configurationFile());
    ASSERT_TRUE(output.good());
    output << "[logging]\n"
           << "file=cycle-{pid}.log\n"
           << "level=trace\n"
           << "flush_level=trace\n"
           << "max_file_bytes=" << maximumBytes << "\n"
           << "max_files=2\n";
    output.close();
    ASSERT_EQ(chmod(directory.configurationFile().c_str(), 0600), 0);
}

TEST(VnaLoggingTest, RotatesFilesUsingConfigurationLimits)
{
    TemporaryLogDirectory directory;
    writeConfiguration(directory, "1024");
    auto logger = vna::detail::VnaLogger::acquire(directory.configurationFile());
    ASSERT_TRUE(logger.hasValue()) << logger.error().description;

    // 每条消息都超过百字节，连续写入必然跨过 1 KiB 上限并产生轮转文件。
    const std::string payload(160, 'x');
    for (std::size_t index = 0; index < 40; ++index) {
        vna::detail::VnaLogger::write(vna::detail::VnaLogLevel::Info, std::to_string(index) + payload);
    }
    auto owner = logger.takeValue();
    owner.reset();

    struct stat attributes{};
    EXPECT_EQ(stat(directory.logFile().c_str(), &attributes), 0);
    EXPECT_EQ(stat(directory.rotatedFile(1).c_str(), &attributes), 0);
    EXPECT_NE(stat(directory.rotatedFile(3).c_str(), &attributes), 0);
}

TEST(VnaLoggingTest, RejectsInvalidRotationSize)
{
    TemporaryLogDirectory directory;
    writeConfiguration(directory, "0");
    auto logger = vna::detail::VnaLogger::acquire(directory.configurationFile());
    ASSERT_FALSE(logger.hasValue());
    EXPECT_EQ(logger.error().code, vna::VnaErrorCode::ConfigurationRejected);
}

}  // namespace
