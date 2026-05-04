/*
 * JSON Utilities — QJsonObject wrapper implementation.
 */

#include "json_utils.h"
#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QString>
#include <stdexcept>

namespace json_utils {

namespace {
inline QString qkey(const std::string& key) {
    return QString::fromStdString(key);
}
}

json parse(const std::string& str) {
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(str));
    return doc.isObject() ? doc.object() : QJsonObject{};
}

std::string to_string(const json& j, int indent) {
    QJsonDocument doc(j);
    QByteArray ba = doc.toJson(indent < 0 ? QJsonDocument::Compact
                                          : QJsonDocument::Indented);
    return ba.toStdString();
}

std::string get_string(const json& j, const std::string& key,
                       const std::string& default_val) {
    QJsonValue v = j.value(qkey(key));
    if (!v.isString()) return default_val;
    return v.toString().toStdString();
}

int get_int(const json& j, const std::string& key, int default_val) {
    QJsonValue v = j.value(qkey(key));
    if (!v.isDouble()) return default_val;
    return v.toInt(default_val);
}

bool get_bool(const json& j, const std::string& key, bool default_val) {
    QJsonValue v = j.value(qkey(key));
    if (!v.isBool()) return default_val;
    return v.toBool(default_val);
}

bool has_key(const json& j, const std::string& key) {
    return j.contains(qkey(key));
}

std::vector<std::string> get_string_array(const json& j, const std::string& key) {
    std::vector<std::string> result;
    QJsonValue v = j.value(qkey(key));
    if (!v.isArray()) return result;
    QJsonArray arr = v.toArray();
    for (const QJsonValue& elem : arr) {
        if (elem.isString()) {
            result.push_back(elem.toString().toStdString());
        }
    }
    return result;
}

json parse_file(const std::string& path) {
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    QByteArray data = file.readAll();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        throw std::runtime_error("JSON parse error in " + path + ": " +
                                 err.errorString().toStdString());
    }
    if (!doc.isObject()) {
        throw std::runtime_error("JSON root in " + path + " is not an object");
    }
    return doc.object();
}

} // namespace json_utils
