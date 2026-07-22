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

// Streams one channel record's data to a file, in whichever format the save
// dialog's filter selected. This is a data-collection instrument, not display
// eye candy: values are written exactly (integer typecodes stay integral,
// float32 keeps round-trip precision) and every chunk is flushed, so a crash
// cannot lose recorded data. Output splits into numbered part files before any
// one grows unwieldy.
//
// A recorder holds exactly one record. Capturing several at once is several
// recorders started together, each writing its own file -- the tracks of a
// multi-rate capture are genuinely separate streams, and interleaving them
// into one table would imply a sample alignment that was never measured.
//
// Half the formats exist so a capture can go straight into a training
// pipeline. Edge Impulse ingests .wav, .csv, .json and .cbor, and its
// acquisition format wants SenML units -- which the channel protocol already
// speaks, so a record's unit passes through untranslated. Their CSV wants a
// first column literally named "timestamp" holding milliseconds at a constant
// interval, so that is the CSV written -- a capture is more useful ingestible
// than in a house format.

#ifndef OPENMVCHANNELRECORDER_H
#define OPENMVCHANNELRECORDER_H

#include <QtCore>

namespace OpenMV {
namespace Internal {

class OpenMVChannelRecorder : public QObject
{
    Q_OBJECT

public:

    enum Format {
        Csv,            // timestamp (ms, constant interval) + one column per axis
        Wav,            // 16-bit PCM, one WAV channel per series
        JsonEdgeImpulse,
        CborEdgeImpulse,
        Npy,            // the samples as they arrived, in their own dtype
    };

    // What a file needs to describe the data it holds. Everything here is
    // known from the record and the connection before recording starts, so a
    // format that cannot represent it is refused up front rather than
    // failing once the user has already captured something.
    struct Info {
        QString name;        // the record's name
        QStringList series;  // per-series names; short or empty means unnamed
        QString unit;        // SenML unit, empty when the record carries none
        QString deviceType;  // the connected board
        QString deviceName;  // its unique id
        QString typecode;    // array typecode of the samples
        double period = 0.0; // sample period in seconds, 0 when unpublished
        double min = 0.0;    // the record's display range, for scaling to PCM
        double max = 0.0;
        int columns = 1;     // series for a waveform, values for a depth frame
        bool gap = true;     // a waveform's timed chunks, vs a depth frame
    };

    // The formats that can actually describe this record, in enum order. The
    // chooser is built from these, so a format is never offered that would
    // then have to be refused.
    static QList<Format> supportedFormats(const Info &info);

    // The save dialog's filter string, the display names for a chooser that
    // is not a file dialog, and the format a chosen filter or suffix names.
    static QString filterString(const QList<Format> &formats);
    static QStringList formatNames(const QList<Format> &formats);
    static Format formatForFilter(const QString &filter, const QString &path);
    static QString suffixFor(Format format);

    // Why this format cannot hold this record, or empty when it can.
    static QString unsupportedReason(Format format, const Info &info);

    explicit OpenMVChannelRecorder(const QString &path, Format format, const Info &info,
                                   QObject *parent = Q_NULLPTR);
    virtual ~OpenMVChannelRecorder();

    bool ok() const { return m_file.isOpen(); }
    QString errorString() const { return m_error; }

    // Appends one chunk. The poll re-reads an unchanged chunk faster than
    // scripts publish, so duplicates are detected (by timestamp and data) and
    // skipped. Returns false when a write fails; errorString() says why.
    bool append(const QByteArray &data, const QString &typecode,
                int samples, int series, double t, double period);

    QString status() const;

private:

    bool openPart();
    bool rollPart();
    bool finishPart();
    bool writeHeader();
    bool writeBlock(const QByteArray &block);
    QString partPath(int part) const;

    QByteArray csvRows(const QByteArray &data, char code, int rows, int actual, double period);

    QString m_basePath;
    Format m_format;
    Info m_info;
    QFile m_file;
    QString m_error;

    // None of these formats can be finalized until the row count is known.
    // NPY patches the shape in its header (padded so the number can be
    // rewritten in place) and WAV its two chunk sizes, both on close. The
    // Edge Impulse formats have to hold their samples until the values array
    // is complete, so those accumulate flat and are encoded at the end.
    qint64 m_headerBytes = 0;
    qint64 m_countOffset = 0;
    QVector<double> m_values;

    bool m_hasLast = false;
    double m_lastT = 0.0;
    QByteArray m_lastData;
    qint64 m_rowCounter = 0; // rows in this part, for a constant-interval clock

    int m_part = 0;
    qint64 m_partRows = 0;
    qint64 m_totalRows = 0;
    qint64 m_totalBytes = 0;
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVCHANNELRECORDER_H
