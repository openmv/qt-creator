#ifndef IMX_H
#define IMX_H

#include <QtCore>
#include <QtWidgets>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>
#include <utils/hostosinfo.h>
#include <utils/synchronousprocess.h>

#define IMX_SETTINGS_GROUP "OpenMVIMX"
#define LAST_IMX_TERMINAL_WINDOW_GEOMETRY "LastIMXTerminalWindowGeometry"

bool imxGetDevice(QJsonObject &obj);
bool imxDownloadBootloaderAndFirmware(QJsonObject &obj);
bool imxDownloadFirmware(QJsonObject &obj);

#endif // IMX_H
