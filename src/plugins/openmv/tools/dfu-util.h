#ifndef DFU_UTIL_H
#define DFU_UTIL_H

#include <QList>
#include <QString>

#include <utils/qtcprocess.h>

QList<QString> getDevices();
void downloadFirmware(QString &command, Utils::QtcProcess &process, const QString &path, const QString &device, const QString &moreArgs = QString());

#endif // DFU_UTIL_H
