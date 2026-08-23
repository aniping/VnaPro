#include "VnaIntegrationTestSupport.h"

#include <gtest/gtest.h>

namespace vna::test {

CommandId unwrapCommand(Result<CommandId> result)
{
    if (!result.hasValue()) {
        ADD_FAILURE() << "异步命令未被接纳: " << result.error().description;
        return 0;
    }
    return result.takeValue();
}

}  // namespace vna::test
