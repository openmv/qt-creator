#ifndef VIDEOTOOLS_H
#define VIDEOTOOLS_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <utils/hostosinfo.h>
#include <utils/synchronousprocess.h>

#include "../openmvpluginio.h"

#define VIDEO_SETTINGS_GROUP "OpenMVFFMPEG"
#define LAST_CONVERT_VIDEO_SRC_PATH "LastConvertSrcPath"
#define LAST_CONVERT_VIDEO_DST_PATH "LastConvertDstPath"
#define LAST_CONVERT_VIDEO_HRES "LastConvertVideoHRes"
#define LAST_PLAY_VIDEO_PATH "LastPlayVideoPath"
#define LAST_SAVE_VIDEO_PATH "LastSaveVideoPath"
#define LAST_SAVE_VIDEO_HRES "LastSaveVideoHRes"

#define VIDEO_RECORDER_FRAME_RATE 30

void convertVideoFileAction(const QString &drivePath);
void playVideoFileAction(const QString &drivePath);
void saveVideoFile(const QString &srcPath);

#endif // VIDEOTOOLS_H
