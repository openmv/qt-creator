#ifndef DFU_UTIL_H
#define DFU_UTIL_H

#include <QtCore>

#include <coreplugin/icore.h>
#include <utils/hostosinfo.h>
#include <utils/synchronousprocess.h>

void downloadFirmware(QString &command, Utils::SynchronousProcess &process, Utils::SynchronousProcessResponse &response, const QString &path, const QString &device, const QString &moreArgs = QString());

#endif // DFU_UTIL_H
