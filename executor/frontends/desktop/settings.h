#pragma once
/*
 * settings.h - the desktop application's settings: settings.json in the
 * data folder, in the format of the tab's and its server's
 * (settings.schema.json, compiled in from executor/web; docs/executor/
 * Settings.md), the page-scoped ones (x-scope: page) being the desktop's.
 * Defaults, types, ranges, labels and help are the schema's.
 *
 * disks.mounted holds the disks in the Mac (image paths here, library
 * keys in the tab) and machine.system the System's image (empty:
 * Executor's own). What only the desktop has (the window, library
 * folders) is in desktop.json beside it, so settings.json stays valid for
 * the schema.
 */
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QStringList>

class Settings : public QObject
{
    Q_OBJECT
public:
    explicit Settings(const QString &dataDir, QObject *parent = nullptr);

    /* "group.name" */
    QJsonValue get(const QString &key) const;
    QString str(const QString &key) const { return get(key).toString(); }
    int integer(const QString &key) const { return get(key).toInt(); }
    bool boolean(const QString &key) const { return get(key).toBool(); }
    QStringList list(const QString &key) const;
    /* Changed: changed(key), saved. */
    void set(const QString &key, const QJsonValue &v);

    /* The schema: the groups in order, each one's properties in order */
    const QJsonObject &schema() const { return schema_; }
    QStringList groups() const { return groupOrder_; }
    QStringList properties(const QString &group) const { return propertyOrder_.value(group); }
    QJsonObject group(const QString &group) const;
    QJsonObject property(const QString &key) const;
    bool isPage(const QString &key) const { return property(key).value("x-scope").toString() == "page"; }
    /* x-apply: "live" or "executor" (Executor restarts) */
    bool live(const QString &key) const { return property(key).value("x-apply").toString() == "live"; }

    /* desktop.json: what only the desktop application has */
    QJsonValue desktop(const QString &key) const { return desktop_.value(key); }
    void setDesktop(const QString &key, const QJsonValue &v);

    QString dataDir() const { return dataDir_; }
    bool save();

signals:
    void changed(const QString &key);

private:
    QString dataDir_;
    QJsonObject schema_, values_, desktop_;
    QStringList groupOrder_;
    QMap<QString, QStringList> propertyOrder_;
};
