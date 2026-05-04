/*
 * JSON Utilities — QJsonObject wrapper.
 *
 * Thin convenience layer over Qt's JSON types so the rest of the
 * codebase can use familiar string-keyed get_string/get_int/get_bool
 * helpers without sprinkling QString::fromStdString everywhere.
 */

#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <QJsonObject>
#include <string>
#include <vector>

namespace json_utils {

// Type alias kept for source compatibility with the previous
// nlohmann::json wrapper. New code can use QJsonObject directly.
using json = QJsonObject;

/**
 * Parse JSON string into a QJsonObject. Returns an empty object on
 * parse error or if the root is not an object.
 */
json parse(const std::string& str);

/**
 * Serialize a QJsonObject to a string.
 * @param indent Indentation level (-1 for compact, otherwise indented)
 */
std::string to_string(const json& j, int indent = -1);

/**
 * Get string value with default. Returns default if key missing or
 * value is not a string.
 */
std::string get_string(const json& j, const std::string& key,
                       const std::string& default_val = "");

/**
 * Get integer value with default. Returns default if key missing or
 * value is not a number.
 */
int get_int(const json& j, const std::string& key, int default_val = 0);

/**
 * Get boolean value with default. Returns default if key missing or
 * value is not a boolean.
 */
bool get_bool(const json& j, const std::string& key, bool default_val = false);

/**
 * Check if key exists in the object.
 */
bool has_key(const json& j, const std::string& key);

/**
 * Get a string array from the object. Returns empty vector if key
 * missing or value is not an array of strings.
 */
std::vector<std::string> get_string_array(const json& j, const std::string& key);

/**
 * Parse a JSON file into a QJsonObject.
 * @throws std::runtime_error on file open or parse error
 */
json parse_file(const std::string& path);

} // namespace json_utils

#endif // JSON_UTILS_H
