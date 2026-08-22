#include <stddef.h>

#include "VnaDriverSdk/VnaDriverApi.h"

/* 此文件强制以 C99 编译，防止公开 ABI 头无意引入任何 C++ 专属语法。 */
_Static_assert(sizeof(void *) == 8, "V1 prototype ABI requires a 64-bit target");
/* 固定结构尺寸可尽早发现字段重排、类型宽度变化或编译器对齐差异。 */
_Static_assert(sizeof(VnaStringViewV1) == 16, "VnaStringViewV1 layout changed");
_Static_assert(sizeof(VnaStatusV1) == 24, "VnaStatusV1 layout changed");
_Static_assert(sizeof(VnaComplex64V1) == 16, "VnaComplex64V1 layout changed");
_Static_assert(sizeof(VnaWaveKeyV1) == 8, "VnaWaveKeyV1 layout changed");
_Static_assert(sizeof(VnaSourcePlanV1) == 24, "VnaSourcePlanV1 layout changed");
_Static_assert(sizeof(VnaExcitationPlanViewV1) == 40, "excitation view layout changed");
_Static_assert(sizeof(VnaPointPlanViewV1) == 48, "point view layout changed");
_Static_assert(sizeof(VnaSweepPlanViewV1) == 32, "sweep plan view layout changed");
_Static_assert(sizeof(VnaCapabilitiesViewV1) == 88, "capabilities view layout changed");
_Static_assert(sizeof(VnaWaveLayoutViewV1) == 24, "wave layout view layout changed");
_Static_assert(sizeof(VnaPrepareResultViewV1) == 48, "prepare result layout changed");
_Static_assert(sizeof(VnaMutableWaveBlockViewV1) == 32, "wave block view layout changed");
_Static_assert(sizeof(VnaDriverHostApiV1) == 56, "host API layout changed");
_Static_assert(sizeof(VnaDriverApiV1) == 144, "driver API layout changed");

int main(void)
{
    /* 静态断言即为全部测试逻辑；成功生成可执行文件就表示 C99 ABI 契约成立。 */
    return 0;
}
