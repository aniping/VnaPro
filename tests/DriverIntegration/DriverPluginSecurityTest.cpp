#include <cstdlib>
#include <stdexcept>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "VnaHardwareManager.h"

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        // mkdtemp 原子创建独占目录，避免测试夹具本身引入可预测路径竞态。
        char pattern[] = "/tmp/vnapro-plugin-test-XXXXXX";
        char *created = mkdtemp(pattern);
        if (created == nullptr) {
            throw std::runtime_error("cannot create temporary plugin directory");
        }
        path_ = created;
    }

    ~TemporaryDirectory()
    {
        // 测试会故意放宽权限，析构前恢复所有者权限以确保目录可清理。
        chmod(path_.c_str(), 0700);
        rmdir(path_.c_str());
    }

    const std::string &path() const
    {
        return path_;
    }

private:
    std::string path_;
};

TEST(DriverPluginSecurityTest, RejectsExternallyWritableDirectory)
{
    // 组/其他用户可写的目录允许替换插件，加载器必须在枚举前直接拒绝。
    TemporaryDirectory directory;
    ASSERT_EQ(chmod(directory.path().c_str(), 0777), 0);
    auto result = vna::VnaHardwareManager::create(directory.path(), VNA_TEST_LOG_CONFIGURATION);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().description, "driver directory is not trusted");
}

}  // namespace
