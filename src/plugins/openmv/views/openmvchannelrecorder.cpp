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

#include "openmvchannelrecorder.h"

#include "../openmvtr.h"

#include <QtCore/QtEndian>

namespace OpenMV {
namespace Internal {

enum : qint64 {
    RECORD_MAX_PART_ROWS  = 1000000,
    RECORD_MAX_PART_BYTES = Q_INT64_C(512) * 1024 * 1024,
};

// The samples-per-value width of an array typecode, and whether its values
// are whole numbers (which the text formats then write without a decimal
// point, so a uint16 reads as 32768 rather than 32768.000000).
static int typecodeStride(char code)
{
    return ((code == 'b') || (code == 'B')) ? 1
         : ((code == 'h') || (code == 'H')) ? 2 : 4;
}

static bool typecodeIsIntegral(char code)
{
    return (code != 'f') && (code != 'd');
}

static char typecodeOf(const QString &typecode)
{
    return typecode.isEmpty() ? 'H' : typecode.at(0).toLatin1();
}

// One sample, read out of the raw chunk exactly as the camera packed it.
static double sampleAt(const uchar *p, char code, qsizetype index)
{
    const uchar *s = p + (index * typecodeStride(code));

    switch(code)
    {
        case 'b': return double(qint8(*s));
        case 'B': return double(quint8(*s));
        case 'h': return double(qFromLittleEndian<qint16>(s));
        case 'i': return double(qFromLittleEndian<qint32>(s));
        case 'I': return double(qFromLittleEndian<quint32>(s));
        case 'f':
        {
            quint32 bits = qFromLittleEndian<quint32>(s);
            float value;
            memcpy(&value, &bits, sizeof(value));
            return double(value);
        }
        case 'H':
        default: return double(qFromLittleEndian<quint16>(s));
    }
}

// A sample's time in milliseconds. Whole milliseconds print as integers,
// but a rate above 1 kHz puts samples inside one -- a 16 kHz sample is
// 0.0625 ms -- so anything finer keeps its decimals rather than rounding
// every sixteenth sample onto the same stamp. Fixed notation throughout: an
// exponent in the first column trips up spreadsheet importers.
static QByteArray timestampText(double milliseconds)
{
    QByteArray text = QByteArray::number(milliseconds, 'f', 6);

    if(text.contains('.'))
    {
        while(text.endsWith('0'))
        {
            text.chop(1);
        }

        if(text.endsWith('.'))
        {
            text.chop(1);
        }
    }

    return text;
}

static QByteArray numberText(double value, bool integral)
{
    return integral ? QByteArray::number(qint64(value))
                    : QByteArray::number(value, 'g', 9);
}

static QString recordBytesString(qint64 bytes)
{
    if(bytes < 1024)
    {
        return Tr::tr("%L1 B").arg(bytes);
    }

    if(bytes < (1024 * 1024))
    {
        return Tr::tr("%L1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }

    return Tr::tr("%L1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

// The count is right aligned in the space reserved for it, which keeps the
// header length -- and so every sample's offset -- unchanged.
static QByteArray npyCountText(qint64 rows)
{
    QByteArray text = QByteArray::number(rows);
    return QByteArray(qMax(0, 12 - int(text.size())), ' ') + text;
}

// Column names for a record's axes: what the sender called them, else the
// record's name, numbered when there is more than one.
static QStringList axisNames(const OpenMVChannelRecorder::Info &info)
{
    QStringList names;
    QString base = info.name.isEmpty() ? QStringLiteral("value") : info.name;

    for(int c = 0; c < qMax(1, info.columns); c++)
    {
        if(c < info.series.size() && (!info.series.at(c).isEmpty()))
        {
            names.append(info.series.at(c));
        }
        else
        {
            names.append((info.columns > 1)
                ? QStringLiteral("%1_%2").arg(base).arg(c) : base);
        }
    }

    return names;
}

///////////////////////////////////////////////////////////////////////////////

// A camera built with single-precision floats cannot hold 1/100, so a record
// declaring 100 Hz publishes a period of 0.00999999977, and every timestamp
// derived from it drifts: 30ms writes as 29.999999 and the interval Edge
// Impulse infers comes out as 9.999999776. Snap the rate back to the whole
// number of samples per second it is a hair away from being.
static double snapPeriod(double period)
{
    if(period <= 0.0)
    {
        return period;
    }

    double rate = 1.0 / period;
    double whole = qRound(rate);

    return ((whole > 0.0) && (qAbs(rate - whole) < (whole * 1e-5))) ? (1.0 / whole) : period;
}

// Display name and file dialog filter for one format.
static QString formatLabel(OpenMVChannelRecorder::Format format)
{
    switch(format)
    {
        case OpenMVChannelRecorder::Wav: return Tr::tr("WAV Audio");
        case OpenMVChannelRecorder::JsonEdgeImpulse: return Tr::tr("Edge Impulse JSON");
        case OpenMVChannelRecorder::CborEdgeImpulse: return Tr::tr("Edge Impulse CBOR");
        case OpenMVChannelRecorder::Npy: return Tr::tr("NumPy Array");
        default: return Tr::tr("CSV");
    }
}

QList<OpenMVChannelRecorder::Format> OpenMVChannelRecorder::supportedFormats(const Info &info)
{
    QList<Format> formats;

    for(Format format : {Csv, Wav, JsonEdgeImpulse, CborEdgeImpulse, Npy})
    {
        if(unsupportedReason(format, info).isEmpty())
        {
            formats.append(format);
        }
    }

    return formats;
}

QStringList OpenMVChannelRecorder::formatNames(const QList<Format> &formats)
{
    QStringList names;

    for(Format format : formats)
    {
        names.append(formatLabel(format));
    }

    return names;
}

QString OpenMVChannelRecorder::filterString(const QList<Format> &formats)
{
    QStringList filters;

    for(Format format : formats)
    {
        filters.append(QStringLiteral("%1 (*.%2)").arg(formatLabel(format), suffixFor(format)));
    }

    return filters.join(QStringLiteral(";;"));
}

OpenMVChannelRecorder::Format OpenMVChannelRecorder::formatForFilter(const QString &filter,
                                                                    const QString &path)
{
    if(filter.startsWith(formatLabel(Wav)))
    {
        return Wav;
    }

    if(filter.startsWith(formatLabel(JsonEdgeImpulse)))
    {
        return JsonEdgeImpulse;
    }

    if(filter.startsWith(formatLabel(CborEdgeImpulse)))
    {
        return CborEdgeImpulse;
    }

    if(filter.startsWith(formatLabel(Npy)))
    {
        return Npy;
    }

    // Typing a name with a suffix and no filter still has to land somewhere
    // sensible.
    QString suffix = QFileInfo(path).suffix().toLower();

    if(suffix == QStringLiteral("wav"))
    {
        return Wav;
    }

    if(suffix == QStringLiteral("json"))
    {
        return JsonEdgeImpulse;
    }

    if(suffix == QStringLiteral("cbor"))
    {
        return CborEdgeImpulse;
    }

    if(suffix == QStringLiteral("npy"))
    {
        return Npy;
    }

    return Csv;
}

QString OpenMVChannelRecorder::suffixFor(Format format)
{
    switch(format)
    {
        case Wav: return QStringLiteral("wav");
        case JsonEdgeImpulse: return QStringLiteral("json");
        case CborEdgeImpulse: return QStringLiteral("cbor");
        case Npy: return QStringLiteral("npy");
        default: return QStringLiteral("csv");
    }
}

QString OpenMVChannelRecorder::unsupportedReason(Format format, const Info &info)
{
    // Every format except the NumPy array states its own sample rate, and
    // there is nothing honest to put there if the script never published one.
    if((format != Npy) && (info.period <= 0.0))
    {
        return Tr::tr("This format needs a sample rate, and \"%1\" does not publish one. "
                      "Pass sample_rate when adding the record, or save a NumPy array.")
            .arg(info.name);
    }

    if((format == Wav) && (!info.gap))
    {
        return Tr::tr("A depth map is not audio; record it to a CSV, JSON or NumPy file.");
    }

    if((format == Wav) && (info.max <= info.min))
    {
        return Tr::tr("WAV needs the record's display range to scale samples, "
                      "and \"%1\" does not publish min and max.").arg(info.name);
    }

    return QString();
}

///////////////////////////////////////////////////////////////////////////////

OpenMVChannelRecorder::OpenMVChannelRecorder(const QString &path, Format format,
                                             const Info &info, QObject *parent) : QObject(parent),
    m_basePath(path),
    m_format(format),
    m_info(info)
{
    m_info.columns = qMax(1, m_info.columns);
    m_info.period = snapPeriod(m_info.period);
    openPart();
}

OpenMVChannelRecorder::~OpenMVChannelRecorder()
{
    finishPart();
    m_file.close();
}

QString OpenMVChannelRecorder::partPath(int part) const
{
    if(!part)
    {
        return m_basePath;
    }

    QFileInfo info(m_basePath);
    QString suffix = info.suffix();
    QString stem = info.completeBaseName() + QStringLiteral("_%1").arg(part, 3, 10, QLatin1Char('0'));
    return info.dir().filePath(suffix.isEmpty() ? stem : (stem + QLatin1Char('.') + suffix));
}

bool OpenMVChannelRecorder::openPart()
{
    m_file.setFileName(partPath(m_part));

    if(!m_file.open(QIODevice::ReadWrite | QIODevice::Truncate))
    {
        m_error = Tr::tr("Cannot open \"%1\" - %2").arg(m_file.fileName(), m_file.errorString());
        return false;
    }

    m_partRows = 0;
    m_rowCounter = 0;
    m_values.clear();
    return writeHeader();
}

bool OpenMVChannelRecorder::writeBlock(const QByteArray &block)
{
    if(m_file.write(block) != block.size())
    {
        m_error = Tr::tr("Cannot write \"%1\" - %2").arg(m_file.fileName(), m_file.errorString());
        m_file.close();
        return false;
    }

    m_totalBytes += block.size();
    return true;
}

bool OpenMVChannelRecorder::writeHeader()
{
    QStringList names = axisNames(m_info);
    QByteArray header;

    switch(m_format)
    {
        case Csv:
        {
            // Edge Impulse infers the sampling frequency from the interval
            // between rows, and requires the first column to be called
            // "timestamp" and to hold milliseconds.
            header = QByteArrayLiteral("timestamp");

            for(const QString &name : names)
            {
                // CSV metacharacters in a name would break the layout.
                QString safe = name;
                safe.replace(QLatin1Char(','), QLatin1Char('_'));
                safe.replace(QLatin1Char('"'), QLatin1Char('_'));
                header += ',';
                header += safe.toUtf8();
            }

            header += '\n';
            break;
        }

        case Wav:
        {
            // Sizes are unknown until the recording stops, so the two length
            // fields are written as zero and patched in finishPart().
            int channels = m_info.columns;
            int rate = qRound(1.0 / m_info.period);
            int blockAlign = channels * 2;

            header.resize(44);
            char *h = header.data();
            memcpy(h, "RIFF", 4);
            qToLittleEndian<quint32>(0, h + 4);
            memcpy(h + 8, "WAVEfmt ", 8);
            qToLittleEndian<quint32>(16, h + 16);
            qToLittleEndian<quint16>(1, h + 20); // PCM
            qToLittleEndian<quint16>(quint16(channels), h + 22);
            qToLittleEndian<quint32>(quint32(rate), h + 24);
            qToLittleEndian<quint32>(quint32(rate * blockAlign), h + 28);
            qToLittleEndian<quint16>(quint16(blockAlign), h + 32);
            qToLittleEndian<quint16>(16, h + 34); // bits per sample
            memcpy(h + 36, "data", 4);
            qToLittleEndian<quint32>(0, h + 40);
            break;
        }

        case Npy:
        {
            // The samples are already little endian and interleaved exactly
            // as NumPy wants them, so the chunks are written through
            // untouched and only the row count is patched in at the end. It
            // is padded here so it can be rewritten in place.
            char code = typecodeOf(m_info.typecode);
            const char *dtype = (code == 'b') ? "|i1" : (code == 'B') ? "|u1"
                              : (code == 'h') ? "<i2" : (code == 'H') ? "<u2"
                              : (code == 'i') ? "<i4" : (code == 'I') ? "<u4" : "<f4";

            QByteArray dict = "{'descr': '" + QByteArray(dtype)
                + "', 'fortran_order': False, 'shape': (";
            m_countOffset = 10 + dict.size();
            // A count of the same width as the one written on close, so the
            // header stays valid even if the file is read before then.
            dict += npyCountText(0);
            dict += ", " + QByteArray::number(m_info.columns) + "), }";

            // The whole header, magic included, pads to a 64 byte boundary.
            int total = 10 + dict.size() + 1;
            dict += QByteArray(((total % 64) ? (64 - (total % 64)) : 0), ' ');
            dict += '\n';

            header = QByteArrayLiteral("\x93NUMPY\x01\x00");
            header.resize(8);
            QByteArray length(2, '\0');
            qToLittleEndian<quint16>(quint16(dict.size()), length.data());
            header += length;
            header += dict;
            break;
        }

        default: break; // the Edge Impulse formats are written whole at the end
    }

    m_headerBytes = header.size();
    return header.isEmpty() ? true : writeBlock(header);
}

bool OpenMVChannelRecorder::finishPart()
{
    if(!m_file.isOpen())
    {
        return false;
    }

    switch(m_format)
    {
        case Wav:
        {
            qint64 dataBytes = m_file.size() - m_headerBytes;
            QByteArray field(4, '\0');

            qToLittleEndian<quint32>(quint32(dataBytes + 36), field.data());
            m_file.seek(4);
            m_file.write(field);

            qToLittleEndian<quint32>(quint32(dataBytes), field.data());
            m_file.seek(40);
            m_file.write(field);
            break;
        }

        case Npy:
        {
            m_file.seek(m_countOffset);
            m_file.write(npyCountText(m_partRows));
            break;
        }

        case JsonEdgeImpulse:
        case CborEdgeImpulse:
        {
            QStringList names = axisNames(m_info);
            QCborArray sensors;

            for(const QString &name : names)
            {
                QCborMap sensor;
                sensor[QStringLiteral("name")] = name;
                // Edge Impulse takes SenML units, which is what the channel
                // protocol already carries, so it passes straight through.
                sensor[QStringLiteral("units")] = m_info.unit;
                sensors.append(sensor);
            }

            QCborArray values;
            bool integral = typecodeIsIntegral(typecodeOf(m_info.typecode));

            for(qint64 r = 0; r < m_partRows; r++)
            {
                QCborArray row;

                for(int c = 0; c < m_info.columns; c++)
                {
                    double value = m_values.at((r * m_info.columns) + c);
                    row.append(integral ? QCborValue(qint64(value)) : QCborValue(value));
                }

                values.append(row);
            }

            QCborMap payload;

            if(!m_info.deviceName.isEmpty())
            {
                payload[QStringLiteral("device_name")] = m_info.deviceName;
            }

            payload[QStringLiteral("device_type")] = m_info.deviceType.isEmpty()
                ? QStringLiteral("OpenMV Cam") : m_info.deviceType;
            payload[QStringLiteral("interval_ms")] = m_info.period * 1000.0;
            payload[QStringLiteral("sensors")] = sensors;
            payload[QStringLiteral("values")] = values;

            QCborMap protectedHeader;
            protectedHeader[QStringLiteral("ver")] = QStringLiteral("v1");
            protectedHeader[QStringLiteral("alg")] = QStringLiteral("none");
            protectedHeader[QStringLiteral("iat")] = QDateTime::currentSecsSinceEpoch();

            QCborMap root;
            root[QStringLiteral("protected")] = protectedHeader;
            // Unsigned data still carries the field, as an empty signature.
            root[QStringLiteral("signature")] = QString(64, QLatin1Char('0'));
            root[QStringLiteral("payload")] = payload;

            QByteArray encoded = (m_format == CborEdgeImpulse)
                ? QCborValue(root).toCbor()
                : QJsonDocument(QCborValue(root).toJsonValue().toObject()).toJson(QJsonDocument::Compact);

            m_file.seek(0);
            writeBlock(encoded);
            m_file.resize(encoded.size());
            break;
        }

        default: break; // the CSV is complete as it stands
    }

    m_file.flush();
    return true;
}

bool OpenMVChannelRecorder::rollPart()
{
    finishPart();
    m_file.close();
    m_part += 1;
    return openPart();
}

QByteArray OpenMVChannelRecorder::csvRows(const QByteArray &data, char code,
                                          int rows, int actual, double period)
{
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
    bool integral = typecodeIsIntegral(code);
    int written = qMin(actual, m_info.columns);

    QByteArray block;
    block.reserve(rows * ((m_info.columns + 2) * 14));

    for(int r = 0; r < rows; r++)
    {
        // Edge Impulse requires a constant interval, so the clock counts rows
        // rather than following device timestamps -- which drift by however
        // far the publisher's cadence misses its declared rate.
        block += timestampText((m_rowCounter + r) * period * 1000.0);

        for(int c = 0; c < m_info.columns; c++)
        {
            block += ',';

            // A record whose series count grew mid-recording has the extra
            // series dropped; one that shrank leaves its columns empty.
            if(c < written)
            {
                block += numberText(sampleAt(p, code, (qsizetype(r) * actual) + c), integral);
            }
        }

        block += '\n';
    }

    return block;
}

bool OpenMVChannelRecorder::append(const QByteArray &data, const QString &typecode,
                                   int samples, int series, double t, double period)
{
    if(!m_file.isOpen())
    {
        return false;
    }

    if((samples <= 0) || data.isEmpty())
    {
        return true; // the script hasn't published a chunk yet
    }

    // The poll runs faster than scripts publish: the same chunk comes back
    // repeatedly. With a timestamp, a chunk is new when t changes (or the
    // data does - a script may republish within one t quantum); without one,
    // only when the data changes.
    bool hasT = !qIsNaN(t);

    if(m_hasLast && (data == m_lastData) && ((!hasT) || (t == m_lastT)))
    {
        return true;
    }

    m_hasLast = true;
    m_lastT = hasT ? t : 0.0;
    m_lastData = data;

    char code = typecodeOf(typecode);
    int stride = typecodeStride(code);
    int actual = qMax(1, series);
    int count = qMin(samples * actual, int(data.size()) / stride);
    int rows = count / actual;

    if(!rows)
    {
        return true;
    }

    switch(m_format)
    {
        case Csv:
        {
            if(!writeBlock(csvRows(data, code, rows, actual, m_info.period)))
            {
                return false;
            }

            break;
        }

        case Wav:
        {
            // 16 bit PCM, with the record's own display range mapped onto it
            // so a float series and a uint16 one come out at the same level.
            const uchar *p = reinterpret_cast<const uchar *>(data.constData());
            double middle = (m_info.max + m_info.min) * 0.5;
            double scale = 65534.0 / (m_info.max - m_info.min);
            int written = qMin(actual, m_info.columns);

            QByteArray block(rows * m_info.columns * 2, '\0');
            char *out = block.data();

            for(int r = 0; r < rows; r++)
            {
                for(int c = 0; c < m_info.columns; c++)
                {
                    double value = (c < written)
                        ? ((sampleAt(p, code, (qsizetype(r) * actual) + c) - middle) * scale) : 0.0;
                    qToLittleEndian<qint16>(qint16(qBound(-32768.0, value, 32767.0)),
                                            out + (((qsizetype(r) * m_info.columns) + c) * 2));
                }
            }

            if(!writeBlock(block))
            {
                return false;
            }

            break;
        }

        case Npy:
        {
            // Straight through: already little endian, already interleaved.
            if(actual == m_info.columns)
            {
                if(!writeBlock(data.left(qsizetype(rows) * actual * stride)))
                {
                    return false;
                }
            }
            else
            {
                // A changed series count would misalign every later row, so
                // the rows are rebuilt against the fixed column count.
                QByteArray block(qsizetype(rows) * m_info.columns * stride, '\0');
                int written = qMin(actual, m_info.columns);

                for(int r = 0; r < rows; r++)
                {
                    memcpy(block.data() + (qsizetype(r) * m_info.columns * stride),
                           data.constData() + (qsizetype(r) * actual * stride),
                           qsizetype(written) * stride);
                }

                if(!writeBlock(block))
                {
                    return false;
                }
            }

            break;
        }

        case JsonEdgeImpulse:
        case CborEdgeImpulse:
        {
            // Held until the values array is complete; the file itself is
            // written once, when the recording stops or the part rolls.
            const uchar *p = reinterpret_cast<const uchar *>(data.constData());
            int written = qMin(actual, m_info.columns);

            m_values.reserve(m_values.size() + (qsizetype(rows) * m_info.columns));

            for(int r = 0; r < rows; r++)
            {
                for(int c = 0; c < m_info.columns; c++)
                {
                    m_values.append((c < written)
                        ? sampleAt(p, code, (qsizetype(r) * actual) + c) : 0.0);
                }
            }

            break;
        }
    }

    // Push every chunk to disk so an IDE crash can't lose recorded data. The
    // buffered formats have nothing on disk to flush yet.
    m_file.flush();

    m_rowCounter += rows;
    m_partRows += rows;
    m_totalRows += rows;

    // Split before a part grows unwieldy (checked per chunk, so a part can
    // overshoot by at most one chunk's rows).
    if(((m_partRows >= RECORD_MAX_PART_ROWS) || (m_file.size() >= RECORD_MAX_PART_BYTES))
    && (!rollPart()))
    {
        return false;
    }

    return true;
}

QString OpenMVChannelRecorder::status() const
{
    // A format still holding its samples in memory has nothing on disk to
    // measure, so its size is estimated from what it is holding.
    qint64 bytes = m_totalBytes + (qint64(m_values.size()) * 8);

    return m_part
        ? Tr::tr("%L1 samples (%L2) - part %L3").arg(m_totalRows).arg(recordBytesString(bytes)).arg(m_part + 1)
        : Tr::tr("%L1 samples (%L2)").arg(m_totalRows).arg(recordBytesString(bytes));
}

} // namespace Internal
} // namespace OpenMV
