#ifndef THRESHOLDEDITOR_H
#define THRESHOLDEDITOR_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

namespace OpenMV {
namespace Internal {

class ThresholdEditor : public QDialog
{
    Q_OBJECT

public:

    explicit ThresholdEditor(const QPixmap &pixmap, QByteArray geometry, QWidget *parent = Q_NULLPTR, Qt::WindowFlags f = Qt::WindowFlags(), const QString &altMessage = QString());
    void setState(QList<QVariant> state);
    QList<QVariant> getState() const;
    void setCombo(int combo) { m_combo->setCurrentIndex(combo); changed(); }
    void setInvert(bool invert) { m_invert->setChecked(invert); changed(); }
    void setGMin(int GMin) { m_GMin->setValue(GMin); changed(); }
    void setGMax(int GMax) { m_GMax->setValue(GMax); changed(); }
    void setLMin(int LMin) { m_LMin->setValue(LMin); changed(); }
    void setLMax(int LMax) { m_LMax->setValue(LMax); changed(); }
    void setAMin(int AMin) { m_AMin->setValue(AMin); changed(); }
    void setAMax(int AMax) { m_AMax->setValue(AMax); changed(); }
    void setBMin(int BMin) { m_BMin->setValue(BMin); changed(); }
    void setBMax(int BMax) { m_BMax->setValue(BMax); changed(); }
    int getGMin() const { return m_GMin->value(); }
    int getGMax() const { return m_GMax->value(); }
    int getLMin() const { return m_LMin->value(); }
    int getLMax() const { return m_LMax->value(); }
    int getAMin() const { return m_AMin->value(); }
    int getAMax() const { return m_AMax->value(); }
    int getBMin() const { return m_BMin->value(); }
    int getBMax() const { return m_BMax->value(); }

public slots:

    void changed();

protected:

    void resizeEvent(QResizeEvent *event);
    void showEvent(QShowEvent *event);

private:

    QImage m_image;
    QGraphicsView *m_raw, *m_bin;
    QComboBox *m_combo;
    QCheckBox *m_invert;
    QWidget *m_paneG, *m_paneLAB;
    QSlider *m_GMin, *m_GMax, *m_LMin, *m_LMax, *m_AMin, *m_AMax, *m_BMin, *m_BMax;
    QLineEdit *m_GOut, *m_LABOut;
    QByteArray m_geometry;
};

} // namespace Internal
} // namespace OpenMV

#endif // THRESHOLDEDITOR_H
