#include "rtmp/amf0.hpp"
#include "core/bytes.hpp"
#include <cstring>

namespace amf0 {

Type Value::type() const {
    if (is_null()) return Type::Null;
    if (is_number()) return Type::Number;
    if (is_boolean()) return Type::Boolean;
    if (is_string()) return Type::String;
    if (is_object()) return Type::Object;
    return Type::Null;
}

Value Value::EcmaArray(ArrayType arr) {
    return Value(std::move(arr));
}

static void encode_string(std::vector<uint8_t>& out, const std::string& str, bool with_type = true) {
    if (with_type) {
        out.push_back(static_cast<uint8_t>(Type::String));
    }
    uint16_t len = static_cast<uint16_t>(str.size());
    uint8_t len_bytes[2];
    bytes::write_u16_be(len_bytes, len);
    out.insert(out.end(), len_bytes, len_bytes + 2);
    out.insert(out.end(), str.begin(), str.end());
}

static void encode_value_impl(std::vector<uint8_t>& out, const Value& value) {
    if (value.is_number()) {
        out.push_back(static_cast<uint8_t>(Type::Number));
        uint8_t num_bytes[8];
        bytes::write_f64_be(num_bytes, value.as_number());
        out.insert(out.end(), num_bytes, num_bytes + 8);
    }
    else if (value.is_boolean()) {
        out.push_back(static_cast<uint8_t>(Type::Boolean));
        out.push_back(value.as_boolean() ? 1 : 0);
    }
    else if (value.is_string()) {
        encode_string(out, value.as_string());
    }
    else if (value.is_null()) {
        out.push_back(static_cast<uint8_t>(Type::Null));
    }
    else if (value.is_object()) {
        out.push_back(static_cast<uint8_t>(Type::Object));
        for (const auto& [key, val] : value.as_object()) {
            uint8_t len_bytes[2];
            bytes::write_u16_be(len_bytes, static_cast<uint16_t>(key.size()));
            out.insert(out.end(), len_bytes, len_bytes + 2);
            out.insert(out.end(), key.begin(), key.end());
            encode_value_impl(out, val);
        }
        out.push_back(0);
        out.push_back(0);
        out.push_back(9);
    }
}

Result<std::vector<uint8_t>> encode(const Value& value) {
    std::vector<uint8_t> out;
    encode_value_impl(out, value);
    return Result<std::vector<uint8_t>>(std::move(out));
}

Result<std::vector<uint8_t>> encode_array(const std::vector<Value>& values) {
    std::vector<uint8_t> out;
    for (const auto& value : values) {
        encode_value_impl(out, value);
    }
    return Result<std::vector<uint8_t>>(std::move(out));
}

static Result<std::pair<std::string, size_t>> decode_string(std::span<const uint8_t> data, bool has_type = true) {
    size_t offset = 0;
    
    if (has_type) {
        if (data.size() < 1) {
            return Result<std::pair<std::string, size_t>>::Err("Not enough data for string type");
        }
        if (data[0] != static_cast<uint8_t>(Type::String)) {
            return Result<std::pair<std::string, size_t>>::Err("Not a string type");
        }
        offset = 1;
    }
    
    if (data.size() < offset + 2) {
        return Result<std::pair<std::string, size_t>>::Err("Not enough data for string length");
    }
    
    uint16_t len = bytes::read_u16_be(data.data() + offset);
    offset += 2;
    
    if (data.size() < offset + len) {
        return Result<std::pair<std::string, size_t>>::Err("Not enough data for string content");
    }
    
    std::string str(reinterpret_cast<const char*>(data.data() + offset), len);
    offset += len;
    
    return Result<std::pair<std::string, size_t>>(std::make_pair(std::move(str), offset));
}

static Result<std::pair<Value, size_t>> decode_impl(std::span<const uint8_t> data) {
    if (data.empty()) {
        return Result<std::pair<Value, size_t>>::Err("Empty data");
    }
    
    Type type = static_cast<Type>(data[0]);
    size_t offset = 1;
    
    switch (type) {
        case Type::Number: {
            if (data.size() < 9) {
                return Result<std::pair<Value, size_t>>::Err("Not enough data for number");
            }
            double num = bytes::read_f64_be(data.data() + 1);
            return Result<std::pair<Value, size_t>>(std::make_pair(Value::Number(num), 9));
        }
        
        case Type::Boolean: {
            if (data.size() < 2) {
                return Result<std::pair<Value, size_t>>::Err("Not enough data for boolean");
            }
            bool val = data[1] != 0;
            return Result<std::pair<Value, size_t>>(std::make_pair(Value::Boolean(val), 2));
        }
        
        case Type::String: {
            auto result = decode_string(data.subspan(1), false);
            if (result.is_err()) {
                return Result<std::pair<Value, size_t>>::Err(result.error());
            }
            auto [str, consumed] = result.value();
            return Result<std::pair<Value, size_t>>(std::make_pair(Value::String(std::move(str)), consumed + 1));
        }
        
        case Type::Null: {
            return Result<std::pair<Value, size_t>>(std::make_pair(Value::Null(), 1));
        }
        
        case Type::Object:
        case Type::EcmaArray: {
            if (type == Type::EcmaArray) {
                if (data.size() < 5) {
                    return Result<std::pair<Value, size_t>>::Err("Not enough data for ECMA array");
                }
                offset = 5;
            }
            
            Value::ObjectType obj;
            while (offset + 3 <= data.size()) {
                uint16_t key_len = bytes::read_u16_be(data.data() + offset);
                offset += 2;
                
                if (key_len == 0) {
                    if (offset < data.size() && data[offset] == 9) {
                        offset++;
                        break;
                    }
                }
                
                if (offset + key_len > data.size()) {
                    return Result<std::pair<Value, size_t>>::Err("Not enough data for object key");
                }
                
                std::string key(reinterpret_cast<const char*>(data.data() + offset), key_len);
                offset += key_len;
                
                auto val_result = decode_impl(data.subspan(offset));
                if (val_result.is_err()) {
                    return Result<std::pair<Value, size_t>>::Err(val_result.error());
                }
                
                auto [val, val_consumed] = val_result.value();
                obj[key] = std::move(val);
                offset += val_consumed;
            }
            
            return Result<std::pair<Value, size_t>>(std::make_pair(Value::Object(std::move(obj)), offset));
        }
        
        default:
            return Result<std::pair<Value, size_t>>::Err("Unknown AMF0 type");
    }
}

Result<std::pair<Value, size_t>> decode(std::span<const uint8_t> data) {
    return decode_impl(data);
}

Result<std::vector<Value>> decode_array(std::span<const uint8_t> data) {
    std::vector<Value> values;
    size_t offset = 0;
    
    while (offset < data.size()) {
        auto result = decode_impl(data.subspan(offset));
        if (result.is_err()) {
            if (values.empty()) {
                return Result<std::vector<Value>>::Err(result.error());
            }
            break;
        }
        
        auto [value, consumed] = result.value();
        values.push_back(std::move(value));
        offset += consumed;
    }
    
    return Result<std::vector<Value>>(std::move(values));
}

}
