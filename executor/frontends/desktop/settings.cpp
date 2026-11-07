/*
 * settings.cpp - settings.json and desktop.json (settings.h).
 */
#include "settings.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

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

void Settings::set(const QString &key, const QJsonValue &v)
{
    if(get(key) == v)
        return;
    QString g = key.section('.', 0, 0), p = key.section('.', 1);
    QJsonObject group = values_[g].toObject();
    group[p] = v;
    values_[g] = group;
    values_["version"] = schema_["properties"].toObject()["version"].toObject()["default"];
    save();
    emit changed(key);
}

void Settings::setDesktop(const QString &key, const QJsonValue &v)
{
    desktop_[key] = v;
    write_json(dataDir_ + "/desktop.json", desktop_);
}

bool Settings::save()
{
    QDir().mkpath(dataDir_);
    return write_json(dataDir_ + "/settings.json", values_);
}
