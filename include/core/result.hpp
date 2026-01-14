#pragma once
#include <string>
#include <variant>
#include <utility>

template<typename T, typename E = std::string>
class Result {
    std::variant<T, E> data_;
    
public:
    Result(T value) : data_(std::move(value)) {}
    
    static Result Ok(T value) { return Result(std::move(value)); }
    static Result Err(E error) { 
        Result r(T{});
        r.data_ = std::move(error);
        return r;
    }
    
    bool is_ok() const { return std::holds_alternative<T>(data_); }
    bool is_err() const { return std::holds_alternative<E>(data_); }
    
    const T& value() const { return std::get<T>(data_); }
    T& value() { return std::get<T>(data_); }
    
    const E& error() const { return std::get<E>(data_); }
    E& error() { return std::get<E>(data_); }
    
    T value_or(T default_val) const {
        return is_ok() ? value() : std::move(default_val);
    }
};

template<typename E>
class Result<void, E> {
    std::variant<std::monostate, E> data_;
    
public:
    Result() : data_(std::monostate{}) {}
    Result(E error) : data_(std::move(error)) {}
    
    bool is_ok() const { return std::holds_alternative<std::monostate>(data_); }
    bool is_err() const { return std::holds_alternative<E>(data_); }
    
    const E& error() const { return std::get<E>(data_); }
    E& error() { return std::get<E>(data_); }
    
    static Result Ok() { return Result(); }
    static Result Err(E error) { return Result(std::move(error)); }
};
