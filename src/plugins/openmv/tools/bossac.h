#ifndef BOSSAC_H
#define BOSSAC_H

#include <QList>
#include <QString>

#include <utils/qtcprocess.h>

namespace OpenMV {
namespace Internal {

void bossacRunBootloader(Utils::QtcProcess &process, const QString &device);
void bossacDownloadFirmware(const QString &details, QString &command, Utils::QtcProcess &process, const QString &path, const QString &device, const QString &moreArgs = QString());

} // namespace Internal
} // namespace OpenMV

#endif // BOSSAC_H
