/*
 * disks.cpp - the library and working copies (disks.h).
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

#if defined(Q_OS_LINUX)
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <unistd.h>
#elif defined(Q_OS_MACOS)
#include <sys/clonefile.h>
#endif

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

/* A clone of FROM at TO where the file system makes them (no data
   copied), else false */
bool clone(const QString &from, const QString &to)
{
#if defined(Q_OS_LINUX)
    int in = ::open(QFile::encodeName(from).constData(), O_RDONLY);
    if(in < 0)
        return false;
    int out = ::open(QFile::encodeName(to).constData(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    bool ok = out >= 0 && ioctl(out, FICLONE, in) == 0;
    if(in >= 0)
        ::close(in);
    if(out >= 0)
        ::close(out);
    if(!ok)
        QFile::remove(to);
    return ok;
#elif defined(Q_OS_MACOS)
    return clonefile(QFile::encodeName(from).constData(), QFile::encodeName(to).constData(), 0) == 0;
#else
    (void)from;
    (void)to;
    return false;
#endif
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

/* overlays/NAME-HASH.img: HASH of the image's path, size and time */
QString Disks::workingCopy(const QString &image) const
{
    QFileInfo fi(image);
    QByteArray id = fi.absoluteFilePath().toUtf8() + '\0' + QByteArray::number(fi.size()) + '\0'
                    + QByteArray::number(fi.lastModified().toMSecsSinceEpoch());
    QString hash = QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex().left(12);
    return settings_.dataDir() + "/overlays/" + fi.completeBaseName() + "-" + hash + "." + fi.suffix();
}

QString Disks::mountPath(const QString &image, QString *error)
{
    if(isCD(image))
        return image;
    QString copy = workingCopy(image);
    if(QFileInfo::exists(copy))
        return copy;
    QDir().mkpath(QFileInfo(copy).path());
    QString part = copy + ".part";
    QFile::remove(part);
    bool ok = clone(image, part) || QFile::copy(image, part);
    if(ok)
    {
        QFile::setPermissions(part, QFile::permissions(part) | QFileDevice::WriteOwner | QFileDevice::ReadOwner);
        ok = QFile::rename(part, copy);
    }
    if(!ok)
    {
        QFile::remove(part);
        if(error)
            *error = "can't make the working copy " + copy;
        return {};
    }
    return copy;
}

QString Disks::imageFor(const QString &mountPath) const
{
    for(const DiskInfo &d : library())
        if(d.path == mountPath || workingCopy(d.path) == mountPath)
            return d.path;
    return mountPath;
}

bool Disks::hasChanges(const QString &image) const
{
    return !isCD(image) && QFileInfo::exists(workingCopy(image));
}

bool Disks::revert(const QString &image)
{
    return QFile::remove(workingCopy(image));
}

bool Disks::additionsFor(const QString &disk, const QString &system) const
{
    if(system.isEmpty())
        return false;
    DiskInfo s = info(system), d = info(disk);
    return s.systems.size() == 1 && d.systems.contains(s.systems[0]);
}
