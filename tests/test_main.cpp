#include "test_framework.hpp"

std::vector<TestCase>& get_tests() {
    static std::vector<TestCase> tests;
    return tests;
}

void register_test(const std::string& name, std::function<void()> func) {
    get_tests().push_back({name, std::move(func)});
}

int main() {
    int passed = 0;
    int failed = 0;
    
    std::cout << "Running " << get_tests().size() << " tests...\n" << std::endl;
    
    for (const auto& test : get_tests()) {
        std::cout << "Running: " << test.name << "... ";
        std::cout.flush();
        
        try {
            test.func();
            std::cout << "PASSED" << std::endl;
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "\n  " << e.what() << std::endl;
            ++failed;
        }
    }
    
    std::cout << "\n" << passed << " passed, " << failed << " failed" << std::endl;
    
    return failed > 0 ? 1 : 0;
}
