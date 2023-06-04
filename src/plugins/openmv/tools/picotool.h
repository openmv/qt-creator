#ifndef PICOTOOL_H
#define PICOTOOL_H

#include <QtCore>
#include <QtWidgets>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>
#include <utils/hostosinfo.h>
#include <utils/qtcprocess.h>

#define PICOTOOL_SETTINGS_GROUP "OpenMVPICOTOOL"
#define LAST_PICOTOOL_TERMINAL_WINDOW_GEOMETRY "LastPICOTOOLTerminalWindowGeometry"

QList<QString> picotoolGetDevices();
void picotoolReset(QString &command, Utils::QtcProcess &process);
void picotoolDownloadFirmware(QString &command, Utils::QtcProcess &process, const QString &path, const QString &moreArgs = QString());

#endif // PICOTOOL_H
