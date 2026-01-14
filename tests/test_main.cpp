#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <sstream>

struct TestCase {
    std::string name;
    std::function<void()> func;
};

static std::vector<TestCase>& get_tests() {
    static std::vector<TestCase> tests;
    return tests;
}

void register_test(const std::string& name, std::function<void()> func) {
    get_tests().push_back({name, std::move(func)});
}

#define TEST_CASE(name) \
    static void test_##name(); \
    static struct Register_##name { \
        Register_##name() { register_test(#name, test_##name); } \
    } register_##name; \
    static void test_##name()

#define REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            std::ostringstream oss; \
            oss << "FAILED: " << #expr << " at " << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(oss.str()); \
        } \
    } while(0)

#define REQUIRE_EQUAL(a, b) \
    do { \
        if ((a) != (b)) { \
            std::ostringstream oss; \
            oss << "FAILED: " << #a << " != " << #b << " at " << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(oss.str()); \
        } \
    } while(0)

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
