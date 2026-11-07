#pragma once
/*
 * log_capture.h - Executor's log (its stderr) for the Log view and a file.
 *
 * stderr becomes a pipe; a thread reads it a line at a time, writes each
 * to the original stderr and the log file (logs/executor-DATE.log in the
 * data folder) and hands it to line() on the GUI thread.
 */
#include <QObject>
#include <QString>

class LogCapture : public QObject
{
    Q_OBJECT
public:
    static LogCapture &instance();
    /* From now on; FILE the log file (empty: none). False: not captured. */
    bool start(const QString &file);

signals:
    void line(QString text);
};
