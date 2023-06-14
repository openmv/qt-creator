#ifndef IMX_H
#define IMX_H

#include <QJsonObject>

bool imxGetDevice(QJsonObject &obj);
bool imxDownloadBootloaderAndFirmware(QJsonObject &obj, bool forceFlashFSErase, bool justEraseFlashFs);
bool imxDownloadFirmware(QJsonObject &obj, bool forceFlashFSErase, bool justEraseFlashFs);

#endif // IMX_H
