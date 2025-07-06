/* Copyright (C) 2023-2024 OpenMV, LLC.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Any redistribution, use, or modification in source or binary form
 *    is done solely for personal benefit and not for any commercial
 *    purpose or for monetary gain. For commercial licensing options,
 *    please contact openmv@openmv.io
 *
 * THIS SOFTWARE IS PROVIDED BY THE LICENSOR AND COPYRIGHT OWNER "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE LICENSOR OR COPYRIGHT
 * OWNER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef OPENMVPLUGINFB_H
#define OPENMVPLUGINFB_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

namespace OpenMV {
namespace Internal {

class OpenMVPluginFB : public QGraphicsView
{
    Q_OBJECT

public:

    explicit OpenMVPluginFB(QWidget *parent = Q_NULLPTR);
    bool pixmapValid() const;
    QPixmap pixmap() const;
    bool beginImageWriter();
    void endImageWriter();
    void enableInteraction(bool enable) { m_enableInteraction = enable; }

public slots:

    void enableFitInView(bool enable);
    void fbMessage(const QString &message);
    void frameBufferData(const QPixmap &data);
    void enableSaveTemplate(bool enable) { m_enableSaveTemplate = enable; }
    void enableSaveDescriptor(bool enable) { m_enableSaveDescriptor = enable; }
    void focusMetric(int metric) { m_focusMetric = metric; }
    void private_timerCallBack();

signals:

    void pixmapUpdate(const QPixmap &data);
    void resolutionAndROIUpdate(const QSize &res, const QRect &roi, int focus);
    void saveImage(const QPixmap &data);
    void saveTemplate(const QRect &rect);
    void saveDescriptor(const QRect &rect);
    void imageWriterTick(const QString &text);
    void imageWriterShutdown();

protected:

    virtual void mousePressEvent(QMouseEvent *event);
    virtual void mouseMoveEvent(QMouseEvent *event);
    virtual void mouseReleaseEvent(QMouseEvent *event);
    virtual void contextMenuEvent(QContextMenuEvent *event);
    virtual void resizeEvent(QResizeEvent *event);

private:

    QRect getROI();
    QPixmap getPixmap(bool pointValid = false, const QPoint &point = QPoint(), bool *cropped = Q_NULLPTR, QRect *croppedRect = Q_NULLPTR);
    void myFitInView(QGraphicsPixmapItem *item);

    bool m_enableFitInView;
    QGraphicsPixmapItem *m_pixmap;
    bool m_enableSaveTemplate;
    bool m_enableSaveDescriptor;
    bool m_enableInteraction;
    int m_focusMetric;

    bool m_unlocked;
    QPoint m_origin;
    QRubberBand *m_band;

    QTimer *m_timer;
    QTemporaryFile *m_tempFile;
    QElapsedTimer m_elaspedTimer;
    QQueue<qint64> m_previousElaspedTimers;

    void broadcastUpdate();
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVPLUGINFB_H
