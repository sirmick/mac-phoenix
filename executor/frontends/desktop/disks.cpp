/*
 * disks.cpp - the library and overlays (disks.h).
 */
#include "disks.h"
#include "settings.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>


namespace {

const QRegularExpression IMAGE("\\.(img|image|dsk|hfs|iso|toast|cdr)$", QRegularExpression::CaseInsensitiveOption);
const QRegularExpression CD("\\.(iso|toast|cdr)$", QRegularExpression::CaseInsensitiveOption);

QStringList strings(const QJsonValue &v)
{
    QStringList out;
    for(const QJsonValue &s : v.toArray())
        out << s.toString();
    return out;
}


}

Disks::Disks(Settings &settings, QObject *parent) : QObject(parent), settings_(settings) {}

bool Disks::isImage(const QString &path) { return IMAGE.match(path).hasMatch(); }
bool Disks::isCD(const QString &path) { return CD.match(path).hasMatch(); }

DiskInfo Disks::info(const QString &path) const
{
    DiskInfo d;
    d.path = path;
    d.cd = isCD(path);
    QFile f(path + ".json");
    QJsonObject rec;
    if(f.open(QIODevice::ReadOnly))
        rec = QJsonDocument::fromJson(f.readAll()).object();
    d.title = rec.value("title").toString(QFileInfo(path).fileName());
    d.note = rec.value("note").toString();
    d.system = rec.value("system").toString();
    d.systems = strings(rec.value("systems"));
    return d;
}

QList<DiskInfo> Disks::library() const
{
    QStringList paths = strings(settings_.desktop("images"));
    for(const QString &folder : strings(settings_.desktop("library")))
        for(const QFileInfo &fi : QDir(folder).entryInfoList(QDir::Files, QDir::Name))
            if(isImage(fi.fileName()))
                paths << fi.absoluteFilePath();
    paths.removeDuplicates();
    QList<DiskInfo> out;
    for(const QString &p : paths)
        if(QFileInfo::exists(p))
            out << info(p);
    return out;
}

void Disks::addImage(const QString &path)
{
    QStringList images = strings(settings_.desktop("images"));
    QString p = QFileInfo(path).absoluteFilePath();
    if(images.contains(p))
        return;
    images << p;
    settings_.setDesktop("images", QJsonArray::fromStringList(images));
    emit libraryChanged();
}

void Disks::addFolder(const QString &folder)
{
    QStringList folders = strings(settings_.desktop("library"));
    QString p = QFileInfo(folder).absoluteFilePath();
    if(folders.contains(p))
        return;
    folders << p;
    settings_.setDesktop("library", QJsonArray::fromStringList(folders));
    emit libraryChanged();
}

void Disks::removeImage(const QString &path)
{
    QStringList images = strings(settings_.desktop("images"));
    if(images.removeAll(path))
    {
        settings_.setDesktop("images", QJsonArray::fromStringList(images));
        emit libraryChanged();
    }
}

/* overlays/NAME-HASH.overlay: HASH of the image's path, size and time */
QString Disks::overlayPath(const QString &data, const QString &image)
{
    if(isCD(image))
        return {};
    QFileInfo fi(image);
    QByteArray id = fi.absoluteFilePath().toUtf8() + '\0' + QByteArray::number(fi.size()) + '\0'
                    + QByteArray::number(fi.lastModified().toMSecsSinceEpoch());
    QString hash = QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex().left(12);
    return data + "/overlays/" + fi.completeBaseName() + "-" + hash + ".overlay";
}

bool Disks::hasChanges(const QString &image) const
{
    QString o = overlayPath(settings_.dataDir(), image);
    return !o.isEmpty() && QFileInfo::exists(o);
}

bool Disks::revert(const QString &image)
{
    QString o = overlayPath(settings_.dataDir(), image);
    return !o.isEmpty() && QFile::remove(o);
}

bool Disks::additionsFor(const QString &disk, const QString &system) const
{
    if(system.isEmpty())
        return false;
    DiskInfo s = info(system), d = info(disk);
    return s.systems.size() == 1 && d.systems.contains(s.systems[0]);
}
