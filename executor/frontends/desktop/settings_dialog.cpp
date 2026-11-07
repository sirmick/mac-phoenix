/*
 * settings_dialog.cpp - the Settings dialog from the schema (settings_dialog.h).
 */
#include "settings_dialog.h"
#include "settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <climits>

namespace {

QString label_of(const QJsonValue &v)
{
    if(v.isBool())
        return v.toBool() ? "On" : "Off";
    if(v.isDouble())
        return QString::number(v.toDouble());
    return v.toString();
}

/* A combo box over VALUES (labels LABELS, else the values), at CURRENT
   (added if it isn't one of them) */
QComboBox *combo(const QJsonArray &values, const QJsonArray &labels, const QJsonValue &current)
{
    auto *c = new QComboBox;
    for(int i = 0; i < values.size(); i++)
        c->addItem(i < labels.size() ? labels[i].toString() : label_of(values[i]), values[i].toVariant());
    int at = c->findData(current.toVariant());
    if(at < 0)
    {
        c->addItem(label_of(current), current.toVariant());
        at = c->count() - 1;
    }
    c->setCurrentIndex(at);
    return c;
}

}

SettingsDialog::SettingsDialog(Settings &settings, Choices choices, QWidget *parent)
    : QDialog(parent), settings_(settings), choices_(std::move(choices))
{
    setWindowTitle("Settings");
    auto *tabs = new QTabWidget;
    for(const QString &g : settings_.groups())
    {
        auto *page = new QWidget;
        auto *form = new QFormLayout(page);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        int n = 0;
        for(const QString &p : settings_.properties(g))
        {
            QString key = g + "." + p;
            if(!settings_.isPage(key))
                continue;
            QWidget *w = editor(key);
            if(!w)
                continue;
            QJsonObject prop = settings_.property(key);
            QString help = prop.value("description").toString();
            if(prop.value("x-apply").toString() == "executor")
                help += " (Executor restarts.)";
            auto *box = new QWidget;
            auto *v = new QVBoxLayout(box);
            v->setContentsMargins(0, 0, 0, 0);
            v->addWidget(w);
            auto *note = new QLabel(help);
            note->setWordWrap(true);
            note->setEnabled(false);
            v->addWidget(note);
            form->addRow(prop.value("title").toString() + ":", box);
            n++;
        }
        if(n)
            tabs->addTab(page, settings_.group(g).value("title").toString());
        else
            delete page;
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { apply(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SettingsDialog::apply);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
    resize(640, 520);
}

/* The widget for KEY and its reader, or none (a property shown elsewhere) */
QWidget *SettingsDialog::editor(const QString &key)
{
    QJsonObject prop = settings_.property(key);
    QJsonValue current = settings_.get(key);
    QString widget = prop.value("x-widget").toString();
    QString type = prop.value("type").toString();
    if(widget == "disk-list" || prop.contains("x-shown-in") || prop.value("readOnly").toBool())
        return nullptr;

    if(prop.contains("x-choices"))
    {
        auto *c = new QComboBox;
        for(const auto &[value, label] : choices_(prop.value("x-choices").toString()))
            c->addItem(label, value);
        int at = c->findData(current.toString());
        if(at < 0)
        {
            c->addItem(current.toString(), current.toString());
            at = c->count() - 1;
        }
        c->setCurrentIndex(at);
        readers_[key] = [c] { return QJsonValue(c->currentData().toString()); };
        return c;
    }
    if(widget == "screen")
    {
        /* the examples, Custom… (typed in), and Auto: the auto setting
           (display.fit_window), the window's size */
        auto *box = new QWidget;
        auto *h = new QHBoxLayout(box);
        h->setContentsMargins(0, 0, 0, 0);
        auto *c = combo(prop.value("examples").toArray(), prop.value("x-labels").toArray(), current);
        c->setEditable(true);
        c->setInsertPolicy(QComboBox::NoInsert);
        h->addWidget(c, 1);
        QString autoKey = prop.value("x-auto-setting").toString();
        QCheckBox *fit = nullptr;
        if(!autoKey.isEmpty())
        {
            fit = new QCheckBox("Auto (" + settings_.property(autoKey).value("description").toString() + ")");
            fit->setChecked(settings_.boolean(autoKey));
            c->setEnabled(!fit->isChecked());
            connect(fit, &QCheckBox::toggled, c, [c](bool on) { c->setEnabled(!on); });
            h->addWidget(fit);
            readers_[autoKey] = [fit] { return QJsonValue(fit->isChecked()); };
        }
        QRegularExpression re(prop.value("pattern").toString());
        readers_[key] = [c, re, current] {
            QString t = c->currentText();
            QString v = c->findText(t) >= 0 ? c->itemData(c->findText(t)).toString() : t.remove(' ').replace("×", "x");
            return re.match(v).hasMatch() ? QJsonValue(v) : current;
        };
        return box;
    }
    if(prop.contains("x-auto") && prop.value("x-auto").toObject().contains("choices"))
    {
        QJsonObject a = prop.value("x-auto").toObject();
        QJsonArray values{ a.value("value") }, labels{ "Auto" };
        for(const QJsonValue &v : a.value("choices").toArray())
            values.append(v);
        for(const QJsonValue &l : a.value("labels").toArray())
            labels.append(l);
        auto *c = combo(values, labels, current);
        readers_[key] = [c] { return QJsonValue::fromVariant(c->currentData()); };
        return c;
    }
    if(prop.contains("enum"))
    {
        auto *c = combo(prop.value("enum").toArray(), prop.value("x-labels").toArray(), current);
        readers_[key] = [c] { return QJsonValue::fromVariant(c->currentData()); };
        return c;
    }
    if(type == "boolean")
    {
        auto *b = new QCheckBox;
        b->setChecked(current.toBool());
        readers_[key] = [b] { return QJsonValue(b->isChecked()); };
        return b;
    }
    if(type == "integer" && widget == "range")
    {
        auto *box = new QWidget;
        auto *h = new QHBoxLayout(box);
        h->setContentsMargins(0, 0, 0, 0);
        auto *s = new QSlider(Qt::Horizontal);
        s->setRange(prop.value("minimum").toInt(), prop.value("maximum").toInt());
        s->setValue(current.toInt());
        auto *l = new QLabel(QString::number(current.toInt()));
        connect(s, &QSlider::valueChanged, l, [l](int v) { l->setNum(v); });
        h->addWidget(s, 1);
        h->addWidget(l);
        readers_[key] = [s] { return QJsonValue(s->value()); };
        return box;
    }
    if(type == "integer")
    {
        auto *s = new QSpinBox;
        s->setRange(prop.value("minimum").toInt(INT_MIN), prop.value("maximum").toInt(INT_MAX));
        s->setValue(current.toInt());
        readers_[key] = [s] { return QJsonValue(s->value()); };
        return s;
    }
    if(type == "string")
    {
        auto *e = new QLineEdit(current.toString());
        e->setMaxLength(prop.value("maxLength").toInt(32767));
        readers_[key] = [e] { return QJsonValue(e->text()); };
        return e;
    }
    return nullptr;
}

void SettingsDialog::apply()
{
    QStringList keys;
    for(const auto &[key, read] : readers_)
    {
        QJsonValue v = read();
        if(v != settings_.get(key))
        {
            settings_.set(key, v);
            keys << key;
        }
    }
    if(!keys.isEmpty())
        emit applied(keys);
}
