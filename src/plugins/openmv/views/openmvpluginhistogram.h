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

#ifndef OPENMVPLUGINHISTOGRAM_H
#define OPENMVPLUGINHISTOGRAM_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include "../qcustomplot/qcustomplot.h"

#define RGB_COLOR_SPACE 0
#define GRAYSCALE_COLOR_SPACE 1
#define LAB_COLOR_SPACE 2
#define YUV_COLOR_SPACE 3

namespace Ui
{
    class OpenMVPluginHistogram;
}

namespace OpenMV {
namespace Internal {

class OpenMVPluginHistogram : public QWidget
{
    Q_OBJECT

public:

    explicit OpenMVPluginHistogram(QWidget *parent = Q_NULLPTR);
    ~OpenMVPluginHistogram();

public slots:

    void colorSpaceChanged(int colorSpace);
    void pixmapUpdate(const QPixmap &data);

signals:

    void focusMetric(int metric);

protected:

    bool eventFilter(QObject *watched, QEvent *event);
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:

    void updatePlot(QCPGraph *graph, int channel);
    void catchUp();

    int m_colorSpace;
    QPixmap m_pixmap;
    // Latest frame received while the widget couldn't be seen (page hidden
    // behind another pane view, or pane collapsed to zero) -- processed on
    // show/expand so switching back always displays current data.
    QPixmap m_pendingPixmap;

    int m_mean;
    int m_median;
    int m_mode;
    int m_standardDeviation;
    int m_min;
    int m_max;
    int m_lowerQuartile;
    int m_upperQuartile;

    QCPGraph *m_channel0;
    QCPGraph *m_channel1;
    QCPGraph *m_channel2;

    // Per-graph exponentially-averaged plot data (display only; the stats readouts stay raw).
    // Camera noise makes bin counts flicker frame to frame, so without this the curve's peaks
    // visibly snap around between near-equal bumps.
    QMap<QCPGraph *, QVector<double>> m_smoothedYs;

    Ui::OpenMVPluginHistogram *m_ui;
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVPLUGINHISTOGRAM_H
