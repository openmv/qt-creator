#ifndef IMX_H
#define IMX_H

#include <QList>
#include <QJsonObject>
#include <QPair>

QList<QPair<int, int> > imxVidPidList(bool spd_host = true, bool bl_host = true);
// Returns PID/VID of SPD and BL bootloaders on the system.
QStringList imxGetAllDevices(bool spd_host = true, bool bl_host = true);
// Returns true if the BL bootloader is present.
bool imxGetDevice(QJsonObject &obj);
bool imxDownloadBootloaderAndFirmware(QJsonObject &obj, bool forceFlashFSErase, bool justEraseFlashFs);
bool imxDownloadFirmware(QJsonObject &obj, bool forceFlashFSErase, bool justEraseFlashFs);

#endif // IMX_H
