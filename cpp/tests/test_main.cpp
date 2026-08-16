#include <iostream>

int RunProtocolTests();
int RunReceiverTests();
int RunMathTests();
int RunUtilTests();
int RunSessionTests();
int RunUnrealMathTests();
int RunProjectionTests();
int RunInterpolatorTests();

// Simple test runner - expand with a proper framework if needed
int main() {
    std::cout << "CameraUnlock Core Tests\n";
    std::cout << "=====================\n";

    int failures = 0;
    failures += RunProtocolTests();
    failures += RunReceiverTests();
    failures += RunMathTests();
    failures += RunUtilTests();
    failures += RunSessionTests();
    failures += RunUnrealMathTests();
    failures += RunProjectionTests();
    failures += RunInterpolatorTests();

    if (failures == 0) {
        std::cout << "All tests passed!\n";
        return 0;
    }
    std::cout << failures << " test(s) FAILED\n";
    return 1;
}
