#include "thresholdeditor.h"

#include "openmvtr.h"

#include <utils/theme/theme.h>

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
    return lab_table[(toRGB565(value)*3)+1]; // 0:255 -> -128:127
}

static inline int toB(QRgb value)
{
    return lab_table[(toRGB565(value)*3)+2]; // 0:255 -> -128:127
}

void ThresholdEditor::changed()
{
    QImage out(m_image.width(), m_image.height(), QImage::Format_Grayscale8);

    if(m_combo->currentIndex()) // LAB
    {
        m_paneG->hide();
        m_paneLAB->show();

        for(int y = 0; y < m_image.height(); y++)
        {
            for(int x = 0; x < m_image.width(); x++)
            {
                QRgb pixel = m_image.pixel(x, y);
                bool LMinOk = toL(pixel) >= qMin(m_LMin->value(), m_LMax->value());
                bool LMaxOk = toL(pixel) <= qMax(m_LMax->value(), m_LMin->value());
                bool AMinOk = toA(pixel) >= qMin(m_AMin->value(), m_AMax->value());
                bool AMaxOk = toA(pixel) <= qMax(m_AMax->value(), m_AMin->value());
                bool BMinOk = toB(pixel) >= qMin(m_BMin->value(), m_BMax->value());
                bool BMaxOk = toB(pixel) <= qMax(m_BMax->value(), m_BMin->value());
                bool allOk = (LMinOk && LMaxOk && AMinOk && AMaxOk && BMinOk && BMaxOk) ^ m_invert->isChecked();
                out.setPixel(x, y, allOk ? -1 : 0);
            }
        }

        m_LABOut->setText(QStringLiteral("(%L1, %L2, %L3, %L4, %L5, %L6)").arg(m_LMin->value())
                                                                          .arg(m_LMax->value())
                                                                          .arg(m_AMin->value())
                                                                          .arg(m_AMax->value())
                                                                          .arg(m_BMin->value())
                                                                          .arg(m_BMax->value()));
    }
    else // Grayscale
    {
        m_paneG->show();
        m_paneLAB->hide();

        for(int y = 0; y < m_image.height(); y++)
        {
            for(int x = 0; x < m_image.width(); x++)
            {
                QRgb pixel = m_image.pixel(x, y);
                bool GMinOk = toGrayscale(pixel) >= qMin(m_GMin->value(), m_GMax->value());
                bool GMaxOk = toGrayscale(pixel) <= qMax(m_GMax->value(), m_GMin->value());
                bool allOk = (GMinOk && GMaxOk) ^ m_invert->isChecked();
                out.setPixel(x, y, allOk ? -1 : 0);
            }
        }

        m_GOut->setText(QStringLiteral("(%L1, %L2)").arg(m_GMin->value())
                                                    .arg(m_GMax->value()));
    }

    if(m_bin->scene())
    {
        delete m_bin->scene();
    }

    m_bin->setScene(new QGraphicsScene(this));
    m_bin->scene()->addPixmap(QPixmap::fromImage(out));

    qreal rawScale = qMin((m_raw->width()-1) / m_raw->sceneRect().width(), (m_raw->height()-1) / m_raw->sceneRect().height());
    m_raw->setTransform(QTransform(1, 0, 0, 0, 1, 0, 0, 0, 1).scale(rawScale, rawScale));

    qreal binScale = qMin((m_bin->width()-1) / m_bin->sceneRect().width(), (m_bin->height()-1) / m_bin->sceneRect().height());
    m_bin->setTransform(QTransform(1, 0, 0, 0, 1, 0, 0, 0, 1).scale(binScale, binScale));

    if(sender() == m_combo)
    {
        QSize s = size();
        adjustSize();
        resize(s);
    }
}

void ThresholdEditor::resizeEvent(QResizeEvent *event)
{
    qreal rawScale = qMin((m_raw->width()-1) / m_raw->sceneRect().width(), (m_raw->height()-1) / m_raw->sceneRect().height());
    m_raw->setTransform(QTransform(1, 0, 0, 0, 1, 0, 0, 0, 1).scale(rawScale, rawScale));

    qreal binScale = qMin((m_bin->width()-1) / m_bin->sceneRect().width(), (m_bin->height()-1) / m_bin->sceneRect().height());
    m_bin->setTransform(QTransform(1, 0, 0, 0, 1, 0, 0, 0, 1).scale(binScale, binScale));

    QDialog::resizeEvent(event);
}

void ThresholdEditor::showEvent(QShowEvent *event)
{
    restoreGeometry(m_geometry);

    QDialog::showEvent(event);
}

ThresholdEditor::ThresholdEditor(const QPixmap &pixmap, QByteArray geometry, QWidget *parent, Qt::WindowFlags f, const QString &altMessage) : QDialog(parent, f)
{
    m_geometry = geometry;
    setWindowTitle(Tr::tr("Threhsold Editor"));

    QVBoxLayout *layout = new QVBoxLayout(this);

    // Graphics Views //
    {
        QWidget *temp = new QWidget();
        QHBoxLayout *h_layout = new QHBoxLayout(temp);
        h_layout->setContentsMargins(0, 0, 0, 0);
        h_layout->setSpacing(0);

        m_image = pixmap.toImage();

        {
            QWidget *tmp = new QWidget();
            QVBoxLayout *v_layout = new QVBoxLayout(tmp);
            v_layout->setContentsMargins(0, 0, 0, 0);

            m_raw = new QGraphicsView();
            m_raw->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            m_raw->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            m_raw->setMinimumWidth(160);
            m_raw->setMinimumHeight(120);
            m_raw->setBackgroundBrush(Utils::creatorTheme()->color(Utils::Theme::BackgroundColorDark));
            m_raw->setScene(new QGraphicsScene(this));
            m_raw->scene()->addPixmap(pixmap);
            v_layout->addWidget(new QLabel(Tr::tr("Source Image")));
            v_layout->addWidget(m_raw);

            h_layout->addWidget(tmp);
            h_layout->addItem(new QSpacerItem(10, 0));
        }

        QPixmap white(pixmap.width(), pixmap.height()); white.fill(Qt::white);

        {
            QWidget *tmp = new QWidget();
            QVBoxLayout *v_layout = new QVBoxLayout(tmp);
            v_layout->setContentsMargins(0, 0, 0, 0);

            m_bin = new QGraphicsView();
            m_bin->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            m_bin->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            m_bin->setMinimumWidth(160);
            m_bin->setMinimumHeight(120);
            m_bin->setBackgroundBrush(Utils::creatorTheme()->color(Utils::Theme::BackgroundColorDark));
            m_bin->setScene(new QGraphicsScene(this));
            m_bin->scene()->addPixmap(white);
            v_layout->addWidget(new QLabel(Tr::tr("Binary Image (white pixels are tracked pixels)")));
            v_layout->addWidget(m_bin);

            h_layout->addWidget(tmp);
        }

        layout->addWidget(temp);

        qreal rawScale = qMin((m_raw->width()-1) / m_raw->sceneRect().width(), (m_raw->height()-1) / m_raw->sceneRect().height());
        m_raw->setTransform(QTransform(1, 0, 0, 0, 1, 0, 0, 0, 1).scale(rawScale, rawScale));

        qreal binScale = qMin((m_bin->width()-1) / m_bin->sceneRect().width(), (m_bin->height()-1) / m_bin->sceneRect().height());
        m_bin->setTransform(QTransform(1, 0, 0, 0, 1, 0, 0, 0, 1).scale(binScale, binScale));
    }

    // Controls //
    {
        QWidget *temp = new QWidget();
        QHBoxLayout *h_layout = new QHBoxLayout(temp);
        h_layout->setContentsMargins(0, 0, 0, 0);
        h_layout->setSpacing(0);

        m_combo = new QComboBox();
        m_combo->addItem(Tr::tr("Grayscale"));
        m_combo->addItem(Tr::tr("LAB"));
        m_combo->setCurrentIndex(m_image.isGrayscale() ? 0 : 1);
        h_layout->addWidget(m_combo);
        h_layout->addItem(new QSpacerItem(10, 0));
        connect(m_combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &ThresholdEditor::changed);

        m_invert = new QCheckBox(Tr::tr("Invert"));
        m_invert->setCheckable(true);
        m_invert->setChecked(false);
        h_layout->addWidget(m_invert);
        connect(m_invert, &QCheckBox::toggled, this, &ThresholdEditor::changed);

        QPushButton *resetButton = new QPushButton(Tr::tr("Reset Sliders"));
        h_layout->addWidget(resetButton);

        connect(resetButton, &QPushButton::clicked, this, [this] {
            if(!m_combo->currentIndex())
            {
                m_GMin->setValue(0);
                m_GMax->setValue(255);
            }
            else
            {
                m_LMin->setValue(0);
                m_LMax->setValue(100);
                m_AMin->setValue(-128);
                m_AMax->setValue(127);
                m_BMin->setValue(-128);
                m_BMax->setValue(127);
            }
        });

        QHBoxLayout *t_layout = new QHBoxLayout();
        t_layout->setContentsMargins(0, 0, 0, 0);
        t_layout->addWidget(new QLabel(Tr::tr("Select the best color tracking thresholds.")));
        t_layout->addWidget(temp);
        QWidget *t_widget = new QWidget();
        t_widget->setLayout(t_layout);
        layout->addWidget(t_widget);
    }

    // Grayscale //
    {
        m_paneG = new QWidget();
        QFormLayout *v_layout = new QFormLayout(m_paneG);
        v_layout->setContentsMargins(0, 0, 0, 0);

        {
            QWidget *temp = new QWidget();
            QHBoxLayout *h_layout = new QHBoxLayout(temp);
            h_layout->setContentsMargins(0, 0, 0, 0);

            m_GMin = new QSlider(Qt::Horizontal);
            m_GMin->setTickInterval(10);
            m_GMin->setTickPosition(QSlider::TicksBelow);
            m_GMin->setSingleStep(1);
            m_GMin->setPageStep(10);
            m_GMin->setRange(0, 255);
            m_GMin->setValue(0);
            h_layout->addWidget(m_GMin);
            connect(m_GMin, &QSlider::valueChanged, this, &ThresholdEditor::changed);

            QLabel *number = new QLabel(QStringLiteral("0"));
            number->setMinimumWidth(number->fontMetrics().horizontalAdvance(QStringLiteral("00000")));
            h_layout->addWidget(number);
            connect(m_GMin, &QSlider::valueChanged, number, static_cast<void (QLabel::*)(int)>(&QLabel::setNum));

            v_layout->addRow(Tr::tr("Grayscale Min"), temp);
        }

        {
            QWidget *temp = new QWidget();
            QHBoxLayout *h_layout = new QHBoxLayout(temp);
            h_layout->setContentsMargins(0, 0, 0, 0);

            m_GMax = new QSlider(Qt::Horizontal);
            m_GMax->setTickInterval(10);
            m_GMax->setTickPosition(QSlider::TicksBelow);
            m_GMax->setSingleStep(1);
            m_GMax->setPageStep(10);
            m_GMax->setRange(0, 255);
            m_GMax->setValue(255);
            h_layout->addWidget(m_GMax);
            connect(m_GMax, &QSlider::valueChanged, this, &ThresholdEditor::changed);

            QLabel *number = new QLabel(QStringLiteral("255"));
            number->setMinimumWidth(number->fontMetrics().horizontalAdvance(QStringLiteral("00000")));
            h_layout->addWidget(number);
            connect(m_GMax, &QSlider::valueChanged, number, static_cast<void (QLabel::*)(int)>(&QLabel::setNum));

            v_layout->addRow(Tr::tr("Grayscale Max"), temp);
        }

        m_GOut = new QLineEdit(QStringLiteral("(0, 255)"));
        m_GOut->setReadOnly(true);
        v_layout->addRow(Tr::tr("Grayscale Threshold"), m_GOut);

        if(!altMessage.isEmpty())
        {
            v_layout->itemAt(v_layout->rowCount() - 1, QFormLayout::LabelRole)->widget()->hide();
            v_layout->itemAt(v_layout->rowCount() - 1, QFormLayout::FieldRole)->widget()->hide();
            m_combo->hide();
        }

        layout->addWidget(m_paneG);
        if(m_combo->currentIndex()) m_paneG->hide();
    }

    // LAB //
    {
        m_paneLAB = new QWidget();
        QFormLayout *v_layout = new QFormLayout(m_paneLAB);
        v_layout->setContentsMargins(0, 0, 0, 0);

        {
            QWidget *temp = new QWidget();
            QHBoxLayout *h_layout = new QHBoxLayout(temp);
            h_layout->setContentsMargins(0, 0, 0, 0);

            m_LMin = new QSlider(Qt::Horizontal);
            m_LMin->setTickInterval(10);
            m_LMin->setTickPosition(QSlider::TicksBelow);
            m_LMin->setSingleStep(1);
            m_LMin->setPageStep(10);
            m_LMin->setRange(0, 100);
            m_LMin->setValue(0);
            h_layout->addWidget(m_LMin);
            connect(m_LMin, &QSlider::valueChanged, this, &ThresholdEditor::changed);

            QLabel *number = new QLabel(QStringLiteral("0"));
            number->setMinimumWidth(number->fontMetrics().horizontalAdvance(QStringLiteral("00000")));
            h_layout->addWidget(number);
            connect(m_LMin, &QSlider::valueChanged, number, static_cast<void (QLabel::*)(int)>(&QLabel::setNum));

            v_layout->addRow(Tr::tr("L Min"), temp);
        }

        {
            QWidget *temp = new QWidget();
            QHBoxLayout *h_layout = new QHBoxLayout(temp);
            h_layout->setContentsMargins(0, 0, 0, 0);

            m_LMax = new QSlider(Qt::Horizontal);
            m_LMax->setTickInterval(10);
            m_LMax->setTickPosition(QSlider::TicksBelow);
            m_LMax->setSingleStep(1);
            m_LMax->setPageStep(10);
            m_LMax->setRange(0, 100);
            m_LMax->setValue(100);
            h_layout->addWidget(m_LMax);
            connect(m_LMax, &QSlider::valueChanged, this, &ThresholdEditor::changed);

            QLabel *number = new QLabel(QStringLiteral("100"));
            number->setMinimumWidth(number->fontMetrics().horizontalAdvance(QStringLiteral("00000")));
            h_layout->addWidget(number);
            connect(m_LMax, &QSlider::valueChanged, number, static_cast<void (QLabel::*)(int)>(&QLabel::setNum));

            v_layout->addRow(Tr::tr("L Max"), temp);
        }

        {
            QWidget *temp = new QWidget();
            QHBoxLayout *h_layout = new QHBoxLayout(temp);
            h_layout->setContentsMargins(0, 0, 0, 0);

            m_AMin = new QSlider(Qt::Horizontal);
            m_AMin->setTickInterval(10);
            m_AMin->setTickPosition(QSlider::TicksBelow);
            m_AMin->setSingleStep(1);
            m_AMin->setPageStep(10);
            m_AMin->setRange(-128, 127);
            m_AMin->setValue(-128);
            h_layout->addWidget(m_AMin);
            connect(m_AMin, &QSlider::valueChanged, this, &ThresholdEditor::changed);

            QLabel *number = new QLabel(QStringLiteral("-128"));
            number->setMinimumWidth(number->fontMetrics().horizontalAdvance(QStringLiteral("00000")));
            h_layout->addWidget(number);
            connect(m_AMin, &QSlider::valueChanged, number, static_cast<void (QLabel::*)(int)>(&QLabel::setNum));

            v_layout->addRow(Tr::tr("A Min"), temp);
        }

        {
            QWidget *temp = new QWidget();
            QHBoxLayout *h_layout = new QHBoxLayout(temp);
            h_layout->setContentsMargins(0, 0, 0, 0);

            m_AMax = new QSlider(Qt::Horizontal);
            m_AMax->setTickInterval(10);
            m_AMax->setTickPosition(QSlider::TicksBelow);
            m_AMax->setSingleStep(1);
            m_AMax->setPageStep(10);
            m_AMax->setRange(-128, 127);
            m_AMax->setValue(127);
            h_layout->addWidget(m_AMax);
            connect(m_AMax, &QSlider::valueChanged, this, &ThresholdEditor::changed);

            QLabel *number = new QLabel(QStringLiteral("127"));
            number->setMinimumWidth(number->fontMetrics().horizontalAdvance(QStringLiteral("00000")));
            h_layout->addWidget(number);
            connect(m_AMax, &QSlider::valueChanged, number, static_cast<void (QLabel::*)(int)>(&QLabel::setNum));

            v_layout->addRow(Tr::tr("A Max"), temp);
        }

        {
            QWidget *temp = new QWidget();
            QHBoxLayout *h_layout = new QHBoxLayout(temp);
            h_layout->setContentsMargins(0, 0, 0, 0);

            m_BMin = new QSlider(Qt::Horizontal);
            m_BMin->setTickInterval(10);
            m_BMin->setTickPosition(QSlider::TicksBelow);
            m_BMin->setSingleStep(1);
            m_BMin->setPageStep(10);
            m_BMin->setRange(-128, 127);
            m_BMin->setValue(-128);
            h_layout->addWidget(m_BMin);
            connect(m_BMin, &QSlider::valueChanged, this, &ThresholdEditor::changed);

            QLabel *number = new QLabel(QStringLiteral("-128"));
            number->setMinimumWidth(number->fontMetrics().horizontalAdvance(QStringLiteral("00000")));
            h_layout->addWidget(number);
            connect(m_BMin, &QSlider::valueChanged, number, static_cast<void (QLabel::*)(int)>(&QLabel::setNum));

            v_layout->addRow(Tr::tr("B Min"), temp);
        }

        {
            QWidget *temp = new QWidget();
            QHBoxLayout *h_layout = new QHBoxLayout(temp);
            h_layout->setContentsMargins(0, 0, 0, 0);

            m_BMax = new QSlider(Qt::Horizontal);
            m_BMax->setTickInterval(10);
            m_BMax->setTickPosition(QSlider::TicksBelow);
            m_BMax->setSingleStep(1);
            m_BMax->setPageStep(10);
            m_BMax->setRange(-128, 127);
            m_BMax->setValue(127);
            h_layout->addWidget(m_BMax);
            connect(m_BMax, &QSlider::valueChanged, this, &ThresholdEditor::changed);

            QLabel *number = new QLabel(QStringLiteral("127"));
            number->setMinimumWidth(number->fontMetrics().horizontalAdvance(QStringLiteral("00000")));
            h_layout->addWidget(number);
            connect(m_BMax, &QSlider::valueChanged, number, static_cast<void (QLabel::*)(int)>(&QLabel::setNum));

            v_layout->addRow(Tr::tr("B Max"), temp);
        }

        m_LABOut = new QLineEdit(QStringLiteral("(0, 100, -128, 127, -128, 127)"));
        m_LABOut->setReadOnly(true);
        v_layout->addRow(Tr::tr("LAB Threshold"), m_LABOut);

        if(!altMessage.isEmpty())
        {
            v_layout->itemAt(v_layout->rowCount() - 1, QFormLayout::LabelRole)->widget()->hide();
            v_layout->itemAt(v_layout->rowCount() - 1, QFormLayout::FieldRole)->widget()->hide();
            m_combo->hide();
        }

        layout->addWidget(m_paneLAB);
        if(!m_combo->currentIndex()) m_paneLAB->hide();
    }

    QHBoxLayout *b_layout = new QHBoxLayout();
    b_layout->setContentsMargins(0, 0, 0, 0);
    b_layout->addWidget(new QLabel(altMessage.isEmpty() ? Tr::tr("Copy the threshold above before closing.") : altMessage));
    QDialogButtonBox *box = new QDialogButtonBox(altMessage.isEmpty() ? QDialogButtonBox::Close : (QDialogButtonBox::Ok | QDialogButtonBox::Cancel));
    b_layout->addWidget(box);
    QWidget *b_widget = new QWidget();
    b_widget->setLayout(b_layout);
    layout->addWidget(b_widget);

    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    changed();
    adjustSize();
}

void ThresholdEditor::setState(QList<QVariant> state)
{
    m_combo->setCurrentIndex(state.isEmpty() ? 0 : state.takeFirst().toInt());
    m_invert->setChecked(state.isEmpty() ? 0 : state.takeFirst().toInt());
    m_GMin->setValue(state.isEmpty() ? 0 : state.takeFirst().toInt());
    m_GMax->setValue(state.isEmpty() ? 0 : state.takeFirst().toInt());
    m_LMin->setValue(state.isEmpty() ? 0 : state.takeFirst().toInt());
    m_LMax->setValue(state.isEmpty() ? 0 : state.takeFirst().toInt());
    m_AMin->setValue(state.isEmpty() ? 0 : state.takeFirst().toInt());
    m_AMax->setValue(state.isEmpty() ? 0 : state.takeFirst().toInt());
    m_BMin->setValue(state.isEmpty() ? 0 : state.takeFirst().toInt());
    m_BMax->setValue(state.isEmpty() ? 0 : state.takeFirst().toInt());
    changed();
}

QList<QVariant> ThresholdEditor::getState() const
{
    return QList<QVariant>()
    << m_combo->currentIndex()
    << m_invert->isChecked()
    << m_GMin->value()
    << m_GMax->value()
    << m_LMin->value()
    << m_LMax->value()
    << m_AMin->value()
    << m_AMax->value()
    << m_BMin->value()
    << m_BMax->value();
}

} // namespace Internal
} // namespace OpenMV
