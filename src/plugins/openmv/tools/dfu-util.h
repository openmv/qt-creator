#ifndef DFU_UTIL_H
#define DFU_UTIL_H

#include <QtCore>
#include <QtWidgets>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>
#include <utils/hostosinfo.h>
#include <utils/qtcprocess.h>

#define DFU_UTIL_SETTINGS_GROUP "OpenMVDFUUtil"
#define LAST_DFU_UTIL_TERMINAL_WINDOW_GEOMETRY "LastDFUUtilTerminalWindowGeometry"

QList<QString> getDevices();
void downloadFirmware(QString &command, Utils::QtcProcess &process, const QString &path, const QString &device, const QString &moreArgs = QString());

#endif // DFU_UTIL_H
