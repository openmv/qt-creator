#ifndef VIDEOTOOLS_H
#define VIDEOTOOLS_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>
#include <utils/hostosinfo.h>
#include <utils/qtcprocess.h>

#include "../openmvpluginio.h"

#define VIDEO_SETTINGS_GROUP "OpenMVFFMPEG"
#define LAST_CONVERT_VIDEO_SRC_PATH "LastConvertSrcPath"
#define LAST_CONVERT_VIDEO_DST_PATH "LastConvertDstPath"
#define LAST_CONVERT_VIDEO_DST_FOLDER_PATH "LastConvertDstFolderPath"
#define LAST_CONVERT_VIDEO_DST_EXTENSION "LastConvertDstExtensionPath"
#define LAST_CONVERT_VIDEO_HRES "LastConvertVideoHRes"
#define LAST_CONVERT_VIDEO_SKIP "LastConvertVideoSkip"
#define LAST_PLAY_VIDEO_PATH "LastPlayVideoPath"
#define LAST_PLAY_RTSP_URL "LastPlayVideoUrl"
#define LAST_PLAY_RTSP_PORT "LastPlayVideoPort"
#define LAST_PLAY_RTSP_TCP "LastPlayVideoTCP"
#define LAST_SAVE_VIDEO_PATH "LastSaveVideoPath"
#define LAST_SAVE_VIDEO_HRES "LastSaveVideoHRes"
#define LAST_SAVE_VIDEO_SKIP "LastSaveVideoSkip"
#define LAST_CONVERT_TERMINAL_WINDOW_GEOMETRY "LastConvertTerminalWindowGeometry"
#define LAST_PLAY_TERMINAL_WINDOW_GEOMETRY "LastPlayTerminalWindowGeometry"

#define VIDEO_RECORDER_FRAME_RATE 30

void convertVideoFileAction(const QString &drivePath);
void playVideoFileAction(const QString &drivePath);
void playRTSPStreamAction();
void saveVideoFile(const QString &srcPath);

#endif // VIDEOTOOLS_H
