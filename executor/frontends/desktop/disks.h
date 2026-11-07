#pragma once
/*
 * disks.h - disk images: the library, and their changes kept apart.
 *
 * The library is the images the user added (Add…, dropped on the window)
 * and those in library folders (desktop.json "library"), with what their
 * records say (NAME.json beside them, tools/web/make_library.py's: title,
 * note, system, systems). An image itself is never written: the Mac
 * writes a working copy in the data folder's overlays/ (KEY.img, KEY from
 * the image's path, size and time, so a changed image starts afresh),
 * made the first time it is mounted (a clone where the file system has
 * them: Linux FICLONE, macOS clonefile; a copy elsewhere). Revert
 * removes it. CDs (.iso, .toast, .cdr) are mounted locked, as they are.
 */
#include <QObject>
#include <QStringList>

class Settings;

struct DiskInfo
{
    QString path;       // the image
    QString title, note;
    QString system;     // the System on it ("8.6"), if it is a System disk
    QStringList systems;  // the Systems its applications are for
    bool cd = false;
};

class Disks : public QObject
{
    Q_OBJECT
public:
    explicit Disks(Settings &settings, QObject *parent = nullptr);

    static bool isImage(const QString &path);
    static bool isCD(const QString &path);
    DiskInfo info(const QString &path) const;

    /* The library: images added and those in the library folders */
    QList<DiskInfo> library() const;
    void addImage(const QString &path);
    void addFolder(const QString &folder);
    void removeImage(const QString &path);

    /* What the Mac mounts for IMAGE: its working copy (made now if there
       is none: blocking) or the image itself (a CD). Empty: no copy. */
    QString mountPath(const QString &image, QString *error = nullptr);
    /* The image a mount path is the working copy of (or the path) */
    QString imageFor(const QString &mountPath) const;
    bool hasChanges(const QString &image) const;
    /* The working copy removed: the image as it was. Not while mounted. */
    bool revert(const QString &image);

    /* Whether DISK's System Folder additions go in on SYSTEM (main.js
       additionsFor: the System disk names one System, the disk is for it) */
    bool additionsFor(const QString &disk, const QString &system) const;

signals:
    void libraryChanged();

private:
    QString workingCopy(const QString &image) const;
    Settings &settings_;
};
