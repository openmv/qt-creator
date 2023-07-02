#ifndef DFU_UTIL_H
#define DFU_UTIL_H

#include <QList>
#include <QString>

#include <utils/qtcprocess.h>

namespace OpenMV {
namespace Internal {

QList<QString> getDevices();
void downloadFirmware(QString &command, Utils::QtcProcess &process, const QString &path, const QString &device, const QString &moreArgs = QString());

} // namespace Internal
} // namespace OpenMV

#endif // DFU_UTIL_H
