#include "../TestSupport.h"
#include "SimulatorEnvironment.h"

int main()
{
    vna_test::ScopedSimulatorEnvironment environment(NULL, NULL);
    return vna_test::RunRegisteredTests();
}
