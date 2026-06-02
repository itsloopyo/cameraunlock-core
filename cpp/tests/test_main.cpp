#include <iostream>

int RunProtocolTests();
int RunMathTests();
int RunUtilTests();
int RunSessionTests();
int RunUnrealMathTests();
int RunProjectionTests();

// Simple test runner - expand with a proper framework if needed
int main() {
    std::cout << "CameraUnlock Core Tests\n";
    std::cout << "=====================\n";

    int failures = 0;
    failures += RunProtocolTests();
    failures += RunMathTests();
    failures += RunUtilTests();
    failures += RunSessionTests();
    failures += RunUnrealMathTests();
    failures += RunProjectionTests();

    if (failures == 0) {
        std::cout << "All tests passed!\n";
        return 0;
    }
    std::cout << failures << " test(s) FAILED\n";
    return 1;
}
