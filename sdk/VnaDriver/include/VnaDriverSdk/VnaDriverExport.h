#ifndef VNA_DRIVER_SDK_VNA_DRIVER_EXPORT_H
#define VNA_DRIVER_SDK_VNA_DRIVER_EXPORT_H

/* 驱动默认隐藏所有实现符号，只把统一 ABI 入口暴露给动态加载器。 */
#if defined(_WIN32)
#define VNA_DRIVER_EXPORT __declspec(dllexport)
#else
#define VNA_DRIVER_EXPORT __attribute__((visibility("default")))
#endif

#endif
