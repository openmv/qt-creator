#ifndef PICOTOOL_H
#define PICOTOOL_H

#include <QList>
#include <QString>

#include <utils/qtcprocess.h>

QList<QString> picotoolGetDevices();
void picotoolReset(QString &command, Utils::QtcProcess &process);
void picotoolDownloadFirmware(QString &command, Utils::QtcProcess &process, const QString &path, const QString &moreArgs = QString());

#endif // PICOTOOL_H
