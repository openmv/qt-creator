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

protected:

    bool eventFilter(QObject *watched, QEvent *event);

private:

    void updatePlot(QCPGraph *graph, int channel);

    int m_colorSpace;
    QPixmap m_pixmap;

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

    Ui::OpenMVPluginHistogram *m_ui;
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVPLUGINHISTOGRAM_H
