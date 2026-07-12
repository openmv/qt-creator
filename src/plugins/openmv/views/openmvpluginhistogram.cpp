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

#include "openmvpluginhistogram.h"
#include "ui_openmvpluginhistogram.h"

#include "openmvtr.h"

#include <cmath>

#include <utils/elidinglabel.h>
#include <utils/theme/theme.h>

#define RGB_COLOR_SPACE_R 0
#define RGB_COLOR_SPACE_G 1
#define RGB_COLOR_SPACE_B 2
#define GRAYSCALE_COLOR_SPACE_Y 3
#define LAB_COLOR_SPACE_L 4
#define LAB_COLOR_SPACE_A 5
#define LAB_COLOR_SPACE_B 6
#define YUV_COLOR_SPACE_Y 7
#define YUV_COLOR_SPACE_U 8
#define YUV_COLOR_SPACE_V 9

extern const uint8_t rb528_table[32];
extern const uint8_t g628_table[64];
extern const uint8_t rb825_table[256];
extern const uint8_t g826_table[256];

namespace OpenMV {
namespace Internal {

static inline int toR5(QRgb value)
{
    return rb825_table[qRed(value)]; // 0:255 -> 0:31
}

static inline int toG6(QRgb value)
{
    return g826_table[qGreen(value)]; // 0:255 -> 0:63
}

static inline int toB5(QRgb value)
{
    return rb825_table[qBlue(value)]; // 0:255 -> 0:31
}

// The 256-bin channels below convert straight from the full 8-bit RGB pixel.
// The firmware's own conversions run on RGB565, but quantizing the streamed
// RGB888 frame down to RGB565 first (the old lab_table/yuv_table path)
// collapses it to so few distinct values that the histograms comb into
// spikes with empty bins between. Grayscale, Y, U, and V use the firmware's
// integer weights (which sum to exactly neutral for gray pixels), so values
// still line up with on-camera thresholds.

static inline int toGrayscale(QRgb value)
{
    return ((qRed(value) * 38) + (qGreen(value) * 75) + (qBlue(value) * 15)) >> 7; // 0:255
}

// CIELAB support (sRGB, D65), on lookup tables so the per-pixel cost is a
// few multiplies: srgbToLinear() linearizes an 8-bit channel and labF() is
// the LAB transfer function f(t) sampled over t in [0:1].

static inline double srgbToLinear(int v)
{
    static double table[256];
    static bool init = false;

    if(!init)
    {
        for(int i = 0; i < 256; i++)
        {
            double s = i / 255.0;
            table[i] = (s <= 0.04045) ? (s / 12.92) : std::pow((s + 0.055) / 1.055, 2.4);
        }

        init = true;
    }

    return table[v];
}

#define LAB_F_LUT_SIZE 4096

static inline double labF(double t)
{
    static double table[LAB_F_LUT_SIZE];
    static bool init = false;

    if(!init)
    {
        for(int i = 0; i < LAB_F_LUT_SIZE; i++)
        {
            double x = i / double(LAB_F_LUT_SIZE - 1);
            table[i] = (x > (216.0 / 24389.0)) ? std::cbrt(x)
                                               : ((((24389.0 / 27.0) * x) + 16.0) / 116.0);
        }

        init = true;
    }

    return table[qBound(0, qRound(t * (LAB_F_LUT_SIZE - 1)), LAB_F_LUT_SIZE - 1)];
}

static inline int toL(QRgb value)
{
    double y = (0.2126729 * srgbToLinear(qRed(value))) +
               (0.7151522 * srgbToLinear(qGreen(value))) +
               (0.0721750 * srgbToLinear(qBlue(value)));

    return qBound(0, qRound((116.0 * labF(y)) - 16.0), 100); // 0:100
}

static inline int toA(QRgb value)
{
    double lr = srgbToLinear(qRed(value));
    double lg = srgbToLinear(qGreen(value));
    double lb = srgbToLinear(qBlue(value));

    double x = ((0.4124564 * lr) + (0.3575761 * lg) + (0.1805375 * lb)) / 0.95047;
    double y = (0.2126729 * lr) + (0.7151522 * lg) + (0.0721750 * lb);

    return qBound(0, qRound(500.0 * (labF(x) - labF(y))) + 128, 255); // 0:255
}

static inline int toB(QRgb value)
{
    double lr = srgbToLinear(qRed(value));
    double lg = srgbToLinear(qGreen(value));
    double lb = srgbToLinear(qBlue(value));

    double y = (0.2126729 * lr) + (0.7151522 * lg) + (0.0721750 * lb);
    double z = ((0.0193339 * lr) + (0.1191920 * lg) + (0.9503041 * lb)) / 1.08883;

    return qBound(0, qRound(200.0 * (labF(y) - labF(z))) + 128, 255); // 0:255
}

static inline int toY(QRgb value)
{
    return ((qRed(value) * 38) + (qGreen(value) * 75) + (qBlue(value) * 15)) >> 7; // 0:255
}

static inline int toU(QRgb value)
{
    return (((qRed(value) * -21) + (qGreen(value) * -43) + (qBlue(value) * 64)) >> 7) + 128; // 0:255
}

static inline int toV(QRgb value)
{
    return (((qRed(value) * 64) + (qGreen(value) * -54) + (qBlue(value) * -10)) >> 7) + 128; // 0:255
}

static inline int getValue(int value, int channel)
{
    switch(channel)
    {
        case RGB_COLOR_SPACE_R:
        {
            return rb528_table[value]; // 0:31 -> 0:255
        }
        case RGB_COLOR_SPACE_G:
        {
            return g628_table[value]; // 0:63 -> 0:255
        }
        case RGB_COLOR_SPACE_B:
        {
            return rb528_table[value]; // 0:31 -> 0:255
        }
        case GRAYSCALE_COLOR_SPACE_Y:
        {
            return value; // 0:255 -> 0:255
        }
        case LAB_COLOR_SPACE_L:
        {
            return value; // 0:100 -> 0:100
        }
        case LAB_COLOR_SPACE_A:
        {
            return value - 128; // 0:255 -> -128:127
        }
        case LAB_COLOR_SPACE_B:
        {
            return value - 128; // 0:255 -> -128:127
        }
        case YUV_COLOR_SPACE_Y:
        {
            return value; // 0:255 -> 0:255
        }
        case YUV_COLOR_SPACE_U:
        {
            return value - 128; // 0:255 -> -128:127
        }
        case YUV_COLOR_SPACE_V:
        {
            return value - 128; // 0:255 -> -128:127
        }
        default:
        {
            return value;
        }
    }
}

// Feed the graph a spline-smoothed version of the histogram bins so the plot draws as a smooth
// curve instead of a jagged point-to-point line (QCustomPlot has no native spline line style;
// smoothing the data gets the same look and the area fill follows for free). Monotone cubic
// interpolation (Fritsch-Carlson) rather than Catmull-Rom: it cannot overshoot the data, so
// peaks round off smoothly AT the bin height (no ringing above 1.0 that would clamp into a flat
// top) and valleys never dip below the axis. Subdivision adapts to the bin count so every
// channel gets ~512 plotted points (coarse 32-bin RGB channels smooth the most; 256-bin
// channels are already near pixel resolution).
static void addSmoothedData(QCPGraph *graph, const QVector<double> &xs, const QVector<double> &ys)
{
    const int n = xs.size();

    if (n < 3) {
        for (int i = 0; i < n; i++) {
            graph->addData(xs.at(i), ys.at(i));
        }
        return;
    }

    // Segment secants, then per-point tangents: zero at local extrema (this is what prevents
    // overshoot), averaged secants elsewhere, with the Fritsch-Carlson limiter keeping the
    // curve monotone within each segment.
    QVector<double> d(n - 1);
    for (int i = 0; i < n - 1; i++) {
        const double h = xs.at(i + 1) - xs.at(i);
        d[i] = h ? ((ys.at(i + 1) - ys.at(i)) / h) : 0.0;
    }

    QVector<double> m(n);
    m[0] = d.first();
    m[n - 1] = d.last();
    for (int i = 1; i < n - 1; i++) {
        m[i] = ((d.at(i - 1) * d.at(i)) <= 0.0) ? 0.0 : ((d.at(i - 1) + d.at(i)) / 2.0);
    }
    for (int i = 0; i < n - 1; i++) {
        if (d.at(i) == 0.0) {
            m[i] = 0.0;
            m[i + 1] = 0.0;
        } else {
            const double a = m.at(i) / d.at(i);
            const double b = m.at(i + 1) / d.at(i);
            const double s = (a * a) + (b * b);
            if (s > 9.0) {
                const double tau = 3.0 / sqrt(s);
                m[i] = tau * a * d.at(i);
                m[i + 1] = tau * b * d.at(i);
            }
        }
    }

    const int subdiv = qBound(2, 512 / n, 16);
    graph->addData(xs.first(), ys.first());

    for (int i = 0; i < n - 1; i++) {
        const double h = xs.at(i + 1) - xs.at(i);

        for (int s = 1; s <= subdiv; s++) {
            const double t = s / double(subdiv);
            const double t2 = t * t;
            const double t3 = t2 * t;
            // Cubic Hermite basis
            const double h00 = (2.0 * t3) - (3.0 * t2) + 1.0;
            const double h10 = t3 - (2.0 * t2) + t;
            const double h01 = (-2.0 * t3) + (3.0 * t2);
            const double h11 = t3 - t2;
            const double x = xs.at(i) + (t * h);
            const double y = (h00 * ys.at(i)) + (h10 * h * m.at(i))
                             + (h01 * ys.at(i + 1)) + (h11 * h * m.at(i + 1));
            graph->addData(x, y);
        }
    }
}

void OpenMVPluginHistogram::updatePlot(QCPGraph *graph, int channel)
{
    QImage image = m_pixmap.toImage();
    QVector<long long> vector;

    switch(channel)
    {
        case RGB_COLOR_SPACE_R:
        {
            vector.resize(32);
            vector.fill(0);

            for(int y = 0; y < image.height(); y++)
            {
                for(int x = 0; x < image.width(); x++)
                {
                    vector[toR5(image.pixel(x, y))]++;
                }
            }

            break;
        }
        case RGB_COLOR_SPACE_G:
        {
            vector.resize(64);
            vector.fill(0);

            for(int y = 0; y < image.height(); y++)
            {
                for(int x = 0; x < image.width(); x++)
                {
                    vector[toG6(image.pixel(x, y))]++;
                }
            }

            break;
        }
        case RGB_COLOR_SPACE_B:
        {
            vector.resize(32);
            vector.fill(0);

            for(int y = 0; y < image.height(); y++)
            {
                for(int x = 0; x < image.width(); x++)
                {
                    vector[toB5(image.pixel(x, y))]++;
                }
            }

            break;
        }
        case GRAYSCALE_COLOR_SPACE_Y:
        {
            vector.resize(256);
            vector.fill(0);

            for(int y = 0; y < image.height(); y++)
            {
                for(int x = 0; x < image.width(); x++)
                {
                    vector[toGrayscale(image.pixel(x, y))]++;
                }
            }

            break;
        }
        case LAB_COLOR_SPACE_L:
        {
            vector.resize(101);
            vector.fill(0);

            for(int y = 0; y < image.height(); y++)
            {
                for(int x = 0; x < image.width(); x++)
                {
                    vector[toL(image.pixel(x, y))]++;
                }
            }

            break;
        }
        case LAB_COLOR_SPACE_A:
        {
            vector.resize(256);
            vector.fill(0);

            for(int y = 0; y < image.height(); y++)
            {
                for(int x = 0; x < image.width(); x++)
                {
                    vector[toA(image.pixel(x, y))]++;
                }
            }

            break;
        }
        case LAB_COLOR_SPACE_B:
        {
            vector.resize(256);
            vector.fill(0);

            for(int y = 0; y < image.height(); y++)
            {
                for(int x = 0; x < image.width(); x++)
                {
                    vector[toB(image.pixel(x, y))]++;
                }
            }

            break;
        }
        case YUV_COLOR_SPACE_Y:
        {
            vector.resize(256);
            vector.fill(0);

            for(int y = 0; y < image.height(); y++)
            {
                for(int x = 0; x < image.width(); x++)
                {
                    vector[toY(image.pixel(x, y))]++;
                }
            }

            break;
        }
        case YUV_COLOR_SPACE_U:
        {
            vector.resize(256);
            vector.fill(0);

            for(int y = 0; y < image.height(); y++)
            {
                for(int x = 0; x < image.width(); x++)
                {
                    vector[toU(image.pixel(x, y))]++;
                }
            }

            break;
        }
        case YUV_COLOR_SPACE_V:
        {
            vector.resize(256);
            vector.fill(0);

            for(int y = 0; y < image.height(); y++)
            {
                for(int x = 0; x < image.width(); x++)
                {
                    vector[toV(image.pixel(x, y))]++;
                }
            }

            break;
        }
    }

    ///////////////////////////////////////////////////////////////////////////

    m_mean = 0;
    m_median = 0;
    m_mode = 0;
    m_standardDeviation = 0;
    m_min = 0;
    m_max = 0;
    m_lowerQuartile = 0;
    m_upperQuartile = 0;

    long long sum = 0;
    long long avg = 0;
    long long mode_count = 0;
    bool min_flag = false;

    for(int i = 0; i < vector.size(); i++)
    {
        int value = getValue(i, channel);

        sum += vector[i];
        avg += value * vector[i];

        if(vector[i] > mode_count)
        {
            mode_count = vector[i];
            m_mode = value;
        }

        if(vector[i] && (!min_flag))
        {
            min_flag = true;
            m_min = value;
        }

        if(vector[i])
        {
            m_max = value;
        }
    }

    m_mean = sum ? round(avg / double(sum)) : 0;

    long long lower_q = ((sum * 1) + 3) / 4; // 1/4th
    long long median = (sum + 1) / 2; // 1/2th
    long long upper_q = ((sum * 3) + 3) / 4; // 3/4th

    long long st_dev_count = 0;
    long long median_count = 0;

    for(int i = 0; i < vector.size(); i++)
    {
        int value = getValue(i, channel);

        st_dev_count += vector[i] * (value - m_mean) * (value - m_mean);

        if((median_count < lower_q) && (lower_q <= (median_count + vector[i])))
        {
            m_lowerQuartile = value;
        }

        if((median_count < median) && (median <= (median_count + vector[i])))
        {
            m_median = value;
        }

        if((median_count < upper_q) && (upper_q <= (median_count + vector[i])))
        {
            m_upperQuartile = value;
        }

        median_count += vector[i];
    }

    m_standardDeviation = sum ? round(sqrt(st_dev_count / double(sum))) : 0;

    ///////////////////////////////////////////////////////////////////////////

    graph->clearData();

    QVector<double> xs(vector.size());
    QVector<double> ys(vector.size());

    // Leave ~8% air above the tallest (mode) peak: y is normalized so the mode bin is the
    // maximum, and with the axis range pinned at [0, 1] that peak would kiss the plot ceiling
    // and read as clipped flat even though the spline rounds it.
    const double kHeadroom = 0.92;

    for(int i = 0; i < vector.size(); i++)
    {
        xs[i] = getValue(i, channel);
        ys[i] = mode_count ? (kHeadroom * vector[i] / double(mode_count)) : 0;
    }

    // Temporal smoothing (display only): camera noise flickers the bin counts every frame, and
    // mode-relative normalization rescales the whole curve whenever two near-equal bins swap
    // being the tallest -- so peaks visibly snap around. Blend each frame into an exponential
    // average so competing peaks crossfade instead. Alpha trades responsiveness for calm
    // (0.3 = a ~3-4 frame tail). A size mismatch means the color space changed; start fresh.
    const double kAlpha = 0.3;
    QVector<double> &prev = m_smoothedYs[graph];

    if(prev.size() == ys.size())
    {
        for(int i = 0; i < ys.size(); i++)
        {
            ys[i] = (kAlpha * ys[i]) + ((1.0 - kAlpha) * prev[i]);
        }
    }

    prev = ys;

    addSmoothedData(graph, xs, ys);
}

OpenMVPluginHistogram::OpenMVPluginHistogram(QWidget *parent) : QWidget(parent), m_colorSpace(RGB_COLOR_SPACE), m_pixmap(QPixmap()), m_ui(new Ui::OpenMVPluginHistogram)
{
    m_ui->setupUi(this);

    // Let the histogram pane compress. The stat labels are Utils::ElidingLabel in the .ui, but
    // our ElidingLabel keeps QLabel's Preferred policy (see the OPENMV-DIFF in elidinglabel.cpp),
    // so the grid would still refuse to shrink below the full text width -- Ignored lets the
    // columns compress and the text elide. Likewise the plots: QCustomPlot's minimumSizeHint
    // (axis rect + margins) floors the pane height/width unless the policy ignores it.
    const QList<Utils::ElidingLabel *> statLabels = findChildren<Utils::ElidingLabel *>();
    for (Utils::ElidingLabel *label : statLabels) {
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

    // Every label (stat names, values, and the channel letters) is
    // mouse-selectable so figures can be copied out. ElidingLabel renders the
    // selection through QLabel's native painting whenever its text fits (see
    // the OPENMV-DIFF in elidinglabel.cpp).
    const QList<QLabel *> allLabels = findChildren<QLabel *>();
    for (QLabel *label : allLabels) {
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }

    // Keep the channel labels (R/G/B, ...) at their minimum width so they hug the left of each stats
    // row instead of centering in a wide column.
    m_ui->C0ChannelLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    m_ui->C1ChannelLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    m_ui->C2ChannelLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    for (QCustomPlot *plot : {m_ui->C0Plot, m_ui->C1Plot, m_ui->C2Plot}) {
        plot->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    }

    m_ui->C0Plot->installEventFilter(this);
    m_ui->C0Plot->setAutoAddPlottableToLegend(false);
    m_ui->C0Plot->setBackground(Utils::creatorTheme()->color(Utils::Theme::BackgroundColorNormal));
    m_ui->C0Plot->axisRect()->setAutoMargins(QCP::msLeft | QCP::msBottom);
    m_ui->C0Plot->axisRect()->setMargins(QMargins());
    m_ui->C0Plot->xAxis->setTickLength(0);
    m_ui->C0Plot->xAxis->setSubTickLength(0);
    m_ui->C0Plot->xAxis->setBasePen(QPen(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal)));
    m_ui->C0Plot->xAxis->setTickPen(QPen(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal)));
    m_ui->C0Plot->xAxis->setSubTickPen(QPen(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal)));
    m_ui->C0Plot->xAxis->setTickLabelColor(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal));
    m_ui->C0Plot->xAxis->grid()->setZeroLinePen(m_ui->C0Plot->xAxis->grid()->pen());
    m_ui->C0Plot->xAxis->setPadding(0);
    m_ui->C0Plot->yAxis->setTicks(false);
    m_ui->C0Plot->yAxis->setTickLabels(false);
    m_ui->C0Plot->yAxis->setBasePen(QPen(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal)));
    m_ui->C0Plot->yAxis->setLabelColor(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal));
    m_ui->C0Plot->yAxis->setPadding(2);
    m_ui->C0Plot->yAxis->setLabelPadding(3);
    m_channel0 = m_ui->C0Plot->addGraph();
    m_ui->C0MeanValue->setMinimumWidth(m_ui->C0MeanValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C0MedianValue->setMinimumWidth(m_ui->C0MedianValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C0ModeValue->setMinimumWidth(m_ui->C0ModeValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C0StDevValue->setMinimumWidth(m_ui->C0StDevValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C0MinValue->setMinimumWidth(m_ui->C0MinValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C0MaxValue->setMinimumWidth(m_ui->C0MaxValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C0LQValue->setMinimumWidth(m_ui->C0LQValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C0UQValue->setMinimumWidth(m_ui->C0UQValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));

    m_ui->C1Plot->installEventFilter(this);
    m_ui->C1Plot->setAutoAddPlottableToLegend(false);
    m_ui->C1Plot->setBackground(Utils::creatorTheme()->color(Utils::Theme::BackgroundColorNormal));
    m_ui->C1Plot->axisRect()->setAutoMargins(QCP::msLeft | QCP::msBottom);
    m_ui->C1Plot->axisRect()->setMargins(QMargins());
    m_ui->C1Plot->xAxis->setTickLength(0);
    m_ui->C1Plot->xAxis->setSubTickLength(0);
    m_ui->C1Plot->xAxis->setBasePen(QPen(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal)));
    m_ui->C1Plot->xAxis->setTickPen(QPen(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal)));
    m_ui->C1Plot->xAxis->setSubTickPen(QPen(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal)));
    m_ui->C1Plot->xAxis->setTickLabelColor(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal));
    m_ui->C1Plot->xAxis->grid()->setZeroLinePen(m_ui->C1Plot->xAxis->grid()->pen());
    m_ui->C1Plot->xAxis->setPadding(0);
    m_ui->C1Plot->yAxis->setTicks(false);
    m_ui->C1Plot->yAxis->setTickLabels(false);
    m_ui->C1Plot->yAxis->setBasePen(QPen(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal)));
    m_ui->C1Plot->yAxis->setLabelColor(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal));
    m_ui->C1Plot->yAxis->setPadding(2);
    m_ui->C1Plot->yAxis->setLabelPadding(3);
    m_channel1 = m_ui->C1Plot->addGraph();
    m_ui->C1MeanValue->setMinimumWidth(m_ui->C1MeanValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C1MedianValue->setMinimumWidth(m_ui->C1MedianValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C1ModeValue->setMinimumWidth(m_ui->C1ModeValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C1StDevValue->setMinimumWidth(m_ui->C1StDevValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C1MinValue->setMinimumWidth(m_ui->C1MinValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C1MaxValue->setMinimumWidth(m_ui->C1MaxValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C1LQValue->setMinimumWidth(m_ui->C1LQValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C1UQValue->setMinimumWidth(m_ui->C1UQValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));

    m_ui->C2Plot->installEventFilter(this);
    m_ui->C2Plot->setAutoAddPlottableToLegend(false);
    m_ui->C2Plot->setBackground(Utils::creatorTheme()->color(Utils::Theme::BackgroundColorNormal));
    m_ui->C2Plot->axisRect()->setAutoMargins(QCP::msLeft | QCP::msBottom);
    m_ui->C2Plot->axisRect()->setMargins(QMargins());
    m_ui->C2Plot->xAxis->setTickLength(0);
    m_ui->C2Plot->xAxis->setSubTickLength(0);
    m_ui->C2Plot->xAxis->setBasePen(QPen(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal)));
    m_ui->C2Plot->xAxis->setTickPen(QPen(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal)));
    m_ui->C2Plot->xAxis->setSubTickPen(QPen(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal)));
    m_ui->C2Plot->xAxis->setTickLabelColor(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal));
    m_ui->C2Plot->xAxis->grid()->setZeroLinePen(m_ui->C2Plot->xAxis->grid()->pen());
    m_ui->C2Plot->xAxis->setPadding(0);
    m_ui->C2Plot->yAxis->setTicks(false);
    m_ui->C2Plot->yAxis->setTickLabels(false);
    m_ui->C2Plot->yAxis->setBasePen(QPen(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal)));
    m_ui->C2Plot->yAxis->setLabelColor(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal));
    m_ui->C2Plot->yAxis->setPadding(2);
    m_ui->C2Plot->yAxis->setLabelPadding(3);
    m_channel2 = m_ui->C2Plot->addGraph();
    m_ui->C2MeanValue->setMinimumWidth(m_ui->C2MeanValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C2MedianValue->setMinimumWidth(m_ui->C2MedianValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C2ModeValue->setMinimumWidth(m_ui->C2ModeValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C2StDevValue->setMinimumWidth(m_ui->C2StDevValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C2MinValue->setMinimumWidth(m_ui->C2MinValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C2MaxValue->setMinimumWidth(m_ui->C2MaxValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C2LQValue->setMinimumWidth(m_ui->C2LQValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));
    m_ui->C2UQValue->setMinimumWidth(m_ui->C2UQValue->fontMetrics().horizontalAdvance(QStringLiteral("-00000")));

    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QString(QStringLiteral("background-color:%1;color:%2;")).
                  arg(Utils::creatorTheme()->color(Utils::Theme::BackgroundColorNormal).name()).
                  arg(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal).name()));

    colorSpaceChanged(m_colorSpace);
}

OpenMVPluginHistogram::~OpenMVPluginHistogram()
{
    delete m_ui;
}

bool OpenMVPluginHistogram::eventFilter(QObject *watched, QEvent *event)
{
    if((watched == m_ui->C0Plot)
    || (watched == m_ui->C1Plot)
    || (watched == m_ui->C2Plot))
    {
        if(event->type() == QEvent::ToolTip)
        {
            QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);
            QCPAbstractPlottable *plottable = Q_NULLPTR;
            double value;

            if(watched == m_ui->C0Plot)
            {
                plottable = m_ui->C0Plot->plottableAt(helpEvent->pos());
                value = m_ui->C0Plot->xAxis->pixelToCoord(helpEvent->pos().x());
            }
            else if(watched == m_ui->C1Plot)
            {
                plottable = m_ui->C1Plot->plottableAt(helpEvent->pos());
                value = m_ui->C1Plot->xAxis->pixelToCoord(helpEvent->pos().x());
            }
            else if(watched == m_ui->C2Plot)
            {
                plottable = m_ui->C2Plot->plottableAt(helpEvent->pos());
                value = m_ui->C2Plot->xAxis->pixelToCoord(helpEvent->pos().x());
            }

            if(plottable)
            {
                QToolTip::showText(helpEvent->globalPos(), Tr::tr("Value %L1").arg(round(value)));
            }
            else
            {
                QToolTip::hideText();
            }

            return true;
        }

        if(event->type() == QEvent::WhatsThis)
        {
            QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);
            QCPAbstractPlottable *plottable = Q_NULLPTR;
            double value;

            if(watched == m_ui->C0Plot)
            {
                plottable = m_ui->C0Plot->plottableAt(helpEvent->pos());
                value = m_ui->C0Plot->xAxis->pixelToCoord(helpEvent->pos().x());
            }
            else if(watched == m_ui->C1Plot)
            {
                plottable = m_ui->C1Plot->plottableAt(helpEvent->pos());
                value = m_ui->C1Plot->xAxis->pixelToCoord(helpEvent->pos().x());
            }
            else if(watched == m_ui->C2Plot)
            {
                plottable = m_ui->C2Plot->plottableAt(helpEvent->pos());
                value = m_ui->C2Plot->xAxis->pixelToCoord(helpEvent->pos().x());
            }

            if(plottable)
            {
                QWhatsThis::showText(helpEvent->globalPos(), Tr::tr("Value %L1").arg(round(value)));
            }
            else
            {
                QWhatsThis::hideText();
            }

            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void OpenMVPluginHistogram::colorSpaceChanged(int colorSpace)
{
    m_colorSpace = colorSpace;

    switch(m_colorSpace)
    {
        case RGB_COLOR_SPACE:
        {
            updatePlot(m_channel0, RGB_COLOR_SPACE_R);
            m_ui->C0MeanValue->setNum(m_mean);
            m_ui->C0MedianValue->setNum(m_median);
            m_ui->C0ModeValue->setNum(m_mode);
            m_ui->C0StDevValue->setNum(m_standardDeviation);
            m_ui->C0MinValue->setNum(m_min);
            m_ui->C0MaxValue->setNum(m_max);
            m_ui->C0LQValue->setNum(m_lowerQuartile);
            m_ui->C0UQValue->setNum(m_upperQuartile);
            m_channel0->setPen(QPen(QBrush(QColor(255, 0, 0)), 0, Qt::SolidLine));
            m_channel0->setBrush(QBrush(QColor(255, 200, 200), Qt::SolidPattern));
            m_ui->C0Plot->rescaleAxes();
            m_ui->C0ChannelLabel->setText(Tr::tr("R"));
            m_ui->C0Plot->yAxis->setRange(0, 1);
            m_ui->C0Plot->replot(QCustomPlot::rpQueuedReplot);

            updatePlot(m_channel1, RGB_COLOR_SPACE_G);
            m_ui->C1MeanValue->setNum(m_mean);
            m_ui->C1MedianValue->setNum(m_median);
            m_ui->C1ModeValue->setNum(m_mode);
            m_ui->C1StDevValue->setNum(m_standardDeviation);
            m_ui->C1MinValue->setNum(m_min);
            m_ui->C1MaxValue->setNum(m_max);
            m_ui->C1LQValue->setNum(m_lowerQuartile);
            m_ui->C1UQValue->setNum(m_upperQuartile);
            m_channel1->setPen(QPen(QBrush(QColor(0, 255, 0)), 0, Qt::SolidLine));
            m_channel1->setBrush(QBrush(QColor(200, 255, 200), Qt::SolidPattern));
            m_ui->C1Plot->rescaleAxes();
            m_ui->C1ChannelLabel->setText(Tr::tr("G"));
            m_ui->C1Plot->yAxis->setRange(0, 1);
            m_ui->C1Plot->replot(QCustomPlot::rpQueuedReplot);

            updatePlot(m_channel2, RGB_COLOR_SPACE_B);
            m_ui->C2MeanValue->setNum(m_mean);
            m_ui->C2MedianValue->setNum(m_median);
            m_ui->C2ModeValue->setNum(m_mode);
            m_ui->C2StDevValue->setNum(m_standardDeviation);
            m_ui->C2MinValue->setNum(m_min);
            m_ui->C2MaxValue->setNum(m_max);
            m_ui->C2LQValue->setNum(m_lowerQuartile);
            m_ui->C2UQValue->setNum(m_upperQuartile);
            m_channel2->setPen(QPen(QBrush(QColor(0, 0, 255)), 0, Qt::SolidLine));
            m_channel2->setBrush(QBrush(QColor(200, 200, 255), Qt::SolidPattern));
            m_ui->C2Plot->rescaleAxes();
            m_ui->C2ChannelLabel->setText(Tr::tr("B"));
            m_ui->C2Plot->yAxis->setRange(0, 1);
            m_ui->C2Plot->replot(QCustomPlot::rpQueuedReplot);

            m_ui->C1Plot->show();
            m_ui->C1Stats->show();
            m_ui->C2Plot->show();
            m_ui->C2Stats->show();

            break;
        }
        case GRAYSCALE_COLOR_SPACE:
        {
            updatePlot(m_channel0, GRAYSCALE_COLOR_SPACE_Y);
            m_ui->C0MeanValue->setNum(m_mean);
            m_ui->C0MedianValue->setNum(m_median);
            m_ui->C0ModeValue->setNum(m_mode);
            m_ui->C0StDevValue->setNum(m_standardDeviation);
            m_ui->C0MinValue->setNum(m_min);
            m_ui->C0MaxValue->setNum(m_max);
            m_ui->C0LQValue->setNum(m_lowerQuartile);
            m_ui->C0UQValue->setNum(m_upperQuartile);
            m_channel0->setPen(QPen(QBrush(QColor(143, 143, 143)), 0, Qt::SolidLine));
            m_channel0->setBrush(QBrush(QColor(200, 200, 200), Qt::SolidPattern));
            m_ui->C0Plot->rescaleAxes();
            m_ui->C0ChannelLabel->setText(Tr::tr("Y"));
            m_ui->C0Plot->yAxis->setRange(0, 1);
            m_ui->C0Plot->replot(QCustomPlot::rpQueuedReplot);

            m_ui->C1Plot->hide();
            m_ui->C1Stats->hide();
            m_ui->C2Plot->hide();
            m_ui->C2Stats->hide();

            break;
        }
        case LAB_COLOR_SPACE:
        {
            updatePlot(m_channel0, LAB_COLOR_SPACE_L);
            m_ui->C0MeanValue->setNum(m_mean);
            m_ui->C0MedianValue->setNum(m_median);
            m_ui->C0ModeValue->setNum(m_mode);
            m_ui->C0StDevValue->setNum(m_standardDeviation);
            m_ui->C0MinValue->setNum(m_min);
            m_ui->C0MaxValue->setNum(m_max);
            m_ui->C0LQValue->setNum(m_lowerQuartile);
            m_ui->C0UQValue->setNum(m_upperQuartile);
            m_channel0->setPen(QPen(QBrush(QColor(143, 143, 143)), 0, Qt::SolidLine));
            m_channel0->setBrush(QBrush(QColor(200, 200, 200), Qt::SolidPattern));
            m_ui->C0Plot->rescaleAxes();
            m_ui->C0ChannelLabel->setText(Tr::tr("L"));
            m_ui->C0Plot->yAxis->setRange(0, 1);
            m_ui->C0Plot->replot(QCustomPlot::rpQueuedReplot);

            updatePlot(m_channel1, LAB_COLOR_SPACE_A);
            m_ui->C1MeanValue->setNum(m_mean);
            m_ui->C1MedianValue->setNum(m_median);
            m_ui->C1ModeValue->setNum(m_mode);
            m_ui->C1StDevValue->setNum(m_standardDeviation);
            m_ui->C1MinValue->setNum(m_min);
            m_ui->C1MaxValue->setNum(m_max);
            m_ui->C1LQValue->setNum(m_lowerQuartile);
            m_ui->C1UQValue->setNum(m_upperQuartile);
            m_channel1->setPen(QPen(QBrush(QColor(204, 255, 0)), 0, Qt::SolidLine));
            m_channel1->setBrush(QBrush(QColor(244, 255, 200), Qt::SolidPattern));
            m_ui->C1Plot->rescaleAxes();
            m_ui->C1ChannelLabel->setText(Tr::tr("A"));
            m_ui->C1Plot->yAxis->setRange(0, 1);
            m_ui->C1Plot->replot(QCustomPlot::rpQueuedReplot);

            updatePlot(m_channel2, LAB_COLOR_SPACE_B);
            m_ui->C2MeanValue->setNum(m_mean);
            m_ui->C2MedianValue->setNum(m_median);
            m_ui->C2ModeValue->setNum(m_mode);
            m_ui->C2StDevValue->setNum(m_standardDeviation);
            m_ui->C2MinValue->setNum(m_min);
            m_ui->C2MaxValue->setNum(m_max);
            m_ui->C2LQValue->setNum(m_lowerQuartile);
            m_ui->C2UQValue->setNum(m_upperQuartile);
            m_channel2->setPen(QPen(QBrush(QColor(0, 102, 255)), 0, Qt::SolidLine));
            m_channel2->setBrush(QBrush(QColor(200, 222, 255), Qt::SolidPattern));
            m_ui->C2Plot->rescaleAxes();
            m_ui->C2ChannelLabel->setText(Tr::tr("B"));
            m_ui->C2Plot->yAxis->setRange(0, 1);
            m_ui->C2Plot->replot(QCustomPlot::rpQueuedReplot);

            m_ui->C1Plot->show();
            m_ui->C1Stats->show();
            m_ui->C2Plot->show();
            m_ui->C2Stats->show();

            break;
        }
        case YUV_COLOR_SPACE:
        {
            updatePlot(m_channel0, YUV_COLOR_SPACE_Y);
            m_ui->C0MeanValue->setNum(m_mean);
            m_ui->C0MedianValue->setNum(m_median);
            m_ui->C0ModeValue->setNum(m_mode);
            m_ui->C0StDevValue->setNum(m_standardDeviation);
            m_ui->C0MinValue->setNum(m_min);
            m_ui->C0MaxValue->setNum(m_max);
            m_ui->C0LQValue->setNum(m_lowerQuartile);
            m_ui->C0UQValue->setNum(m_upperQuartile);
            m_channel0->setPen(QPen(QBrush(QColor(143, 143, 143)), 0, Qt::SolidLine));
            m_channel0->setBrush(QBrush(QColor(200, 200, 200), Qt::SolidPattern));
            m_ui->C0Plot->rescaleAxes();
            m_ui->C0ChannelLabel->setText(Tr::tr("Y"));
            m_ui->C0Plot->yAxis->setRange(0, 1);
            m_ui->C0Plot->replot(QCustomPlot::rpQueuedReplot);

            updatePlot(m_channel1, YUV_COLOR_SPACE_U);
            m_ui->C1MeanValue->setNum(m_mean);
            m_ui->C1MedianValue->setNum(m_median);
            m_ui->C1ModeValue->setNum(m_mode);
            m_ui->C1StDevValue->setNum(m_standardDeviation);
            m_ui->C1MinValue->setNum(m_min);
            m_ui->C1MaxValue->setNum(m_max);
            m_ui->C1LQValue->setNum(m_lowerQuartile);
            m_ui->C1UQValue->setNum(m_upperQuartile);
            m_channel1->setPen(QPen(QBrush(QColor(0, 255, 102)), 0, Qt::SolidLine));
            m_channel1->setBrush(QBrush(QColor(200, 255, 222), Qt::SolidPattern));
            m_ui->C1Plot->rescaleAxes();
            m_ui->C1ChannelLabel->setText(Tr::tr("U"));
            m_ui->C1Plot->yAxis->setRange(0, 1);
            m_ui->C1Plot->replot(QCustomPlot::rpQueuedReplot);

            updatePlot(m_channel2, YUV_COLOR_SPACE_V);
            m_ui->C2MeanValue->setNum(m_mean);
            m_ui->C2MedianValue->setNum(m_median);
            m_ui->C2ModeValue->setNum(m_mode);
            m_ui->C2StDevValue->setNum(m_standardDeviation);
            m_ui->C2MinValue->setNum(m_min);
            m_ui->C2MaxValue->setNum(m_max);
            m_ui->C2LQValue->setNum(m_lowerQuartile);
            m_ui->C2UQValue->setNum(m_upperQuartile);
            m_channel2->setPen(QPen(QBrush(QColor(204, 0, 255)), 0, Qt::SolidLine));
            m_channel2->setBrush(QBrush(QColor(244, 200, 255), Qt::SolidPattern));
            m_ui->C2Plot->rescaleAxes();
            m_ui->C2ChannelLabel->setText(Tr::tr("V"));
            m_ui->C2Plot->yAxis->setRange(0, 1);
            m_ui->C2Plot->replot(QCustomPlot::rpQueuedReplot);

            m_ui->C1Plot->show();
            m_ui->C1Stats->show();
            m_ui->C2Plot->show();
            m_ui->C2Stats->show();

            break;
        }
    }
}

int start_of_scan_offset(const QByteArray &jpeg) {
    for (uint8_t *pstart = ((uint8_t *) jpeg.data()), *p = pstart, *pend = pstart + jpeg.length(); p < pend; ) {
        uint16_t header = (p[0] << 8) | p[1];
        p += sizeof(uint16_t);

        if ((0xFFD0 <= header) && (header <= 0xFFD9)) {
            continue;
        } else if (0xFFDA == header) {
            // Start-of-Scan (no more jpeg headers left).
            return p - pstart;
        } else if (((0xFFC0 <= header) && (header <= 0xFFCF))
                   || ((0xFFDB <= header) && (header <= 0xFFDF))
                   || ((0xFFE0 <= header) && (header <= 0xFFEF))
                   || ((0xFFF0 <= header) && (header <= 0xFFFE))) {
            uint16_t size = (p[0] << 8) | p[1];
            p += sizeof(uint16_t);

            if (((0xFFC1 <= header) && (header <= 0xFFC3))
                || ((0xFFC5 <= header) && (header <= 0xFFC7))
                || ((0xFFC9 <= header) && (header <= 0xFFCB))
                || ((0xFFCD <= header) && (header <= 0xFFCF))) {
                // Non-baseline jpeg.
                return 0;
            } else {
                p += size - sizeof(uint16_t);
            }
        } else {
            // Invalid JPEG
            return 0;
        }
    }

    return 0;
}

void OpenMVPluginHistogram::pixmapUpdate(const QPixmap &data)
{
    if (data.isNull())
    {
        return;
    }

    m_pixmap = data.scaledToWidth(160, Qt::SmoothTransformation);

    switch(m_colorSpace)
    {
        case RGB_COLOR_SPACE:
        {
            updatePlot(m_channel0, RGB_COLOR_SPACE_R);
            m_ui->C0MeanValue->setNum(m_mean);
            m_ui->C0MedianValue->setNum(m_median);
            m_ui->C0ModeValue->setNum(m_mode);
            m_ui->C0StDevValue->setNum(m_standardDeviation);
            m_ui->C0MinValue->setNum(m_min);
            m_ui->C0MaxValue->setNum(m_max);
            m_ui->C0LQValue->setNum(m_lowerQuartile);
            m_ui->C0UQValue->setNum(m_upperQuartile);
            m_channel0->setPen(QPen(QBrush(QColor(255, 0, 0)), 0, Qt::SolidLine));
            m_channel0->setBrush(QBrush(QColor(255, 200, 200), Qt::SolidPattern));
            m_ui->C0Plot->rescaleAxes();
            m_ui->C0ChannelLabel->setText(Tr::tr("R"));
            m_ui->C0Plot->yAxis->setRange(0, 1);
            m_ui->C0Plot->replot(QCustomPlot::rpQueuedReplot);

            updatePlot(m_channel1, RGB_COLOR_SPACE_G);
            m_ui->C1MeanValue->setNum(m_mean);
            m_ui->C1MedianValue->setNum(m_median);
            m_ui->C1ModeValue->setNum(m_mode);
            m_ui->C1StDevValue->setNum(m_standardDeviation);
            m_ui->C1MinValue->setNum(m_min);
            m_ui->C1MaxValue->setNum(m_max);
            m_ui->C1LQValue->setNum(m_lowerQuartile);
            m_ui->C1UQValue->setNum(m_upperQuartile);
            m_channel1->setPen(QPen(QBrush(QColor(0, 255, 0)), 0, Qt::SolidLine));
            m_channel1->setBrush(QBrush(QColor(200, 255, 200), Qt::SolidPattern));
            m_ui->C1Plot->rescaleAxes();
            m_ui->C1ChannelLabel->setText(Tr::tr("G"));
            m_ui->C1Plot->yAxis->setRange(0, 1);
            m_ui->C1Plot->replot(QCustomPlot::rpQueuedReplot);

            updatePlot(m_channel2, RGB_COLOR_SPACE_B);
            m_ui->C2MeanValue->setNum(m_mean);
            m_ui->C2MedianValue->setNum(m_median);
            m_ui->C2ModeValue->setNum(m_mode);
            m_ui->C2StDevValue->setNum(m_standardDeviation);
            m_ui->C2MinValue->setNum(m_min);
            m_ui->C2MaxValue->setNum(m_max);
            m_ui->C2LQValue->setNum(m_lowerQuartile);
            m_ui->C2UQValue->setNum(m_upperQuartile);
            m_channel2->setPen(QPen(QBrush(QColor(0, 0, 255)), 0, Qt::SolidLine));
            m_channel2->setBrush(QBrush(QColor(200, 200, 255), Qt::SolidPattern));
            m_ui->C2Plot->rescaleAxes();
            m_ui->C2ChannelLabel->setText(Tr::tr("B"));
            m_ui->C2Plot->yAxis->setRange(0, 1);
            m_ui->C2Plot->replot(QCustomPlot::rpQueuedReplot);

            break;
        }
        case GRAYSCALE_COLOR_SPACE:
        {
            updatePlot(m_channel0, GRAYSCALE_COLOR_SPACE_Y);
            m_ui->C0MeanValue->setNum(m_mean);
            m_ui->C0MedianValue->setNum(m_median);
            m_ui->C0ModeValue->setNum(m_mode);
            m_ui->C0StDevValue->setNum(m_standardDeviation);
            m_ui->C0MinValue->setNum(m_min);
            m_ui->C0MaxValue->setNum(m_max);
            m_ui->C0LQValue->setNum(m_lowerQuartile);
            m_ui->C0UQValue->setNum(m_upperQuartile);
            m_channel0->setPen(QPen(QBrush(QColor(143, 143, 143)), 0, Qt::SolidLine));
            m_channel0->setBrush(QBrush(QColor(200, 200, 200), Qt::SolidPattern));
            m_ui->C0Plot->rescaleAxes();
            m_ui->C0ChannelLabel->setText(Tr::tr("Y"));
            m_ui->C0Plot->yAxis->setRange(0, 1);
            m_ui->C0Plot->replot(QCustomPlot::rpQueuedReplot);

            break;
        }
        case LAB_COLOR_SPACE:
        {
            updatePlot(m_channel0, LAB_COLOR_SPACE_L);
            m_ui->C0MeanValue->setNum(m_mean);
            m_ui->C0MedianValue->setNum(m_median);
            m_ui->C0ModeValue->setNum(m_mode);
            m_ui->C0StDevValue->setNum(m_standardDeviation);
            m_ui->C0MinValue->setNum(m_min);
            m_ui->C0MaxValue->setNum(m_max);
            m_ui->C0LQValue->setNum(m_lowerQuartile);
            m_ui->C0UQValue->setNum(m_upperQuartile);
            m_channel0->setPen(QPen(QBrush(QColor(143, 143, 143)), 0, Qt::SolidLine));
            m_channel0->setBrush(QBrush(QColor(200, 200, 200), Qt::SolidPattern));
            m_ui->C0Plot->rescaleAxes();
            m_ui->C0ChannelLabel->setText(Tr::tr("L"));
            m_ui->C0Plot->yAxis->setRange(0, 1);
            m_ui->C0Plot->replot(QCustomPlot::rpQueuedReplot);

            updatePlot(m_channel1, LAB_COLOR_SPACE_A);
            m_ui->C1MeanValue->setNum(m_mean);
            m_ui->C1MedianValue->setNum(m_median);
            m_ui->C1ModeValue->setNum(m_mode);
            m_ui->C1StDevValue->setNum(m_standardDeviation);
            m_ui->C1MinValue->setNum(m_min);
            m_ui->C1MaxValue->setNum(m_max);
            m_ui->C1LQValue->setNum(m_lowerQuartile);
            m_ui->C1UQValue->setNum(m_upperQuartile);
            m_channel1->setPen(QPen(QBrush(QColor(204, 255, 0)), 0, Qt::SolidLine));
            m_channel1->setBrush(QBrush(QColor(244, 255, 200), Qt::SolidPattern));
            m_ui->C1Plot->rescaleAxes();
            m_ui->C1ChannelLabel->setText(Tr::tr("A"));
            m_ui->C1Plot->yAxis->setRange(0, 1);
            m_ui->C1Plot->replot(QCustomPlot::rpQueuedReplot);

            updatePlot(m_channel2, LAB_COLOR_SPACE_B);
            m_ui->C2MeanValue->setNum(m_mean);
            m_ui->C2MedianValue->setNum(m_median);
            m_ui->C2ModeValue->setNum(m_mode);
            m_ui->C2StDevValue->setNum(m_standardDeviation);
            m_ui->C2MinValue->setNum(m_min);
            m_ui->C2MaxValue->setNum(m_max);
            m_ui->C2LQValue->setNum(m_lowerQuartile);
            m_ui->C2UQValue->setNum(m_upperQuartile);
            m_channel2->setPen(QPen(QBrush(QColor(0, 102, 255)), 0, Qt::SolidLine));
            m_channel2->setBrush(QBrush(QColor(200, 222, 255), Qt::SolidPattern));
            m_ui->C2Plot->rescaleAxes();
            m_ui->C2ChannelLabel->setText(Tr::tr("B"));
            m_ui->C2Plot->yAxis->setRange(0, 1);
            m_ui->C2Plot->replot(QCustomPlot::rpQueuedReplot);

            break;
        }
        case YUV_COLOR_SPACE:
        {
            updatePlot(m_channel0, YUV_COLOR_SPACE_Y);
            m_ui->C0MeanValue->setNum(m_mean);
            m_ui->C0MedianValue->setNum(m_median);
            m_ui->C0ModeValue->setNum(m_mode);
            m_ui->C0StDevValue->setNum(m_standardDeviation);
            m_ui->C0MinValue->setNum(m_min);
            m_ui->C0MaxValue->setNum(m_max);
            m_ui->C0LQValue->setNum(m_lowerQuartile);
            m_ui->C0UQValue->setNum(m_upperQuartile);
            m_channel0->setPen(QPen(QBrush(QColor(143, 143, 143)), 0, Qt::SolidLine));
            m_channel0->setBrush(QBrush(QColor(200, 200, 200), Qt::SolidPattern));
            m_ui->C0Plot->rescaleAxes();
            m_ui->C0ChannelLabel->setText(Tr::tr("Y"));
            m_ui->C0Plot->yAxis->setRange(0, 1);
            m_ui->C0Plot->replot(QCustomPlot::rpQueuedReplot);

            updatePlot(m_channel1, YUV_COLOR_SPACE_U);
            m_ui->C1MeanValue->setNum(m_mean);
            m_ui->C1MedianValue->setNum(m_median);
            m_ui->C1ModeValue->setNum(m_mode);
            m_ui->C1StDevValue->setNum(m_standardDeviation);
            m_ui->C1MinValue->setNum(m_min);
            m_ui->C1MaxValue->setNum(m_max);
            m_ui->C1LQValue->setNum(m_lowerQuartile);
            m_ui->C1UQValue->setNum(m_upperQuartile);
            m_channel1->setPen(QPen(QBrush(QColor(0, 255, 102)), 0, Qt::SolidLine));
            m_channel1->setBrush(QBrush(QColor(200, 255, 222), Qt::SolidPattern));
            m_ui->C1Plot->rescaleAxes();
            m_ui->C1ChannelLabel->setText(Tr::tr("U"));
            m_ui->C1Plot->yAxis->setRange(0, 1);
            m_ui->C1Plot->replot(QCustomPlot::rpQueuedReplot);

            updatePlot(m_channel2, YUV_COLOR_SPACE_V);
            m_ui->C2MeanValue->setNum(m_mean);
            m_ui->C2MedianValue->setNum(m_median);
            m_ui->C2ModeValue->setNum(m_mode);
            m_ui->C2StDevValue->setNum(m_standardDeviation);
            m_ui->C2MinValue->setNum(m_min);
            m_ui->C2MaxValue->setNum(m_max);
            m_ui->C2LQValue->setNum(m_lowerQuartile);
            m_ui->C2UQValue->setNum(m_upperQuartile);
            m_channel2->setPen(QPen(QBrush(QColor(204, 0, 255)), 0, Qt::SolidLine));
            m_channel2->setBrush(QBrush(QColor(244, 200, 255), Qt::SolidPattern));
            m_ui->C2Plot->rescaleAxes();
            m_ui->C2ChannelLabel->setText(Tr::tr("V"));
            m_ui->C2Plot->yAxis->setRange(0, 1);
            m_ui->C2Plot->replot(QCustomPlot::rpQueuedReplot);

            break;
        }
    }

    QBuffer buffer;
    QImageWriter writer(&buffer, "jpg");
    writer.write(m_pixmap.toImage());
    emit focusMetric(((buffer.size() - start_of_scan_offset(buffer.data())) * 100 * 8) / (m_pixmap.rect().width() * m_pixmap.rect().height()));
}

} // namespace Internal
} // namespace OpenMV
