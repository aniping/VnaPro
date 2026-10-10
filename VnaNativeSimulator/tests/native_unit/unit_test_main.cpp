#include "../test_support.h"
#include "simulator_environment.h"

int main()
{
    vna_test::ScopedSimulatorEnvironment environment(NULL, NULL);
    return vna_test::RunRegisteredTests();
}
