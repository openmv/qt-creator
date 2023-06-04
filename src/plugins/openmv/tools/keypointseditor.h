#ifndef KEYPOINTSEDITOR_H
#define KEYPOINTSEDITOR_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <utils/fileutils.h>

#define KEYPOINT_DATA_SIZE 32

class Keypoint : public QObject
{
    Q_OBJECT

public:

    explicit Keypoint(QObject *parent = Q_NULLPTR) : QObject(parent)
    {
        m_x = 0;
        m_y = 0;
        m_score = 0;
        m_octave = 0;
        m_angle = 0;
        m_normalized = 0;
        memset(m_data, 0, KEYPOINT_DATA_SIZE);
    }

    quint16 m_x, m_y, m_score, m_octave, m_angle, m_normalized;
    char m_data[KEYPOINT_DATA_SIZE];
};

class Keypoints : public QObject
{
    Q_OBJECT

public:

    explicit Keypoints(QObject *parent = Q_NULLPTR) : QObject(parent)
    {
        m_type = 0;
        m_size = 0;
        m_max_octave = 1;
    }

    static Keypoints *newKeypoints(const QString &path, QObject *parent = Q_NULLPTR);
    void mergeKeypoints(const QString &path);
    bool saveKeypoints(const QString &path);

    quint32 m_type, m_size;
    quint16 m_max_octave;
};

class KeypointsItem : public QGraphicsItem
{

public:

    explicit KeypointsItem(Keypoint *k, int size, QGraphicsItem * parent = Q_NULLPTR) : QGraphicsItem(parent)
    {
        setAcceptHoverEvents(true);
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        m_k = k;
        m_size = size / sqrt(m_k->m_octave);
    }

    QRectF boundingRect() const
    {
        double minX = m_k->m_x - (m_size / 2.0);
        double maxX = m_k->m_x + (m_size / 2.0);
        double minY = m_k->m_y - (m_size / 2.0);
        double maxY = m_k->m_y + (m_size / 2.0);
        minX = qMin(minX, m_k->m_x + (cos((m_k->m_angle * M_PI) / 180.0) * m_size));
        maxX = qMax(maxX, m_k->m_x + (cos((m_k->m_angle * M_PI) / 180.0) * m_size));
        minY = qMin(minY, m_k->m_y - (sin((m_k->m_angle * M_PI) / 180.0) * m_size));
        maxY = qMax(maxY, m_k->m_y - (sin((m_k->m_angle * M_PI) / 180.0) * m_size));
        return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
    {
        Q_UNUSED(option)
        Q_UNUSED(widget)

        QPointF p0 = QPointF(m_k->m_x,
                             m_k->m_y);
        QPointF p1 = QPointF(m_k->m_x + (cos((m_k->m_angle * M_PI) / 180.0) * m_size),
                             m_k->m_y - (sin((m_k->m_angle * M_PI) / 180.0) * m_size));
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(isSelected() ? Qt::green : Qt::white, 0));
        painter->drawEllipse(p0, m_size / 2.0, m_size / 2.0);
        painter->drawLine(p0, p1);
        painter->restore();
    }

    enum KeypointsItemType
    {
        Type = UserType + 1
    };

    int type() const
    {
        return Type;
    }

    Keypoint *m_k;
    int m_size;

protected:

    void hoverEnterEvent(QGraphicsSceneHoverEvent *event)
    {
        setToolTip(QStringLiteral("x: %L1, y: %L2, score: %L3, octave: %L4, angle: %L5").arg(m_k->m_x).arg(m_k->m_y).arg(m_k->m_score).arg(m_k->m_octave).arg(m_k->m_angle));
        QGraphicsItem::hoverEnterEvent(event);
    }

    void hoverMoveEvent(QGraphicsSceneHoverEvent *event)
    {
        setToolTip(QStringLiteral("x: %L1, y: %L2, score: %L3, octave: %L4, angle: %L5").arg(m_k->m_x).arg(m_k->m_y).arg(m_k->m_score).arg(m_k->m_octave).arg(m_k->m_angle));
        QGraphicsItem::hoverMoveEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
    {
        setToolTip(QString());
        QGraphicsItem::hoverLeaveEvent(event);
    }
};

class KeypointsView : public QGraphicsView
{
    Q_OBJECT

public:

    explicit KeypointsView(Keypoints *keypoints, const QPixmap &pixmap, QWidget *parent = Q_NULLPTR);

protected:

    void keyPressEvent(QKeyEvent *event);

    void resizeEvent(QResizeEvent *event);
};

class KeypointsEditor : public QDialog
{
    Q_OBJECT

public:

    explicit KeypointsEditor(Keypoints *keypoints, const QPixmap &pixmap, QByteArray geometry, QWidget *parent = Q_NULLPTR, Qt::WindowFlags f = Qt::WindowFlags());

protected:

    void showEvent(QShowEvent *event);

private:

    QByteArray m_geometry;
};

#endif // KEYPOINTSEDITOR_H
