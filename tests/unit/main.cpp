#include "Test.h"

#include <cstdio>
#include <stdexcept>

namespace kmtest {

std::vector<Case>& Registry() {
    static std::vector<Case> r;
    return r;
}

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void Fail(const char* file, int line, const std::string& msg) {
    throw Failure(std::string(file) + ":" + std::to_string(line) + ": " + msg);
}

}  // namespace kmtest

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int passed = 0, failed = 0;
    for (const auto& c : kmtest::Registry()) {
        if (filter && std::string(c.name).find(filter) == std::string::npos) continue;
        try {
            c.fn();
            ++passed;
            std::printf("[ ok ] %s\n", c.name);
        } catch (const std::exception& e) {
            ++failed;
            std::printf("[FAIL] %s\n    %s\n", c.name, e.what());
        }
    }
    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
