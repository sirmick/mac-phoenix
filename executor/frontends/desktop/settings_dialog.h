#pragma once
/*
 * settings_dialog.h - the Settings dialog, made from the schema as the
 * tab's settings.js makes its own: a tab a group, the page-scoped
 * properties in the schema's order, each one's widget from its type and
 * x- keywords (x-auto, x-labels, x-widget, x-choices), its title, its
 * description under it, and "Executor restarts" for x-apply: executor.
 * disks.mounted is the Disks panel's.
 */
#include <QDialog>
#include <QJsonValue>
#include <QList>
#include <QPair>

#include <functional>
#include <map>

class Settings;
class QWidget;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    /* x-choices: value and label pairs the application fills in ("systems") */
    using Choices = std::function<QList<QPair<QString, QString>>(const QString &name)>;
    SettingsDialog(Settings &settings, Choices choices, QWidget *parent = nullptr);

signals:
    /* Applied: the keys whose value changed (saved already) */
    void applied(const QStringList &keys);

private:
    QWidget *editor(const QString &key);
    void apply();

    Settings &settings_;
    Choices choices_;
    std::map<QString, std::function<QJsonValue()>> readers_;  // key -> the widget's value
};
