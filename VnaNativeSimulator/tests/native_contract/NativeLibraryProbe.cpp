#include "NativeLibraryProbe.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cctype>
#include <cstring>
#include <sstream>

namespace vna_test {
namespace {

bool IsAbsolutePath(const std::string& path)
{
#if defined(_WIN32)
    const bool driveRooted = path.size() >= 3U && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' &&
                             (path[2] == '/' || path[2] == '\\');
    const bool unc = path.size() >= 5U && path[0] == '\\' && path[1] == '\\' && path[2] != '\\';
    return driveRooted || unc;
#else
    return !path.empty() && path[0] == '/';
#endif
}

#if defined(_WIN32)
std::string LoaderError(const char* operation)
{
    std::ostringstream message;
    message << operation << " failed (Windows error " << GetLastError() << ')';
    return message.str();
}
#endif

template <typename FunctionPointer>
bool ResolveSymbol(void* handle, const char* symbolName, FunctionPointer* target, std::string* errorMessage)
{
#if defined(_WIN32)
    FARPROC symbol = GetProcAddress(static_cast<HMODULE>(handle), symbolName);
    if (symbol == NULL) {
        if (errorMessage != NULL) {
            *errorMessage = LoaderError("GetProcAddress");
        }
        return false;
    }
#else
    dlerror();
    void* symbol = dlsym(handle, symbolName);
    const char* loaderError = dlerror();
    if (loaderError != NULL || symbol == NULL) {
        if (errorMessage != NULL) {
            *errorMessage = loaderError == NULL ? "符号地址为空" : loaderError;
        }
        return false;
    }
#endif

    static_assert(sizeof(FunctionPointer) == sizeof(symbol), "函数指针尺寸必须与平台动态加载器的符号地址一致");
    std::memcpy(target, &symbol, sizeof(symbol));
    return true;
}

}  // namespace

NativeFunctions::NativeFunctions()
    : hwInit(NULL),
      getSweepData(NULL),
      startSweep(NULL),
      activateRxLink(NULL),
      setRfPowerOn(NULL),
      setFastSweepMode(NULL),
      getIDN(NULL),
      checkSweepPortPower(NULL),
      dataFrameDownload(NULL)
{
}

NativeLibraryProbe::NativeLibraryProbe() : handle_(NULL), functions_() {}

NativeLibraryProbe::~NativeLibraryProbe()
{
    Close();
}

bool NativeLibraryProbe::Open(const std::string& absolutePath, std::string* errorMessage)
{
    Close();
    if (!IsAbsolutePath(absolutePath)) {
        if (errorMessage != NULL) {
            *errorMessage = "动态库路径必须是绝对路径";
        }
        return false;
    }

#if defined(_WIN32)
    handle_ = LoadLibraryExA(absolutePath.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
    handle_ = dlopen(absolutePath.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (handle_ == NULL) {
        if (errorMessage != NULL) {
#if defined(_WIN32)
            *errorMessage = LoaderError("LoadLibraryEx");
#else
            const char* loaderError = dlerror();
            *errorMessage = loaderError == NULL ? "dlopen 失败" : loaderError;
#endif
        }
        return false;
    }
    return true;
}

void NativeLibraryProbe::Close()
{
    functions_ = NativeFunctions();
    if (handle_ != NULL) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
        handle_ = NULL;
    }
}

bool NativeLibraryProbe::IsOpen() const
{
    return handle_ != NULL;
}

bool NativeLibraryProbe::ResolveAll(std::vector<std::string>* missingSymbols, std::string* errorMessage)
{
    bool allResolved = true;
    if (missingSymbols != NULL) {
        missingSymbols->clear();
    }
    if (handle_ == NULL) {
        if (errorMessage != NULL) {
            *errorMessage = "尚未打开动态库";
        }
        return false;
    }

#define VNA_RESOLVE(symbol_name, member_name)                                                   \
    do {                                                                                        \
        std::string vna_symbol_error;                                                           \
        if (!ResolveSymbol(handle_, symbol_name, &functions_.member_name, &vna_symbol_error)) { \
            allResolved = false;                                                                \
            if (missingSymbols != NULL) {                                                       \
                missingSymbols->push_back(symbol_name);                                         \
            }                                                                                   \
            if (errorMessage != NULL && errorMessage->empty()) {                                \
                *errorMessage = std::string(symbol_name) + ": " + vna_symbol_error;             \
            }                                                                                   \
        }                                                                                       \
    } while (false)

    if (errorMessage != NULL) {
        errorMessage->clear();
    }
    VNA_RESOLVE(vna_native::kDrvHwInitSymbol, hwInit);
    VNA_RESOLVE(vna_native::kDrvGetSweepDataSymbol, getSweepData);
    VNA_RESOLVE(vna_native::kDrvStartSweepSymbol, startSweep);
    VNA_RESOLVE(vna_native::kDrvActivateRxLinkSymbol, activateRxLink);
    VNA_RESOLVE(vna_native::kDrvSetRfPowerOnSymbol, setRfPowerOn);
    VNA_RESOLVE(vna_native::kDrvSetFastSweepModeSymbol, setFastSweepMode);
    VNA_RESOLVE(vna_native::kDrvGetIDNSymbol, getIDN);
    VNA_RESOLVE(vna_native::kDrvCheckSweepPortPowerSymbol, checkSweepPortPower);
    VNA_RESOLVE(vna_native::kDrvDataFrameDownloadSymbol, dataFrameDownload);
#undef VNA_RESOLVE

    return allResolved;
}

const NativeFunctions& NativeLibraryProbe::Functions() const
{
    return functions_;
}

}  // namespace vna_test
