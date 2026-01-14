#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <sstream>
#include <stdexcept>

struct TestCase {
    std::string name;
    std::function<void()> func;
};

std::vector<TestCase>& get_tests();

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

void register_test(const std::string& name, std::function<void()> func);
