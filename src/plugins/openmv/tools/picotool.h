#ifndef PICOTOOL_H
#define PICOTOOL_H

#include <QtCore>
#include <QtWidgets>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>
#include <utils/hostosinfo.h>
#include <utils/synchronousprocess.h>

#define PICOTOOL_SETTINGS_GROUP "OpenMVPICOTOOL"
#define LAST_PICOTOOL_TERMINAL_WINDOW_GEOMETRY "LastPICOTOOLTerminalWindowGeometry"

QList<QString> picotoolGetDevices();
void picotoolReset(QString &command, Utils::SynchronousProcess &process, Utils::SynchronousProcessResponse &response);
void picotoolDownloadFirmware(QString &command, Utils::SynchronousProcess &process, Utils::SynchronousProcessResponse &response, const QString &path, const QString &moreArgs = QString());

#endif // PICOTOOL_H
