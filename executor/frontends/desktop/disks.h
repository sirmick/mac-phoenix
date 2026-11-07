#pragma once
/*
 * disks.h - disk images: the library, and their changes kept apart.
 *
 * The library is the images the user added (Add…, dropped on the window)
 * and those in library folders (desktop.json "library"), with what their
 * records say (NAME.json beside them, tools/web/make_library.py's: title,
 * note, system, systems). An image itself is never written: the Mac's
 * changes go to an overlay in the data folder's overlays/ (KEY.overlay,
 * KEY from the image's name, path, size and time, so a changed image
 * starts afresh), the changed 4 KB blocks in executor/server's format
 * (host/overlay.h), through the opener main.cpp gives the File Manager
 * (hfs/image_io.h). Revert removes it. CDs (.iso, .toast, .cdr) are
 * mounted locked, as they are, with no overlay.
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

    /* IMAGE's overlay in DATA's overlays/ (none for a CD: empty) */
    static QString overlayPath(const QString &data, const QString &image);
    bool hasChanges(const QString &image) const;
    /* The overlay removed: the image as it was. Not while mounted. */
    bool revert(const QString &image);

    /* Whether DISK's System Folder additions go in on SYSTEM (main.js
       additionsFor: the System disk names one System, the disk is for it) */
    bool additionsFor(const QString &disk, const QString &system) const;

signals:
    void libraryChanged();

private:
    Settings &settings_;
};
