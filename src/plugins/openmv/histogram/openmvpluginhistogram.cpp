#include "openmvpluginhistogram.h"
#include "ui_openmvpluginhistogram.h"

#include "openmvtr.h"

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
extern const int8_t lab_table[196608];
extern const int8_t yuv_table[196608];

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

static inline int toRGB565(QRgb value)
{
    int r = toR5(value);
    int g = toG6(value);
    int b = toB5(value);

    return (r << 3) | (g >> 3) | ((g & 0x7) << 13) | (b << 8); // byte reversed.
}

static inline int toGrayscale(QRgb value)
{
    return yuv_table[(toRGB565(value)*3)+0] + 128; // 0:255 -> 0:255
}

static inline int toL(QRgb value)
{
    return lab_table[(toRGB565(value)*3)+0]; // 0:255 -> 0:100
}

static inline int toA(QRgb value)
{
    return lab_table[(toRGB565(value)*3)+1] + 128; // 0:255 -> 0:255
}

static inline int toB(QRgb value)
{
    return lab_table[(toRGB565(value)*3)+2] + 128; // 0:255 -> 0:255
}

static inline int toY(QRgb value)
{
    return yuv_table[(toRGB565(value)*3)+0] + 128; // 0:255 -> 0:255
}

static inline int toU(QRgb value)
{
    return yuv_table[(toRGB565(value)*3)+1] + 128; // 0:255 -> 0:255
}

static inline int toV(QRgb value)
{
    return yuv_table[(toRGB565(value)*3)+2] + 128; // 0:255 -> 0:255
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

void OpenMVPluginHistogram::updatePlot(QCPGraph *graph, int channel)
{
    QImage image = m_pixmap.toImage();
    QVector<int> vector;

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

    for(int i = 0; i < vector.size(); i++)
    {
        graph->addData(getValue(i, channel), mode_count ? (vector[i] / double(mode_count)) : 0);
    }
}

OpenMVPluginHistogram::OpenMVPluginHistogram(QWidget *parent) : QWidget(parent), m_colorSpace(RGB_COLOR_SPACE), m_pixmap(QPixmap()), m_ui(new Ui::OpenMVPluginHistogram)
{
    m_ui->setupUi(this);

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
            m_ui->C0Plot->yAxis->setLabel(Tr::tr("R"));
            m_ui->C0Plot->yAxis->setRange(0, 1);
            m_ui->C0Plot->replot();

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
            m_ui->C1Plot->yAxis->setLabel(Tr::tr("G"));
            m_ui->C1Plot->yAxis->setRange(0, 1);
            m_ui->C1Plot->replot();

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
            m_ui->C2Plot->yAxis->setLabel(Tr::tr("B"));
            m_ui->C2Plot->yAxis->setRange(0, 1);
            m_ui->C2Plot->replot();

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
            m_ui->C0Plot->yAxis->setLabel(Tr::tr("Y"));
            m_ui->C0Plot->yAxis->setRange(0, 1);
            m_ui->C0Plot->replot();

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
            m_ui->C0Plot->yAxis->setLabel(Tr::tr("L"));
            m_ui->C0Plot->yAxis->setRange(0, 1);
            m_ui->C0Plot->replot();

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
            m_ui->C1Plot->yAxis->setLabel(Tr::tr("A"));
            m_ui->C1Plot->yAxis->setRange(0, 1);
            m_ui->C1Plot->replot();

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
            m_ui->C2Plot->yAxis->setLabel(Tr::tr("B"));
            m_ui->C2Plot->yAxis->setRange(0, 1);
            m_ui->C2Plot->replot();

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
            m_ui->C0Plot->yAxis->setLabel(Tr::tr("Y"));
            m_ui->C0Plot->yAxis->setRange(0, 1);
            m_ui->C0Plot->replot();

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
            m_ui->C1Plot->yAxis->setLabel(Tr::tr("U"));
            m_ui->C1Plot->yAxis->setRange(0, 1);
            m_ui->C1Plot->replot();

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
            m_ui->C2Plot->yAxis->setLabel(Tr::tr("V"));
            m_ui->C2Plot->yAxis->setRange(0, 1);
            m_ui->C2Plot->replot();

            m_ui->C1Plot->show();
            m_ui->C1Stats->show();
            m_ui->C2Plot->show();
            m_ui->C2Stats->show();

            break;
        }
    }
}

void OpenMVPluginHistogram::pixmapUpdate(const QPixmap &data)
{
    m_pixmap = data;

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
            m_ui->C0Plot->yAxis->setLabel(Tr::tr("R"));
            m_ui->C0Plot->yAxis->setRange(0, 1);
            m_ui->C0Plot->replot();

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
            m_ui->C1Plot->yAxis->setLabel(Tr::tr("G"));
            m_ui->C1Plot->yAxis->setRange(0, 1);
            m_ui->C1Plot->replot();

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
            m_ui->C2Plot->yAxis->setLabel(Tr::tr("B"));
            m_ui->C2Plot->yAxis->setRange(0, 1);
            m_ui->C2Plot->replot();

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
            m_ui->C0Plot->yAxis->setLabel(Tr::tr("Y"));
            m_ui->C0Plot->yAxis->setRange(0, 1);
            m_ui->C0Plot->replot();

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
            m_ui->C0Plot->yAxis->setLabel(Tr::tr("L"));
            m_ui->C0Plot->yAxis->setRange(0, 1);
            m_ui->C0Plot->replot();

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
            m_ui->C1Plot->yAxis->setLabel(Tr::tr("A"));
            m_ui->C1Plot->yAxis->setRange(0, 1);
            m_ui->C1Plot->replot();

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
            m_ui->C2Plot->yAxis->setLabel(Tr::tr("B"));
            m_ui->C2Plot->yAxis->setRange(0, 1);
            m_ui->C2Plot->replot();

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
            m_ui->C0Plot->yAxis->setLabel(Tr::tr("Y"));
            m_ui->C0Plot->yAxis->setRange(0, 1);
            m_ui->C0Plot->replot();

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
            m_ui->C1Plot->yAxis->setLabel(Tr::tr("U"));
            m_ui->C1Plot->yAxis->setRange(0, 1);
            m_ui->C1Plot->replot();

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
            m_ui->C2Plot->yAxis->setLabel(Tr::tr("V"));
            m_ui->C2Plot->yAxis->setRange(0, 1);
            m_ui->C2Plot->replot();

            break;
        }
    }
}

} // namespace Internal
} // namespace OpenMV
