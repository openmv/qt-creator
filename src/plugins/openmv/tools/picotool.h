#ifndef PICOTOOL_H
#define PICOTOOL_H

#include <QList>
#include <QString>

#include <utils/qtcprocess.h>

namespace OpenMV {
namespace Internal {

QList<QString> picotoolGetDevices();
void picotoolReset(QString &command, Utils::Process &process);
void picotoolDownloadFirmware(const QString &details, QString &command, Utils::Process &process, const QString &path, const QString &moreArgs = QString());

} // namespace Internal
} // namespace OpenMV

#endif // PICOTOOL_H
