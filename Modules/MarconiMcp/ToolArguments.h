// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Reading the arguments of a tool call, and writing the JSON Schemas that describe them

#pragma once

#include <json/single_include/nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

namespace Marconi
{
    namespace Mcp
    {
        // A bad argument: reported to the AI as a failed tool call.
        struct ArgumentError
        {
            std::string message;
        };

        inline int GetInt(const nlohmann::json& arguments, const char* key, int fallback, int minimum, int maximum)
        {
            if (!arguments.contains(key) || arguments[key].is_null())
            {
                return fallback;
            }
            const nlohmann::json& value = arguments[key];
            if (!value.is_number())
            {
                throw ArgumentError{std::string(key) + " must be a number."};
            }
            const double number = value.get<double>();
            if (number < minimum || number > maximum)
            {
                throw ArgumentError{std::string(key) + " must be between " + std::to_string(minimum) + " and " +
                                    std::to_string(maximum) + "."};
            }
            return static_cast<int>(number);
        }

        inline std::string GetString(const nlohmann::json& arguments, const char* key, bool required)
        {
            if (!arguments.contains(key) || arguments[key].is_null())
            {
                if (required)
                {
                    throw ArgumentError{std::string(key) + " is needed."};
                }
                return std::string();
            }
            if (!arguments[key].is_string())
            {
                throw ArgumentError{std::string(key) + " must be text."};
            }
            return arguments[key].get<std::string>();
        }

        inline nlohmann::json Property(const char* type, const char* description)
        {
            return {{"type", type}, {"description", description}};
        }

        inline nlohmann::json ObjectSchema(nlohmann::json properties, std::vector<std::string> required = {})
        {
            nlohmann::json schema = {{"type", "object"}, {"additionalProperties", false}};
            if (!properties.empty())
            {
                schema["properties"] = std::move(properties);
            }
            if (!required.empty())
            {
                schema["required"] = required;
            }
            return schema;
        }

        inline bool GetBool(const nlohmann::json& arguments, const char* key, bool fallback)
        {
            if (!arguments.contains(key) || arguments[key].is_null())
            {
                return fallback;
            }
            if (!arguments[key].is_boolean())
            {
                throw ArgumentError{std::string(key) + " must be true or false."};
            }
            return arguments[key].get<bool>();
        }
    } // namespace Mcp
} // namespace Marconi
