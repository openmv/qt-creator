/* Copyright (C) 2026 OpenMV, LLC.
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

// Channels view for the histogram pane's view selector: a live browser for
// the SenML-CBOR channels a running script publishes, mirroring OpenMV
// Studio's Channels tab. Records render by their widget type: value labels
// (with units), depth-map images (Turbo colormap), and -- on writable
// channels -- toggles, sliders, spinboxes (precise stepped entry), radio
// buttons, line edits, selects, and pushbuttons (momentary actions) that
// write back to the script, plus static rich text ("text"). The control
// vocabulary matches the Settings Editor's element names where they exist
// there ("label" keeps Studio's meaning: a name/value readout row).
// Fed by OpenMVPluginIO::readChannels() polling while the view is visible;
// control changes are emitted through writeChannel() and the next render of
// that channel is skipped so an in-flight pre-write read can't snap a
// control back.

#ifndef OPENMVCHANNELSVIEW_H
#define OPENMVCHANNELSVIEW_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

namespace OpenMV {
namespace Internal {

// Renders a WxH float32 buffer through the Turbo colormap, scaled to the
// view's width with the aspect ratio kept.
class OpenMVChannelDepth : public QWidget
{
    Q_OBJECT

public:

    explicit OpenMVChannelDepth(QWidget *parent = Q_NULLPTR);

    void setData(const QByteArray &data, int width, int height, double min, double max);

    virtual bool hasHeightForWidth() const { return true; }
    virtual int heightForWidth(int width) const;

protected:

    virtual void paintEvent(QPaintEvent *event);

private:

    QImage m_image;
};

// Streams waveform chunks to CSV. This is a data-collection instrument,
// not display eye candy: one row per sample with a device timestamp,
// values written exactly (integer typecodes stay integral, floats keep
// round-trip precision), flushed per chunk so a crash can't lose recorded
// data. The output splits into numbered part files before any one grows
// unwieldy.
//
// The file holds one or more tracks -- one per recorded graph, so a
// per-graph recording is a single track and "Record All" is one track per
// graph, all sharing the time column. A waveform track writes one row per
// sample: its series columns plus a "gap" column reporting
// discontinuities between its chunks in seconds (missed chunks, bursty
// publishers; empty when a chunk continues exactly where the previous one
// ended). A depth track writes one row per frame with one column per
// value and no gap column.
class OpenMVChannelRecorder : public QObject
{
    Q_OBJECT

public:

    // A recorded graph's column group: its name prefix, column count
    // (series for a waveform, values for a depth frame), and whether it
    // carries a gap column. A single track with an empty name writes the
    // plain value/series_N column names.
    struct Track {
        QString name;
        int columns = 1;
        bool gap = true;
    };

    explicit OpenMVChannelRecorder(const QString &path, const QList<Track> &tracks,
                                   QObject *parent = Q_NULLPTR);
    virtual ~OpenMVChannelRecorder();

    bool ok() const { return m_file.isOpen(); }
    QString errorString() const { return m_error; }

    // Appends one chunk to a track. The poll re-reads an unchanged chunk
    // faster than scripts publish, so duplicates are detected (by timestamp
    // and data) and skipped. Returns false when a write fails;
    // errorString() says why.
    bool append(int track, const QByteArray &data, const QString &typecode,
                int samples, int series, double t, double period);

    QString status() const;

private:

    bool openPart();
    bool rollPart();
    bool writeHeader();
    QString partPath(int part) const;

    struct TrackState {
        int offset = 0; // this track's first CSV column after time
        bool hasLast = false;
        double lastT = 0.0;
        QByteArray lastData;
        bool hasChunkTiming = false;
        double chunkEnd = 0.0;
    };

    QString m_basePath;
    QFile m_file;
    QString m_error;
    QList<Track> m_tracks;
    QList<TrackState> m_states;
    int m_totalColumns = 0; // series + gap columns across all tracks
    int m_part = 0;
    qint64 m_partRows = 0;
    qint64 m_totalRows = 0;
    qint64 m_totalBytes = 0;
    QElapsedTimer m_hostClock;
};

// Renders interleaved 1D sample series (mic/IMU waveforms): w samples per
// series, h series overlaid as separate traces, scaled to [min, max].
class OpenMVChannelWaveform : public QWidget
{
    Q_OBJECT

public:

    explicit OpenMVChannelWaveform(QWidget *parent = Q_NULLPTR);

    void setData(const QByteArray &data, const QString &typecode,
                 int samples, int series, double min, double max);

protected:

    virtual void paintEvent(QPaintEvent *event);

private:

    QVector<float> m_samples; // interleaved
    int m_seriesCount = 1;
    int m_samplesPerSeries = 0;
    double m_min = 0.0;
    double m_max = 1.0;
};

class OpenMVChannelsView : public QStackedWidget
{
    Q_OBJECT

public:

    explicit OpenMVChannelsView(QWidget *parent = Q_NULLPTR);

public slots:

    // One map per channel: name, flags, and the raw CBOR payload. An empty
    // list shows the "no channels" message; reset() shows the "connect"
    // message and drops all per-session state.
    void channelsData(const QVariantList &channels);
    void reset();

signals:

    // A control changed: raw CBOR write payload for the named channel.
    void writeChannel(const QString &name, const QByteArray &data);

private:

    // One decoded SenML record and the widgets rendering it.
    struct Record {
        QString channelName;
        int channelId = 0;
        quint8 flags = 0;
        QString name;
        QString wtype;
        QCborMap rec;

        // Widget refs by type (only the relevant ones are non-null).
        QWidget *row = Q_NULLPTR;
        QLabel *value = Q_NULLPTR;
        QCheckBox *toggle = Q_NULLPTR;
        QSlider *slider = Q_NULLPTR;
        QLabel *sliderValue = Q_NULLPTR;
        double sliderMin = 0.0;
        double sliderStep = 1.0;
        QDoubleSpinBox *spinbox = Q_NULLPTR;
        QButtonGroup *radio = Q_NULLPTR;
        QLineEdit *lineedit = Q_NULLPTR;
        QComboBox *select = Q_NULLPTR;
        QPushButton *pushbutton = Q_NULLPTR;
        OpenMVChannelDepth *depth = Q_NULLPTR;
        QLabel *depthHeader = Q_NULLPTR;
        OpenMVChannelWaveform *waveform = Q_NULLPTR;
        QLabel *waveformHeader = Q_NULLPTR;
        QPushButton *recordButton = Q_NULLPTR;
        QLabel *recordStatus = Q_NULLPTR;
        OpenMVChannelRecorder *recorder = Q_NULLPTR; // owned by the row widget
        int sectionIndex = -1; // this waveform's channel section (Record All)
        int trackIndex = -1;   // its track in that section's group recording
    };

    // A channel section's Record All bar: captures every waveform in the
    // section into one multi-track file. Mutually exclusive with the
    // per-graph buttons.
    struct Section {
        QString channelName;
        QWidget *bar = Q_NULLPTR;
        QPushButton *button = Q_NULLPTR;
        QLabel *status = Q_NULLPTR;
        OpenMVChannelRecorder *recorder = Q_NULLPTR; // owned by the bar widget
    };

    void showMessage(const QString &message);
    void clearContent();
    QList<Record> decode(const QVariantList &channels, QString *schema) const;
    void buildContent(QList<Record> &records);
    void patchContent(QList<Record> &records);
    void stageWrite(const QString &channelName, const QString &recordName, const QCborValue &value);
    void toggleRecording(int index);
    void toggleGroupRecording(int index);
    void updateRecordButtonStates();
    OpenMVChannelRecorder::Track trackFor(const Record &record, bool named) const;

    QLabel *m_message;
    QVBoxLayout *m_contentLayout;
    QString m_schema;
    QList<Record> m_records;
    QList<Section> m_sections;

    // Controls mid-interaction (slider drags) that patches must not fight.
    QSet<QString> m_activeControls;

    // Channels with a write in flight: skip rendering them until the count
    // drains so a pre-write read can't snap the control back.
    QMap<QString, int> m_skipRenders;

    // Controls with a write in flight, keyed "channel/name" -> (written value,
    // deadline ms). The control is held at the written value until the device
    // echoes it back (or the deadline passes), so a pre-write read still in
    // flight can't snap it back -- which showed as a checkbox flicking off then
    // on. Longer-lived and per-control where m_skipRenders is a coarse count.
    QHash<QString, QPair<QCborValue, qint64>> m_pendingWrites;
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVCHANNELSVIEW_H
