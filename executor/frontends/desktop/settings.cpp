/*
 * settings.cpp - settings.json and desktop.json (settings.h).
 */
#include "settings.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <climits>
#include <cstdio>

#include <cctype>
#include <cstring>

namespace {

QJsonObject read_json(const QString &path)
{
    QFile f(path);
    if(!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool write_json(const QString &path, const QJsonObject &o)
{
    QSaveFile f(path);
    if(!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

/* QJsonObject sorts its keys; the dialog follows the schema's order. The
   keys of the objects at PATH ("properties", "properties/G/properties"),
   in the order the text has them: a scan of strings and nesting. */
class KeyOrder
{
public:
    explicit KeyOrder(const QByteArray &text) : s(text) {}
    QMap<QString, QStringList> scan()
    {
        value(QString());
        return out;
    }

private:
    const QByteArray &s;
    int i = 0;
    QMap<QString, QStringList> out;

    void ws()
    {
        while(i < s.size() && isspace((unsigned char)s[i]))
            i++;
    }
    QString string()
    {
        QByteArray b;
        for(i++; i < s.size() && s[i] != '"'; i++)
        {
            if(s[i] == '\\')
                i++;
            b += s[i];
        }
        i++;
        return QString::fromUtf8(b);
    }
    void value(const QString &path)
    {
        ws();
        if(i >= s.size())
            return;
        char c = s[i];
        if(c == '{')
        {
            i++;
            for(;;)
            {
                ws();
                if(i >= s.size() || s[i] == '}')
                {
                    i++;
                    return;
                }
                if(s[i] == ',')
                {
                    i++;
                    continue;
                }
                QString key = string();
                out[path] << key;
                ws();
                i++;  /* : */
                value(path.isEmpty() ? key : path + "/" + key);
            }
        }
        else if(c == '[')
        {
            i++;
            for(;;)
            {
                ws();
                if(i >= s.size() || s[i] == ']')
                {
                    i++;
                    return;
                }
                if(s[i] == ',')
                {
                    i++;
                    continue;
                }
                value(path + "/[]");
            }
        }
        else if(c == '"')
            string();
        else
            while(i < s.size() && !strchr(",}] \t\r\n", s[i]))
                i++;
    }
};

}

Settings::Settings(const QString &dataDir, QObject *parent) : QObject(parent), dataDir_(dataDir)
{
    QFile f(":/settings.schema.json");
    f.open(QIODevice::ReadOnly);
    QByteArray text = f.readAll();
    schema_ = QJsonDocument::fromJson(text).object();
    auto order = KeyOrder(text).scan();
    for(const QString &g : order.value("properties"))
        if(schema_["properties"][g].toObject().contains("properties"))
        {
            groupOrder_ << g;
            propertyOrder_[g] = order.value("properties/" + g + "/properties");
        }
    values_ = read_json(dataDir_ + "/settings.json");
    desktop_ = read_json(dataDir_ + "/desktop.json");
}

QJsonObject Settings::group(const QString &g) const
{
    return schema_["properties"][g].toObject();
}

QJsonObject Settings::property(const QString &key) const
{
    QString g = key.section('.', 0, 0), p = key.section('.', 1);
    return schema_["properties"][g]["properties"][p].toObject();
}

QJsonValue Settings::get(const QString &key) const
{
    QString g = key.section('.', 0, 0), p = key.section('.', 1);
    QJsonValue v = values_[g][p];
    return v.isUndefined() ? property(key).value("default") : v;
}

QStringList Settings::list(const QString &key) const
{
    QStringList out;
    for(const QJsonValue &v : get(key).toArray())
        out << v.toString();
    return out;
}

/* V against KEY's schema, as far as the server checks a page setting
   (schema.go validate): type, enum, range, length, pattern, items. Empty:
   valid. */
QString Settings::invalid(const QString &key, const QJsonValue &v) const
{
    QJsonObject p = property(key);
    if(p.isEmpty())
        return "not a setting";
    QStringList types;
    QJsonValue t = p.value("type");
    if(t.isArray())
        for(const QJsonValue &x : t.toArray())
            types << x.toString();
    else
        types << t.toString();
    auto is = [&](const QJsonValue &x, const QString &type) {
        return (type == "string" && x.isString()) || (type == "boolean" && x.isBool())
               || (type == "array" && x.isArray())
               || (type == "integer" && x.isDouble() && x.toDouble() == (double)(qint64)x.toDouble());
    };
    if(std::none_of(types.begin(), types.end(), [&](const QString &type) { return is(v, type); }))
        return "a " + types.join(" or ");
    if(p.contains("enum") && !p.value("enum").toArray().contains(v))
        return "not one of its choices";
    if(v.isDouble())
    {
        if(p.contains("minimum") && v.toDouble() < p.value("minimum").toDouble())
            return "below its minimum";
        if(p.contains("maximum") && v.toDouble() > p.value("maximum").toDouble())
            return "above its maximum";
    }
    if(v.isString())
    {
        if(p.contains("maxLength") && v.toString().size() > p.value("maxLength").toInt())
            return QString("longer than %1").arg(p.value("maxLength").toInt());
        if(p.contains("pattern") && !QRegularExpression(p.value("pattern").toString()).match(v.toString()).hasMatch())
            return "not of its form";
    }
    if(v.isArray())
    {
        QJsonArray a = v.toArray();
        if(p.contains("maxItems") && a.size() > p.value("maxItems").toInt())
            return QString("more than %1 items").arg(p.value("maxItems").toInt());
        QJsonObject items = p.value("items").toObject();
        for(const QJsonValue &x : a)
            if(items.value("type").toString() == "string"
               && (!x.isString() || x.toString().size() > items.value("maxLength").toInt(INT_MAX)
                   || x.toString().size() < items.value("minLength").toInt(0)))
                return "an item is not of its form";
    }
    return {};
}

bool Settings::set(const QString &key, const QJsonValue &v)
{
    if(get(key) == v)
        return true;
    /* the server refuses a file with a value the schema doesn't allow:
       never written */
    QString why = invalid(key, v);
    if(!why.isEmpty())
    {
        fprintf(stderr, "[desktop] setting %s not changed: %s\n", key.toLocal8Bit().constData(),
                why.toLocal8Bit().constData());
        return false;
    }
    QString g = key.section('.', 0, 0), p = key.section('.', 1);
    QJsonObject group = values_[g].toObject();
    group[p] = v;
    values_[g] = group;
    save();
    emit changed(key);
    return true;
}

void Settings::setDesktop(const QString &key, const QJsonValue &v)
{
    desktop_[key] = v;
    write_json(dataDir_ + "/desktop.json", desktop_);
}

/* As the server's (settings.go saveSettings): version, as the schema's
   (without it the server takes the file for version 1's flat keys) */
bool Settings::save()
{
    values_["version"] = schema_["properties"].toObject()["version"].toObject()["default"];
    QDir().mkpath(dataDir_);
    return write_json(dataDir_ + "/settings.json", values_);
}
