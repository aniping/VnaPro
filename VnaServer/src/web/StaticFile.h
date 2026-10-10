#ifndef VNAPRO_WEB_STATIC_FILE_H
#define VNAPRO_WEB_STATIC_FILE_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace vna_web {
// Keep the verified handle open throughout incremental reading to prevent a
// path replacement between validation and a later reopening of the file.
class StaticFile {
public:
    StaticFile(const std::string &root, const std::string &path);
    ~StaticFile();
    StaticFile(const StaticFile &) = delete;
    StaticFile &operator=(const StaticFile &) = delete;
    std::size_t size() const;
    bool advance(std::string &output);

private:
    std::size_t size_ = 0;
#ifdef _WIN32
    void *file_ = reinterpret_cast<void *>(static_cast<std::intptr_t>(-1));
    static std::string finalPath(void *file);
#else
    int file_ = -1;
#endif
};
}  // namespace vna_web

#endif
