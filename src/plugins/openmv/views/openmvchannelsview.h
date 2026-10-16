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

#include "openmvchannelrecorder.h"

#include "../qcustomplot/qcustomplot.h"

namespace OpenMV {
namespace Internal {

// Renders a WxH float32 buffer through one of the camera's own colour
// palettes, scaled to fit the row with the aspect ratio kept. The scaling
// is done on the depth values and the result looked up in the palette, so a
// smoothed map never blends two palette colours into a third that is not in
// it. A tone mapping curve decides how the values spread over the palette:
// the declared range keeps colours comparable between frames, the adaptive
// curves show the most relief in whatever the frame holds.
class OpenMVChannelDepth : public QWidget
{
    Q_OBJECT

public:

    // In the order the camera's image module lists them.
    enum Palette { DepthPalette, RainbowPalette, IronbowPalette, EventDarkPalette, EventLightPalette };
    enum Scaling { NearestScaling, BilinearScaling, BicubicScaling };
    // Range: the declared min and max, comparable between frames. Auto:
    // the frame's own range. Equalize: clipped, tiled histogram
    // equalization (CLAHE), which shows the most relief without turning a
    // flat surface's noise into contrast.
    enum ToneMap { RangeMap, AutoMap, EqualizeMap };

    explicit OpenMVChannelDepth(QWidget *parent = Q_NULLPTR);

    void setData(const QByteArray &data, int width, int height, double min, double max);

    // The record's own name, for the exported image's file name.
    void setTitle(const QString &title) { m_title = title; }

    // Identifies this map in the settings, as "<channel>/<record>"; its
    // saved image path is remembered per map.
    void setSettingsKey(const QString &key) { m_settingsKey = key; }

    void setColorPalette(int palette);
    void setScaling(int scaling);
    void setToneMap(int toneMap);
    int colorPalette() const { return m_palette; }
    int scaling() const { return m_scaling; }
    int toneMap() const { return m_toneMap; }

    // Freezing the display never affects recording.
    void setPaused(bool paused) { m_paused = paused; }

    // The newest frame's statistics, for the header above the map. A
    // paused map keeps reporting the frame it is showing.
    double frameMin() const { return m_frameMin; }
    double frameMax() const { return m_frameMax; }
    double frameMean() const { return m_frameMean; }

    // The clipped, eased range the adaptive curves work from.
    double rangeLow() const { return m_rangeLow; }
    double rangeHigh() const { return m_rangeHigh; }
    QJsonObject describe(bool includeData) const; // size, ranges and (optionally) the values, for MCP

protected:

    virtual void paintEvent(QPaintEvent *event);
    virtual void mouseMoveEvent(QMouseEvent *event);
    virtual void leaveEvent(QEvent *event);
    virtual void contextMenuEvent(QContextMenuEvent *event);

private:

    // The map at a point in source pixel units (centres at .5), clamped to
    // the edges, through the chosen filter.
    double sample(double x, double y) const;

    // The lookup tables the adaptive curves need for the current frame.
    void prepareMapping();

    // A value's position along the palette, 0 near to 1 far, through the
    // chosen curve. x and y are where it sits in the map, for the local
    // curve.
    double mapValue(double value, double x, double y) const;

    QImage render(const QSize &size) const;
    QRect imageRect() const; // where the map sits in the row
    void saveImage();

    QVector<float> m_values;
    int m_width = 0;
    int m_height = 0;
    double m_min = 0.0;
    double m_max = 1.0;
    double m_frameMin = 0.0; // the newest frame's own range and mean
    double m_frameMax = 0.0;
    double m_frameMean = 0.0;

    // The adaptive curves' working range: the frame's values with the
    // extremes clipped off, eased from frame to frame so a stray pixel or
    // sensor noise cannot swing the colours.
    bool m_rangeValid = false;
    double m_rangeLow = 0.0;
    double m_rangeHigh = 1.0;
    QVector<double> m_tileLuts; // Equalize: palette position per value bin, per tile
    int m_tilesX = 1;
    int m_tilesY = 1;

    int m_palette = DepthPalette;
    int m_scaling = BicubicScaling;
    int m_toneMap = RangeMap;
    bool m_paused = false;
    bool m_hovering = false;
    QPoint m_cursor;
    QString m_title;
    QString m_settingsKey;
    QImage m_cache; // the last render, kept while nothing has changed
};

// Live scrolling plot of interleaved 1D sample series (mic/IMU waveforms):
// w samples per series, h series overlaid as separate traces, scaled to
// [min, max]. Chunks append end to end into a rolling history, so the plot
// shows a continuous stream rather than only the newest chunk, and the view
// can be panned and zoomed back through it.
//
// Built on QCustomPlot for the parts that are hard to hand-roll: adaptive
// sampling (what makes a 16 kHz trace affordable to draw), drag/zoom, and
// the spectrum view. It deliberately does not look like a chart -- the
// resting state is the full-bleed trace the hand-painted widget it replaced
// drew, with subtle ticks; the readout, cursor and axes appear on demand.
class OpenMVChannelWaveform : public QCustomPlot
{
    Q_OBJECT

public:

    explicit OpenMVChannelWaveform(QWidget *parent = Q_NULLPTR);

    // Appends one chunk. t is the device chunk timestamp in seconds (NaN when
    // the sender omits it) and period the sample period in seconds (0 when the
    // sender publishes no rate, in which case the x axis counts samples).
    void setData(const QByteArray &data, const QString &typecode,
                 int samples, int series, double min, double max,
                 double period, double t, const QStringList &names);

    // The record's own name, used to label a single trace.
    void setTitle(const QString &title) { m_title = title; }

    // Identifies this graph in the settings, as "<channel>/<record>". Its
    // saved image path and its auto scale preference are both remembered per
    // graph, so several plots in one session each keep their own.
    void setSettingsKey(const QString &key);

    // The view modes, driven from the buttons beside the record bar.
    void setSpectrum(bool enabled);
    void setShowStats(bool enabled);

    // Freezes the plot. Recording is unaffected -- this is a display control,
    // so resuming starts a fresh trace rather than splicing the stream back
    // together across the samples that were skipped.
    void setPaused(bool paused);

signals:

    // The trigger pauses the graph itself, so the Pause button has to hear
    // about a freeze it did not initiate.
    void pausedChanged(bool paused);

public:
    bool spectrum() const { return m_spectrum; }
    QJsonObject describe(bool includeData, int maxSamples) const; // series, ranges and (optionally) the newest samples, for MCP

protected:

    virtual void changeEvent(QEvent *event);
    virtual void leaveEvent(QEvent *event);

private:

    void applyTheme();

    // Fits the vertical axis to what is on screen, when the graph is set to
    // scale itself rather than to the range the record declares.
    void updateVerticalRange();

    // The corner numbers always describe what is actually drawn, so they
    // follow the axis rather than the declared range.
    void updateRangeLabels();

    // Watches the newest samples for the armed crossing, and freezes the
    // graph on the one that crosses.
    void checkTrigger(const QVector<double> &keys, const QVector<double> &values);
    void updateTriggerItems();
    void armTrigger(bool armed);

    // The corner readout: values under the cursor while the mouse is over the
    // plot, statistics for the visible window when it is not.
    void updateReadout();

    // Axes, tickers and which set of traces is shown, for the current domain.
    void applyMode();

    // Trades line quality for speed, but only once the window holds more
    // points than the pane can resolve.
    void updateDrawQuality();

    // Windowed FFT of the newest samples of each series, in dBFS against the
    // record's own display range.
    void computeSpectrum();

    void showContextMenu(const QPoint &pos);
    void saveImage();
    void resetHistory(int series, double min, double max, double period,
                      const QStringList &names);
    void setSeriesVisible(int series, bool visible);

    // The visible window when following, and how much history is kept behind
    // it, in seconds (or in chunks when the sender publishes no rate).
    double viewSpan() const;

    int m_seriesCount = 0;
    int m_samplesPerChunk = 0;
    double m_min = 0.0;
    double m_max = 1.0;
    double m_period = 0.0;
    bool m_timeAxis = false; // x is seconds (period known) vs sample counts
    double m_next = 0.0;     // where the next chunk starts when t is absent
    double m_end = 0.0;      // right edge of the newest data
    bool m_hasData = false;
    bool m_following = true; // auto-scroll until the user pans or zooms
    double m_lastT = qQNaN();
    QByteArray m_lastData;
    QString m_typecode;
    QStringList m_seriesNames;
    QString m_title;
    QString m_settingsKey;

    QCPItemStraightLine *m_cursor = Q_NULLPTR;

    // A second cursor, left where the user put it, so the readout can measure
    // the distance to the one under the pointer rather than only report a
    // value at it.
    QCPItemStraightLine *m_pinned = Q_NULLPTR;
    bool m_hasPinned = false;
    double m_pinnedKey = 0.0;
    QCPItemText *m_readout = Q_NULLPTR;
    QCPItemText *m_maxLabel = Q_NULLPTR; // display range, inset in the corners
    QCPItemText *m_minLabel = Q_NULLPTR;
    QList<QCPItemTracer *> m_tracers; // one per series, shown under the cursor
    bool m_hovering = false;
    bool m_showStats = false; // window statistics in the readout, off by default
    bool m_paused = false;
    bool m_autoScale = true;  // fit the axis to the data, not to min/max
    double m_history = 50.0;  // how many screens of history to keep behind
    double m_cursorKey = 0.0;

    // Traces are held twice over: graph(s) is the time domain and
    // graph(seriesCount + s) its spectrum, so switching domains does not
    // discard the history the spectrum is computed from.
    // How the spectrum is taken. A single unaveraged frame of a noisy signal
    // jumps around too much to read a floor off, and a rectangular window
    // smears every tone across its neighbours, so both are adjustable.
    enum SpectrumWindow { HannWindow, HammingWindow, BlackmanWindow, RectangularWindow };

    bool m_spectrum = false;
    int m_spectrumWindow = HannWindow;
    bool m_spectrumAveraging = false;
    bool m_spectrumPeakHold = false;
    bool m_logFrequency = false;
    QVector<QVector<double>> m_spectrumHeld; // averaged or held bins, per series
    double m_nyquist = 0.0;
    int m_fftSize = 0;
    QSet<int> m_hiddenSeries; // traces the user switched off in the legend

    // Capture on a level crossing, the way a scope does it: arm, and the next
    // crossing freezes the graph with the event a quarter of the way in, so
    // there is a run of samples on either side of it to read.
    bool m_triggerArmed = false;
    bool m_triggered = false;
    bool m_triggerRising = true;
    bool m_triggerHasLevel = false;
    bool m_triggerHasPrevious = false;
    int m_triggerSeries = 0;
    double m_triggerLevel = 0.0;
    double m_triggerPrevious = 0.0; // carried across chunks, so a crossing at
                                    // a chunk boundary is not missed
    bool m_triggerFilling = false;  // crossed, still taking samples until the
    double m_triggerFillKey = 0.0;  // window to the right of it has filled
    QCPItemStraightLine *m_triggerLine = Q_NULLPTR;
    QCPItemStraightLine *m_triggerMark = Q_NULLPTR;

    // Statistics are smoothed before display: recomputed against a sliding
    // window at the poll rate, the raw digits flicker too fast to read.
    struct SeriesStats {
        bool valid = false;
        double low = 0.0;  // spectrum: the peak's key
        double high = 0.0; // spectrum: the peak's level
        double rms = 0.0;  // spectrum: unused
    };

    QList<SeriesStats> m_stats;
};

class OpenMVChannelsView : public QStackedWidget
{
    Q_OBJECT

public:

    explicit OpenMVChannelsView(QWidget *parent = Q_NULLPTR);

public slots:

    // One map per channel: name, flags, and the raw CBOR payload. An empty
    // list freezes what is showing (the script stopped, or has not
    // registered anything yet); reset() shows the "connect" message and
    // drops all per-session state.
    void channelsData(const QVariantList &channels);
    void reset();

    // Drops what is showing: the next script's channels start from a blank
    // view rather than over the previous script's stale controls.
    void clear();

    // Keeps what is showing but disables every control that would write
    // to the camera, and closes any captures. The view thaws by itself when
    // channel data arrives again.
    void freeze();

    // The connected board, named in the files a recording writes: Edge
    // Impulse groups samples by the device they came off.
    void setDevice(const QString &type, const QString &id);

public:

    // Everything the pane shows, as JSON: one entry per record with its
    // channel, name, widget type, current value / control state and, for
    // waveform and depth rows, the newest data. For MCP.
    QJsonArray snapshot(bool includeData, int maxSamples) const;

    // Stage a control write as if the user had changed it. False when the
    // pane is frozen (no script running) or no such control exists.
    bool write(const QString &channelName, const QString &recordName, const QJsonValue &value);

    // Record a waveform/depth record to `path` (format from its extension, as
    // the row's Record button offers) and stop again. For MCP.
    bool startRecording(const QString &channelName, const QString &recordName, const QString &path, QString *error);
    bool stopRecording(const QString &channelName, const QString &recordName, QString *status);

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
        bool groupStarted = false; // started by its section's Record All
        int sectionIndex = -1;     // this waveform's channel section
    };

    // A channel section's Record All bar: starts and stops every graph in the
    // section together. Each still writes its own file -- the point is the
    // shared instant, not a shared table, since tracks at different rates
    // have no rows in common. Mutually exclusive with the per-graph buttons.
    struct Section {
        QString channelName;
        QWidget *bar = Q_NULLPTR;
        QPushButton *button = Q_NULLPTR;
        QLabel *status = Q_NULLPTR;
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
    void syncSectionBar(int index);
    void applyControlStates();
    OpenMVChannelRecorder::Info infoFor(const Record &record) const;
    bool startRecorder(Record &record, const QString &path,
                       OpenMVChannelRecorder::Format format, bool group);
    void stopRecorder(Record &record);

    QLabel *m_message;
    QVBoxLayout *m_contentLayout;
    QString m_schema;
    QString m_deviceType;
    QString m_deviceId;
    QList<Record> m_records;
    QList<Section> m_sections;
    bool m_frozen = false; // showing a stopped script's last state

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
