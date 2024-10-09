#ifndef VIDEOTOOLS_H
#define VIDEOTOOLS_H

#include <QString>

#define VIDEO_RECORDER_FRAME_RATE 30

namespace OpenMV {
namespace Internal {

void convertVideoFileAction(const QString &drivePath);
void playVideoFileAction(const QString &drivePath);
void playRTSPStreamAction();
void saveVideoFile(const QString &srcPath);

} // namespace Internal
} // namespace OpenMV

#endif // VIDEOTOOLS_H
