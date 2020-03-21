#ifndef DFU_UTIL_H
#define DFU_UTIL_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <coreplugin/icore.h>
#include <utils/hostosinfo.h>
#include <utils/synchronousprocess.h>

bool downloadFirmware(const QString &path);

#endif // DFU_UTIL_H
