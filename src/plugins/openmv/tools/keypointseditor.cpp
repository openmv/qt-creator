#include "keypointseditor.h"

#include <utils/theme/theme.h>

Keypoints *Keypoints::newKeypoints(const QString &path, QObject *parent)
{
    QFile file(path);

    if(file.open(QIODevice::ReadOnly))
    {
        QByteArray data = file.readAll();

        if((file.error() == QFile::NoError) && (!data.isEmpty()))
        {
            file.close();

            QDataStream stream(data);
            stream.setByteOrder(QDataStream::LittleEndian);
            stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
            Keypoints *ks = new Keypoints(parent);
            stream >> ks->m_type;
            stream >> ks->m_size;

            for(uint i = 0; (i < ks->m_size) && (!stream.atEnd()); i++)
            {
                Keypoint *k = new Keypoint(ks);

                stream >> k->m_x;
                stream >> k->m_y;
                stream >> k->m_score;
                stream >> k->m_octave;
                stream >> k->m_angle;
                k->m_normalized = false;
                stream.readRawData(k->m_data, KEYPOINT_DATA_SIZE);

                ks->m_max_octave = qMax(ks->m_max_octave, k->m_octave);
            }

            return ks;
        }
    }

    return Q_NULLPTR;
}

void Keypoints::mergeKeypoints(const QString &path)
{
    QFile file(path);

    if(file.open(QIODevice::ReadOnly))
    {
        QByteArray data = file.readAll();

        if((file.error() == QFile::NoError) && (!data.isEmpty()))
        {
            file.close();

            QDataStream stream(data);
            stream.setByteOrder(QDataStream::LittleEndian);
            stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
            quint32 type, size;
            stream >> type;
            stream >> size;

            if(type == m_type)
            {
                m_size += size;

                for(uint i = 0; (i < size) && (!stream.atEnd()); i++)
                {
                    Keypoint *k = new Keypoint(this);

                    stream >> k->m_x;
                    stream >> k->m_y;
                    stream >> k->m_score;
                    stream >> k->m_octave;
                    stream >> k->m_angle;
                    k->m_normalized = false;
                    stream.readRawData(k->m_data, KEYPOINT_DATA_SIZE);

                    m_max_octave = qMax(m_max_octave, k->m_octave);
                }
            }
        }
    }
}

bool Keypoints::saveKeypoints(const QString &path)
{
    Utils::FileSaver file(Utils::FilePath::fromString(path));

    if(!file.hasError())
    {
        QByteArray data;
        QDataStream stream(&data, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        stream << m_type;
        stream << m_size;

        foreach(const QObject *o, children())
        {
            Keypoint *k = const_cast<Keypoint *>(static_cast<const Keypoint *>(o));

            stream << k->m_x;
            stream << k->m_y;
            stream << k->m_score;
            stream << k->m_octave;
            stream << k->m_angle;
            stream.writeRawData(k->m_data, KEYPOINT_DATA_SIZE);
        }

        return file.write(data) && file.finalize();
    }

    return false;
}

KeypointsView::KeypointsView(Keypoints *keypoints, const QPixmap &pixmap, QWidget *parent) : QGraphicsView(parent)
{
    setDragMode(QGraphicsView::RubberBandDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMinimumWidth(160);
    setMinimumHeight(120);
    setBackgroundBrush(Utils::creatorTheme()->color(Utils::Theme::BackgroundColorDark));
    setScene(new QGraphicsScene(this));
    scene()->addPixmap(pixmap);

    foreach(const QObject *o, keypoints->children())
    {
        scene()->addItem(new KeypointsItem(const_cast<Keypoint *>(static_cast<const Keypoint *>(o)), sqrt((pixmap.width()*pixmap.width())+(pixmap.height()*pixmap.height()))/20)); // sqrt((160*160)+(120*120))/20=10
    }

    qreal scale = qMin(width() / sceneRect().width(), height() / sceneRect().height());
    setTransform(QTransform(1, 0, 0, 0, 1, 0, 0, 0, 1).scale(scale, scale));
}

void KeypointsView::keyPressEvent(QKeyEvent *event)
{
    switch(event->key())
    {
        case Qt::Key_Delete:
        {
            foreach(QGraphicsItem *item, scene()->selectedItems())
            {
                KeypointsItem *k = qgraphicsitem_cast<KeypointsItem *>(item);

                if(k)
                {
                    static_cast<Keypoints *>(k->m_k->parent())->m_size -= 1;
                    delete k->m_k;
                    delete k;
                }
            }

            break;
        }
    }

    QGraphicsView::keyPressEvent(event);
}

void KeypointsView::resizeEvent(QResizeEvent *event)
{
    qreal rawScale = qMin(width() / sceneRect().width(), height() / sceneRect().height());
    setTransform(QTransform(1, 0, 0, 0, 1, 0, 0, 0, 1).scale(rawScale, rawScale));
    QGraphicsView::resizeEvent(event);
}

void KeypointsEditor::showEvent(QShowEvent *event)
{
    restoreGeometry(m_geometry);

    QDialog::showEvent(event);
}

KeypointsEditor::KeypointsEditor(Keypoints *keypoints, const QPixmap &pixmap, QByteArray geometry, QWidget *parent, Qt::WindowFlags f) : QDialog(parent, f)
{
    m_geometry = geometry;
    setWindowTitle(tr("Keypoints Editor"));

    QVBoxLayout *layout = new QVBoxLayout(this);
    KeypointsView *view = new KeypointsView(keypoints, pixmap);
    layout->addWidget(view);
    layout->addWidget(new QLabel(tr("Select and delete outlier keypoints using both the mouse and the delete key.")));

    if(keypoints->m_max_octave > 1)
    {
        for(int i = 1; i <= keypoints->m_max_octave; i++)
        {
            QCheckBox *box = new QCheckBox(tr("Show Octave %L1").arg(i));
            box->setCheckable(true);
            box->setChecked(true);
            layout->addWidget(box);
            connect(box, &QCheckBox::toggled, this, [view, i] (bool checked) {
                foreach(QGraphicsItem *item, view->scene()->items())
                {
                    KeypointsItem *k = qgraphicsitem_cast<KeypointsItem *>(item);

                    if(k && (k->m_k->m_octave == i))
                    {
                        k->setVisible(checked);
                    }
                }
            });
        }
    }

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton *done = new QPushButton(tr("Done"));
    box->addButton(done, QDialogButtonBox::AcceptRole);
    layout->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
