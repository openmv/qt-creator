#ifndef BOSSAC_H
#define BOSSAC_H

#include <QList>
#include <QString>

#include <utils/qtcprocess.h>

void bossacRunBootloader(Utils::QtcProcess &process, const QString &device);
void bossacDownloadFirmware(QString &command, Utils::QtcProcess &process, const QString &path, const QString &device, const QString &moreArgs = QString());

#endif // BOSSAC_H
