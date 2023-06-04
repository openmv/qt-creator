#ifndef OPENMVPLUGINFB_H
#define OPENMVPLUGINFB_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include "tools/videotools.h"

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
    void frameBufferData(const QPixmap &data);
    void enableSaveTemplate(bool enable) { m_enableSaveTemplate = enable; }
    void enableSaveDescriptor(bool enable) { m_enableSaveDescriptor = enable; }
    void private_timerCallBack();

signals:

    void pixmapUpdate(const QPixmap &data);
    void resolutionAndROIUpdate(const QSize &res, const QRect &roi);
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

    bool m_unlocked;
    QPoint m_origin;
    QRubberBand *m_band;

    QTimer *m_timer;
    QTemporaryFile *m_tempFile;
    QElapsedTimer m_elaspedTimer;
    QQueue<qint64> m_previousElaspedTimers;
};

#endif // OPENMVPLUGINFB_H
