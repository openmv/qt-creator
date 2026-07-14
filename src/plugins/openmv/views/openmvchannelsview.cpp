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

#include "openmvchannelsview.h"
#include "openmvviewstyle.h"
#include "../openmvtr.h"

#include <QtCore/QtEndian>
#include <QtSvg/QSvgRenderer>

#include <extensionsystem/pluginmanager.h>
#include <utils/qtcsettings.h>
#include <utils/store.h>
#include <utils/theme/theme.h>

namespace OpenMV {
namespace Internal {

// SenML-compatible CBOR integer keys (matching OpenMV Studio).
enum : int {
    CBOR_KEY_BN = -2,  // base name
    CBOR_KEY_N  = 0,   // name
    CBOR_KEY_U  = 1,   // unit
    CBOR_KEY_V  = 2,   // numeric value
    CBOR_KEY_VS = 3,   // string value
    CBOR_KEY_VB = 4,   // boolean value
    CBOR_KEY_VD = 8,   // data value (binary)
    CBOR_KEY_T  = 6,   // time (SenML): waveform chunk timestamp
    CBOR_KEY_UT = 7,   // update time (SenML): waveform sample period (s)

    // Custom 2D data extension keys
    CBOR_KEY_W   = -20, // width
    CBOR_KEY_H   = -21, // height
    CBOR_KEY_MIN = -23, // min
    CBOR_KEY_MAX = -24, // max

    // Widget keys
    CBOR_KEY_W_TYPE = -30, // widget type
    CBOR_KEY_W_MIN  = -31, // slider min
    CBOR_KEY_W_MAX  = -32, // slider max
    CBOR_KEY_W_STEP = -33, // slider step
    CBOR_KEY_W_OPTS = -34, // select options
};

#define CHANNEL_FLAG_WRITE (1 << 1)

// Unit -> 14x14 icon covering the SenML unit registry (RFC 8428 plus the
// RFC 8798 secondary units) and OpenMV Studio's legacy names (lux, mg,
// mdps). Units sharing a concept share a glyph; "currentColor" is
// replaced with the muted name-label color at render time.
#define UNIT_ICON_THERMOMETER "<svg viewBox=\"0 0 14 14\"><path d=\"M5 1.5a1.5 1.5 0 1 1 3 0 1.5 1.5 0 0 1-3 0zM9 5v5a2.5 2.5 0 1 1-5 0V5a3 3 0 1 1 6 0zm-2 0a1 1 0 1 0-2 0v5a.5.5 0 0 0 1 0V5z\" fill=\"currentColor\"/></svg>"
#define UNIT_ICON_DROPLET "<svg viewBox=\"0 0 14 14\"><path d=\"M7 1S3 5.5 3 8.5a4 4 0 0 0 8 0C11 5.5 7 1 7 1zm0 10a2.5 2.5 0 0 1-2.5-2.5c0-.3.05-.6.15-.9l2.85-3.2 2.85 3.2c.1.3.15.6.15.9A2.5 2.5 0 0 1 7 11z\" fill=\"currentColor\"/></svg>"
#define UNIT_ICON_SUN "<svg viewBox=\"0 0 14 14\"><circle cx=\"7\" cy=\"7\" r=\"3\" fill=\"currentColor\"/><g stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linecap=\"round\"><line x1=\"7\" y1=\"1\" x2=\"7\" y2=\"2.5\"/><line x1=\"7\" y1=\"11.5\" x2=\"7\" y2=\"13\"/><line x1=\"1\" y1=\"7\" x2=\"2.5\" y2=\"7\"/><line x1=\"11.5\" y1=\"7\" x2=\"13\" y2=\"7\"/><line x1=\"2.8\" y1=\"2.8\" x2=\"3.8\" y2=\"3.8\"/><line x1=\"10.2\" y1=\"10.2\" x2=\"11.2\" y2=\"11.2\"/><line x1=\"2.8\" y1=\"11.2\" x2=\"3.8\" y2=\"10.2\"/><line x1=\"10.2\" y1=\"3.8\" x2=\"11.2\" y2=\"2.8\"/></g></svg>"
#define UNIT_ICON_PRESSURE "<svg viewBox=\"0 0 14 14\"><path d=\"M3 12V6l2-4 2 4v6m-1-3H4m6 3V4l2-2v10\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></svg>"
#define UNIT_ICON_BOLT "<svg viewBox=\"0 0 14 14\"><path d=\"M8 1L3 8h4l-1 5 5-7H7l1-5z\" fill=\"currentColor\"/></svg>"
#define UNIT_ICON_AMP "<svg viewBox=\"0 0 14 14\"><path d=\"M7 2l-4 10h2l1-3h2l1 3h2L7 2zm0 3.5L8.2 8H5.8L7 5.5z\" fill=\"currentColor\"/></svg>"
#define UNIT_ICON_ANGLE "<svg viewBox=\"0 0 14 14\"><circle cx=\"7\" cy=\"7\" r=\"4\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/><line x1=\"7\" y1=\"7\" x2=\"7\" y2=\"3.5\" stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linecap=\"round\"/><circle cx=\"7\" cy=\"2\" r=\"1\" fill=\"currentColor\"/></svg>"
#define UNIT_ICON_WIND "<svg viewBox=\"0 0 14 14\"><path d=\"M1 10c1.5-1 3-4 5-4s2.5 3 4 3 2.5-2 3-3\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linecap=\"round\"/><path d=\"M10 4l2 1-1 2\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></svg>"
#define UNIT_ICON_AXES "<svg viewBox=\"0 0 14 14\"><path d=\"M2 7h10M7 2v10M4 4l6 6M10 4l-6 6\" stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linecap=\"round\"/></svg>"
#define UNIT_ICON_GYRO "<svg viewBox=\"0 0 14 14\"><circle cx=\"7\" cy=\"7\" r=\"5\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/><path d=\"M7 7l3-2\" stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linecap=\"round\"/><path d=\"M9 3l1.5.5-.5 1.5\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></svg>"
#define UNIT_ICON_MAGNET "<svg viewBox=\"0 0 14 14\"><path d=\"M4 12V6a3 3 0 0 1 6 0v6\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.6\"/><path d=\"M3 10h2.6M8.4 10H11\" stroke=\"currentColor\" stroke-width=\"1.2\"/></svg>"
#define UNIT_ICON_RULER "<svg viewBox=\"0 0 14 14\"><rect x=\"1\" y=\"5\" width=\"12\" height=\"4\" rx=\"1\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/><path d=\"M4 5v2M7 5v2M10 5v2\" stroke=\"currentColor\" stroke-width=\"1\"/></svg>"
#define UNIT_ICON_CLOCK "<svg viewBox=\"0 0 14 14\"><circle cx=\"7\" cy=\"7\" r=\"5\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/><path d=\"M7 4v3l2 2\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linecap=\"round\"/></svg>"
#define UNIT_ICON_WAVE "<svg viewBox=\"0 0 14 14\"><path d=\"M1 7c1-4 2-4 3 0s2 4 3 0 2-4 3 0 2 4 3 0\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linecap=\"round\"/></svg>"
#define UNIT_ICON_CHIP "<svg viewBox=\"0 0 14 14\"><rect x=\"4\" y=\"4\" width=\"6\" height=\"6\" rx=\"1\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/><path d=\"M5.5 4V2M8.5 4V2M5.5 12v-2M8.5 12v-2M4 5.5H2M4 8.5H2M12 5.5h-2M12 8.5h-2\" stroke=\"currentColor\" stroke-width=\"1\"/></svg>"
#define UNIT_ICON_BATTERY "<svg viewBox=\"0 0 14 14\"><rect x=\"1\" y=\"4\" width=\"10\" height=\"6\" rx=\"1\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/><rect x=\"3\" y=\"6\" width=\"4\" height=\"2\" fill=\"currentColor\"/><path d=\"M12.5 6v2\" stroke=\"currentColor\" stroke-width=\"1.5\" stroke-linecap=\"round\"/></svg>"
#define UNIT_ICON_ENERGY "<svg viewBox=\"0 0 14 14\"><rect x=\"1\" y=\"4\" width=\"10\" height=\"6\" rx=\"1\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/><path d=\"M12.5 6v2\" stroke=\"currentColor\" stroke-width=\"1.5\" stroke-linecap=\"round\"/><path d=\"M6.6 4.6L4.6 7.2h1.5l-1 2.2 2.3-2.6H5.9l.7-2.2z\" fill=\"currentColor\"/></svg>"
#define UNIT_ICON_POWER "<svg viewBox=\"0 0 14 14\"><circle cx=\"7\" cy=\"7\" r=\"5.5\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.1\"/><path d=\"M7.8 3.5L5.5 7.4h1.9L6.2 10.5l2.4-3.6H6.8l1-3.4z\" fill=\"currentColor\"/></svg>"
#define UNIT_ICON_SPEAKER "<svg viewBox=\"0 0 14 14\"><path d=\"M2 5.5v3h1.8L7 11V3L3.8 5.5H2z\" fill=\"currentColor\"/><path d=\"M9 5.5a2 2 0 0 1 0 3M10.8 4a4.3 4.3 0 0 1 0 6\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.1\" stroke-linecap=\"round\"/></svg>"
#define UNIT_ICON_HASH "<svg viewBox=\"0 0 14 14\"><path d=\"M5 2L4 12M10 2L9 12M2.5 5h9M2.5 9h9\" stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linecap=\"round\"/></svg>"
#define UNIT_ICON_HEART "<svg viewBox=\"0 0 14 14\"><path d=\"M7 12S2 8.6 2 5.6a2.6 2.6 0 0 1 5-1 2.6 2.6 0 0 1 5 1C12 8.6 7 12 7 12z\" fill=\"currentColor\"/></svg>"
#define UNIT_ICON_PERCENT "<svg viewBox=\"0 0 14 14\"><path d=\"M3 11l8-8\" stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linecap=\"round\"/><circle cx=\"4.2\" cy=\"4.2\" r=\"1.7\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.1\"/><circle cx=\"9.8\" cy=\"9.8\" r=\"1.7\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.1\"/></svg>"
#define UNIT_ICON_SCALE "<svg viewBox=\"0 0 14 14\"><path d=\"M5 4.5a2 2 0 1 1 4 0\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/><path d=\"M4 4.5h6l1.2 6.2a1 1 0 0 1-1 1.3H3.8a1 1 0 0 1-1-1.3z\" fill=\"currentColor\"/></svg>"
#define UNIT_ICON_CLOUD "<svg viewBox=\"0 0 14 14\"><path d=\"M4.2 11a2.6 2.6 0 0 1-.2-5.2 3.4 3.4 0 0 1 6.6-.5A2.4 2.4 0 0 1 10.4 11z\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linejoin=\"round\"/></svg>"
#define UNIT_ICON_OMEGA "<svg viewBox=\"0 0 14 14\"><path d=\"M3.5 11.5H6v-1.3a3.6 3.6 0 1 1 2 0v1.3h2.5\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></svg>"
#define UNIT_ICON_CAPACITOR "<svg viewBox=\"0 0 14 14\"><path d=\"M1 7h4M9 7h4M5 3.5v7M9 3.5v7\" stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linecap=\"round\"/></svg>"
#define UNIT_ICON_COIL "<svg viewBox=\"0 0 14 14\"><path d=\"M1 8.5a1.5 1.5 0 0 1 3 0 1.5 1.5 0 0 1 3 0 1.5 1.5 0 0 1 3 0 1.5 1.5 0 0 1 3 0\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/></svg>"
#define UNIT_ICON_RADIATION "<svg viewBox=\"0 0 14 14\"><circle cx=\"7\" cy=\"7\" r=\"1.3\" fill=\"currentColor\"/><path d=\"M7 6L5 2.6a6 6 0 0 1 4 0zM8.1 7.6l3.4 2a6 6 0 0 1-2 3.4zM5.9 7.6l-3.4 2a6 6 0 0 0 2 3.4z\" fill=\"currentColor\"/></svg>"
#define UNIT_ICON_FLASK "<svg viewBox=\"0 0 14 14\"><path d=\"M5.5 1.5h3M6 1.5v4L3.2 11a1.4 1.4 0 0 0 1.3 2h5a1.4 1.4 0 0 0 1.3-2L8 5.5v-4\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linejoin=\"round\"/></svg>"
#define UNIT_ICON_AREA "<svg viewBox=\"0 0 14 14\"><rect x=\"2.5\" y=\"2.5\" width=\"9\" height=\"9\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/></svg>"
#define UNIT_ICON_VOLUME "<svg viewBox=\"0 0 14 14\"><path d=\"M7 1.5l5 2.5v6l-5 2.5-5-2.5V4z\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linejoin=\"round\"/><path d=\"M2 4l5 2.5L12 4M7 6.5v6\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1\"/></svg>"
#define UNIT_ICON_FLOW "<svg viewBox=\"0 0 14 14\"><path d=\"M4.5 1.5S2 4.5 2 6.5a2.5 2.5 0 0 0 5 0c0-2-2.5-5-2.5-5z\" fill=\"currentColor\"/><path d=\"M8.5 10.5H13M11.2 8.7l1.8 1.8-1.8 1.8\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></svg>"
#define UNIT_ICON_GLOBE "<svg viewBox=\"0 0 14 14\"><circle cx=\"7\" cy=\"7\" r=\"5.2\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.1\"/><ellipse cx=\"7\" cy=\"7\" rx=\"2.3\" ry=\"5.2\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1\"/><path d=\"M1.8 7h10.4\" stroke=\"currentColor\" stroke-width=\"1\"/></svg>"
#define UNIT_ICON_FORCE "<svg viewBox=\"0 0 14 14\"><rect x=\"8\" y=\"4.5\" width=\"4.5\" height=\"5\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/><path d=\"M1 7h5.5M4.8 5.2L6.6 7l-1.8 1.8\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></svg>"
static const struct { const char *unit; const char *svg; } UNIT_ICONS[] = {
    // Temperature
    { "Cel", UNIT_ICON_THERMOMETER },
    { "K", UNIT_ICON_THERMOMETER },
    // Humidity
    { "%RH", UNIT_ICON_DROPLET },
    // Light
    { "lx", UNIT_ICON_SUN },
    { "lux", UNIT_ICON_SUN },
    { "lm", UNIT_ICON_SUN },
    { "cd", UNIT_ICON_SUN },
    { "cd/m2", UNIT_ICON_SUN },
    { "W/m2", UNIT_ICON_SUN },
    // Pressure
    { "Pa", UNIT_ICON_PRESSURE },
    { "hPa", UNIT_ICON_PRESSURE },
    // Voltage
    { "V", UNIT_ICON_BOLT },
    { "mV", UNIT_ICON_BOLT },
    // Current
    { "A", UNIT_ICON_AMP },
    { "mA", UNIT_ICON_AMP },
    // Angle
    { "deg", UNIT_ICON_ANGLE },
    { "rad", UNIT_ICON_ANGLE },
    { "sr", UNIT_ICON_ANGLE },
    // Speed
    { "m/s", UNIT_ICON_WIND },
    { "km/h", UNIT_ICON_WIND },
    { "m/h", UNIT_ICON_WIND },
    // Acceleration
    { "mg", UNIT_ICON_AXES },
    { "m/s2", UNIT_ICON_AXES },
    // Angular rate
    { "mdps", UNIT_ICON_GYRO },
    { "dps", UNIT_ICON_GYRO },
    { "rad/s", UNIT_ICON_GYRO },
    // Magnetic
    { "uT", UNIT_ICON_MAGNET },
    { "T", UNIT_ICON_MAGNET },
    { "gauss", UNIT_ICON_MAGNET },
    { "Wb", UNIT_ICON_MAGNET },
    // Length
    { "m", UNIT_ICON_RULER },
    { "cm", UNIT_ICON_RULER },
    { "mm", UNIT_ICON_RULER },
    { "km", UNIT_ICON_RULER },
    // Time
    { "s", UNIT_ICON_CLOCK },
    { "ms", UNIT_ICON_CLOCK },
    { "min", UNIT_ICON_CLOCK },
    { "h", UNIT_ICON_CLOCK },
    // Frequency
    { "Hz", UNIT_ICON_WAVE },
    { "kHz", UNIT_ICON_WAVE },
    { "MHz", UNIT_ICON_WAVE },
    { "1/s", UNIT_ICON_WAVE },
    { "1/min", UNIT_ICON_WAVE },
    // Data
    { "bit", UNIT_ICON_CHIP },
    { "B", UNIT_ICON_CHIP },
    { "KB", UNIT_ICON_CHIP },
    { "KiB", UNIT_ICON_CHIP },
    { "MB", UNIT_ICON_CHIP },
    { "GB", UNIT_ICON_CHIP },
    { "bit/s", UNIT_ICON_CHIP },
    { "B/s", UNIT_ICON_CHIP },
    { "MB/s", UNIT_ICON_CHIP },
    { "Mbit/s", UNIT_ICON_CHIP },
    // Battery
    { "%EL", UNIT_ICON_BATTERY },
    { "EL", UNIT_ICON_BATTERY },
    // Energy/Charge
    { "J", UNIT_ICON_ENERGY },
    { "Wh", UNIT_ICON_ENERGY },
    { "kWh", UNIT_ICON_ENERGY },
    { "Ah", UNIT_ICON_ENERGY },
    { "C", UNIT_ICON_ENERGY },
    { "varh", UNIT_ICON_ENERGY },
    { "kvarh", UNIT_ICON_ENERGY },
    { "kVAh", UNIT_ICON_ENERGY },
    { "Wh/km", UNIT_ICON_ENERGY },
    // Power
    { "W", UNIT_ICON_POWER },
    { "kW", UNIT_ICON_POWER },
    { "VA", UNIT_ICON_POWER },
    { "kVA", UNIT_ICON_POWER },
    { "VAs", UNIT_ICON_POWER },
    { "var", UNIT_ICON_POWER },
    { "kvar", UNIT_ICON_POWER },
    { "vars", UNIT_ICON_POWER },
    { "dBW", UNIT_ICON_POWER },
    { "dBm", UNIT_ICON_POWER },
    // Sound
    { "dB", UNIT_ICON_SPEAKER },
    { "Bspl", UNIT_ICON_SPEAKER },
    // Counting
    { "count", UNIT_ICON_HASH },
    // Heart
    { "beat/min", UNIT_ICON_HEART },
    { "beats", UNIT_ICON_HEART },
    // Ratio
    { "%", UNIT_ICON_PERCENT },
    { "/", UNIT_ICON_PERCENT },
    { "/100", UNIT_ICON_PERCENT },
    { "/1000", UNIT_ICON_PERCENT },
    { "ppm", UNIT_ICON_PERCENT },
    // Mass
    { "kg", UNIT_ICON_SCALE },
    { "g", UNIT_ICON_SCALE },
    // Concentration
    { "kg/m3", UNIT_ICON_CLOUD },
    { "ug/m3", UNIT_ICON_CLOUD },
    { "mm/h", UNIT_ICON_CLOUD },
    // Electrical
    { "Ohm", UNIT_ICON_OMEGA },
    { "S", UNIT_ICON_OMEGA },
    { "S/m", UNIT_ICON_OMEGA },
    { "F", UNIT_ICON_CAPACITOR },
    { "H", UNIT_ICON_COIL },
    // Radiation
    { "Bq", UNIT_ICON_RADIATION },
    { "Gy", UNIT_ICON_RADIATION },
    { "Sv", UNIT_ICON_RADIATION },
    // Chemistry
    { "mol", UNIT_ICON_FLASK },
    { "kat", UNIT_ICON_FLASK },
    { "pH", UNIT_ICON_FLASK },
    // Geometry
    { "m2", UNIT_ICON_AREA },
    { "m3", UNIT_ICON_VOLUME },
    { "l", UNIT_ICON_VOLUME },
    // Flow
    { "m3/s", UNIT_ICON_FLOW },
    { "l/s", UNIT_ICON_FLOW },
    // Position
    { "lat", UNIT_ICON_GLOBE },
    { "lon", UNIT_ICON_GLOBE },
    // Force
    { "N", UNIT_ICON_FORCE },
};

// A small icon label for a known unit, tinted like the muted name labels;
// null for units without an icon.
static QLabel *unitIconLabel(const QString &unit)
{
    for(size_t i = 0; i < (sizeof(UNIT_ICONS) / sizeof(UNIT_ICONS[0])); i++)
    {
        if(unit == QLatin1String(UNIT_ICONS[i].unit))
        {
            QLabel *label = new QLabel;

            QByteArray svg(UNIT_ICONS[i].svg);
            svg.replace("currentColor", Utils::creatorTheme()->color(Utils::Theme::TextColorNormal).name().toLatin1());

            qreal dpr = label->devicePixelRatioF();
            QPixmap pixmap(qRound(14 * dpr), qRound(14 * dpr));
            pixmap.setDevicePixelRatio(dpr);
            pixmap.fill(Qt::transparent);

            QSvgRenderer renderer(svg);
            QPainter painter(&pixmap);
            renderer.render(&painter);
            painter.end();

            label->setPixmap(pixmap);
            return label;
        }
    }

    return Q_NULLPTR;
}

// The plain value of a record: numeric, string, or boolean.
static QCborValue recordValue(const QCborMap &rec)
{
    if (rec.contains(qint64(CBOR_KEY_V))) return rec.value(qint64(CBOR_KEY_V));
    if (rec.contains(qint64(CBOR_KEY_VS))) return rec.value(qint64(CBOR_KEY_VS));
    return rec.value(qint64(CBOR_KEY_VB));
}

static QString displayValue(const QCborValue &value)
{
    if (value.isDouble()) return QString::number(value.toDouble(), 'f', 1);
    if (value.isInteger()) return QString::number(value.toInteger());
    if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return value.toString();
}

// A CBOR write payload the script reads back: [ { N: name, V: value } ].
static QByteArray encodeWrite(const QString &recordName, const QCborValue &value)
{
    QCborMap rec;
    rec.insert(qint64(CBOR_KEY_N), recordName);
    rec.insert(qint64(CBOR_KEY_V), value);
    return QCborArray({rec}).toCborValue().toCbor();
}

// Slider positions are integer steps over [min, max] in units of step.
static int sliderSteps(double min, double max, double step)
{
    return qMax(1, qRound((max - min) / ((step > 0.0) ? step : 1.0)));
}

// Decode interleaved samples per the array-module typecode carried in the
// record's string-value slot ("H" uint16 when absent). Little-endian.
static QVector<float> decodeSamples(const QByteArray &data, const QString &typecode, int count)
{
    char code = typecode.isEmpty() ? 'H' : typecode.at(0).toLatin1();
    int stride = ((code == 'b') || (code == 'B')) ? 1
               : ((code == 'h') || (code == 'H')) ? 2 : 4;
    int n = qMin(count, int(data.size()) / stride);

    QVector<float> out;
    out.reserve(n);

    const uchar *p = reinterpret_cast<const uchar *>(data.constData());

    for(int i = 0; i < n; i++, p += stride)
    {
        switch(code)
        {
            case 'b': out.append(float(qint8(*p))); break;
            case 'B': out.append(float(quint8(*p))); break;
            case 'h': out.append(float(qFromLittleEndian<qint16>(p))); break;
            case 'i': out.append(float(qFromLittleEndian<qint32>(p))); break;
            case 'I': out.append(float(qFromLittleEndian<quint32>(p))); break;
            case 'f':
            {
                quint32 bits = qFromLittleEndian<quint32>(p);
                float value;
                memcpy(&value, &bits, sizeof(value));
                out.append(value);
                break;
            }
            case 'H':
            default: out.append(float(qFromLittleEndian<quint16>(p))); break;
        }
    }

    return out;
}

// Human-readable sample rate from the SenML update-time (period) field.
static QString rateString(double period)
{
    if(period <= 0.0)
    {
        return QString();
    }

    double hz = 1.0 / period;
    return (hz >= 1000.0) ? Tr::tr("%L1 kHz").arg(hz / 1000.0, 0, 'f', 1)
                          : Tr::tr("%L1 Hz").arg(hz, 0, 'f', 0);
}

static QCborValue numberValue(double value)
{
    // Write integral values as integers (what scripts usually expect).
    return (value == double(qint64(value))) ? QCborValue(qint64(value)) : QCborValue(value);
}

///////////////////////////////////////////////////////////////////////////////

// Approximation of the Turbo colormap by Anton Mikhailov (Google): red
// (close) -> yellow -> green -> cyan -> blue -> dark blue (far), matching
// OpenMV Studio's depth rendering.
static const struct { double t; int r, g, b; } TURBO_STOPS[] = {
    { 0.00, 200,  36,  12 },
    { 0.20, 252, 192,  12 },
    { 0.40, 100, 236,  28 },
    { 0.60,  24, 220, 180 },
    { 0.80,  40, 120, 252 },
    { 1.00,  24,  32, 112 },
};

static QRgb turboColor(double t)
{
    size_t i = 0;

    while ((i < ((sizeof(TURBO_STOPS) / sizeof(TURBO_STOPS[0])) - 2)) && (t > TURBO_STOPS[i + 1].t))
    {
        i++;
    }

    double f = (t - TURBO_STOPS[i].t) / (TURBO_STOPS[i + 1].t - TURBO_STOPS[i].t);
    return qRgb(qRound(TURBO_STOPS[i].r + (f * (TURBO_STOPS[i + 1].r - TURBO_STOPS[i].r))),
                qRound(TURBO_STOPS[i].g + (f * (TURBO_STOPS[i + 1].g - TURBO_STOPS[i].g))),
                qRound(TURBO_STOPS[i].b + (f * (TURBO_STOPS[i + 1].b - TURBO_STOPS[i].b))));
}

OpenMVChannelDepth::OpenMVChannelDepth(QWidget *parent) : QWidget(parent)
{
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true); // layouts honor heightForWidth() only with this set
    setSizePolicy(policy);
}

void OpenMVChannelDepth::setData(const QByteArray &data, int width, int height, double min, double max)
{
    if ((width <= 0) || (height <= 0) || (data.size() < int(sizeof(float)) * width * height))
    {
        return;
    }

    if ((m_image.width() != width) || (m_image.height() != height))
    {
        m_image = QImage(width, height, QImage::Format_RGB32);
        updateGeometry();
    }

    const float *values = reinterpret_cast<const float *>(data.constData());
    double range = ((max - min) != 0.0) ? (max - min) : 1.0;

    for (int y = 0; y < height; y++)
    {
        QRgb *line = reinterpret_cast<QRgb *>(m_image.scanLine(y));

        for (int x = 0; x < width; x++)
        {
            line[x] = turboColor(qBound(0.0, (double(values[(y * width) + x]) - min) / range, 1.0));
        }
    }

    QWidget::update();
}

int OpenMVChannelDepth::heightForWidth(int width) const
{
    return m_image.isNull() ? 0 : ((width * m_image.height()) / qMax(1, m_image.width()));
}

void OpenMVChannelDepth::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    if (m_image.isNull())
    {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRect(0, 0, width(), heightForWidth(width())), m_image);
}

///////////////////////////////////////////////////////////////////////////////

// CSV part files stay loadable everywhere: under Excel's 1,048,576-line
// sheet limit (with margin for a chunk's worth of overshoot), and bounded
// in bytes as a backstop for very wide records.
enum : qint64 {
    RECORD_MAX_PART_ROWS  = 1000000,
    RECORD_MAX_PART_BYTES = Q_INT64_C(512) * 1024 * 1024,
};

static QString recordBytesString(qint64 bytes)
{
    if(bytes >= (1024 * 1024))
    {
        return Tr::tr("%L1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    }

    if(bytes >= 1024)
    {
        return Tr::tr("%L1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }

    return Tr::tr("%L1 B").arg(bytes);
}

OpenMVChannelRecorder::OpenMVChannelRecorder(const QString &path, const QList<Track> &tracks,
                                             QObject *parent) : QObject(parent),
    m_basePath(path),
    m_tracks(tracks)
{
    int offset = 0;

    for(const Track &track : m_tracks)
    {
        TrackState state;
        state.offset = offset;
        m_states.append(state);
        offset += qMax(1, track.columns) + (track.gap ? 1 : 0); // value columns + any gap column
    }

    m_totalColumns = offset;
    m_hostClock.start();
    openPart();
}

OpenMVChannelRecorder::~OpenMVChannelRecorder()
{
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

    if(!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        m_error = Tr::tr("Cannot open \"%1\" - %2").arg(m_file.fileName(), m_file.errorString());
        return false;
    }

    m_partRows = 0;
    return writeHeader();
}

bool OpenMVChannelRecorder::writeHeader()
{
    QByteArray header("time");

    for(const Track &track : m_tracks)
    {
        // Track names become column names; CSV metacharacters would break
        // the layout, so they degrade to underscores.
        QString safeName = track.name;
        safeName.replace(QLatin1Char(','), QLatin1Char('_'));
        safeName.replace(QLatin1Char('"'), QLatin1Char('_'));
        QByteArray name = safeName.toUtf8();
        int columns = qMax(1, track.columns);

        for(int c = 0; c < columns; c++)
        {
            header += ',';

            if(name.isEmpty())
            {
                header += (columns > 1) ? ("series_" + QByteArray::number(c)) : QByteArrayLiteral("value");
            }
            else
            {
                header += (columns > 1) ? (name + '_' + QByteArray::number(c)) : name;
            }
        }

        if(track.gap)
        {
            header += ',';
            header += name.isEmpty() ? QByteArrayLiteral("gap") : (name + QByteArrayLiteral("_gap"));
        }
    }

    header += '\n';

    if(m_file.write(header) != header.size())
    {
        m_error = Tr::tr("Cannot write \"%1\" - %2").arg(m_file.fileName(), m_file.errorString());
        m_file.close();
        return false;
    }

    m_totalBytes += header.size();
    return true;
}

bool OpenMVChannelRecorder::rollPart()
{
    m_file.close();
    m_part += 1;
    return openPart();
}

bool OpenMVChannelRecorder::append(int track, const QByteArray &data, const QString &typecode,
                                   int samples, int series, double t, double period)
{
    if(!m_file.isOpen())
    {
        return false;
    }

    if((track < 0) || (track >= m_tracks.size()) || (samples <= 0) || data.isEmpty())
    {
        return true; // no such track / the script hasn't published a chunk yet
    }

    TrackState &state = m_states[track];

    // The poll runs faster than scripts publish: the same chunk comes back
    // repeatedly. With a timestamp, a chunk is new when t changes (or the
    // data does - a script may republish within one t quantum); without
    // one, only when the data changes.
    bool hasT = !qIsNaN(t);

    if(state.hasLast && (data == state.lastData) && ((!hasT) || (t == state.lastT)))
    {
        return true;
    }

    state.hasLast = true;
    state.lastT = hasT ? t : 0.0;
    state.lastData = data;

    // The column layout is fixed when the recording starts; a record whose
    // series count grows mid-recording has the extra series dropped, one
    // that shrinks leaves its missing columns empty.
    int actual = qMax(1, series);
    int columns = qMax(1, m_tracks.at(track).columns);
    int written = qMin(actual, columns);

    // Decode straight from the raw bytes so values land in the CSV exactly:
    // integer typecodes as integers, float32 with round-trip precision.
    char code = typecode.isEmpty() ? 'H' : typecode.at(0).toLatin1();
    int stride = ((code == 'b') || (code == 'B')) ? 1
               : ((code == 'h') || (code == 'H')) ? 2 : 4;
    int count = qMin(samples * actual, int(data.size()) / stride);
    int rows = count / actual;

    if(!rows)
    {
        return true;
    }

    // Sample times are device time: the chunk's t stamp plus the sample
    // period. Without a t stamp the host clock (seconds since the recording
    // started) anchors the chunk; without a period, samples in a chunk
    // share its timestamp.
    double base = hasT ? t : (m_hostClock.nsecsElapsed() / 1e9);

    // Data completeness check: a chunk covers rows * period seconds, so a
    // continuous source's next chunk starts where the last one ended. The
    // discontinuity (in seconds) goes in the first row's gap column when it
    // reaches a sample period - a missed chunk (published faster than the
    // poll captured) or a bursty publisher shows up here.
    QByteArray gapText;

    if(hasT && (period > 0.0))
    {
        double gap = base - state.chunkEnd;

        if(state.hasChunkTiming && (qAbs(gap) >= period))
        {
            gapText = QByteArray::number(gap, 'f', 6);
        }

        state.hasChunkTiming = true;
        state.chunkEnd = base + (rows * period);
    }

    // In a multi-track file each track owns a column group; the other
    // tracks' cells stay empty on this track's rows.
    bool gap = m_tracks.at(track).gap;
    int prefix = state.offset;
    int suffix = m_totalColumns - state.offset - (columns + (gap ? 1 : 0));

    const uchar *p = reinterpret_cast<const uchar *>(data.constData());

    QByteArray block;
    block.reserve(rows * ((m_totalColumns + 1) * 14));

    for(int r = 0; r < rows; r++)
    {
        block += QByteArray::number(base + ((period > 0.0) ? (r * period) : 0.0), 'f', 6);

        for(int c = 0; c < prefix; c++)
        {
            block += ',';
        }

        for(int c = 0; c < columns; c++)
        {
            block += ',';

            if(c >= written)
            {
                continue;
            }

            const uchar *s = p + ((qsizetype(r) * actual) + c) * stride;

            switch(code)
            {
                case 'b': block += QByteArray::number(int(qint8(*s))); break;
                case 'B': block += QByteArray::number(uint(quint8(*s))); break;
                case 'h': block += QByteArray::number(int(qFromLittleEndian<qint16>(s))); break;
                case 'i': block += QByteArray::number(qFromLittleEndian<qint32>(s)); break;
                case 'I': block += QByteArray::number(qFromLittleEndian<quint32>(s)); break;
                case 'f':
                {
                    quint32 bits = qFromLittleEndian<quint32>(s);
                    float value;
                    memcpy(&value, &bits, sizeof(value));
                    block += QByteArray::number(double(value), 'g', 9);
                    break;
                }
                case 'H':
                default: block += QByteArray::number(uint(qFromLittleEndian<quint16>(s))); break;
            }
        }

        if(gap)
        {
            block += ',';

            if(!r)
            {
                block += gapText;
            }
        }

        for(int c = 0; c < suffix; c++)
        {
            block += ',';
        }

        block += '\n';
    }

    if(m_file.write(block) != block.size())
    {
        m_error = Tr::tr("Cannot write \"%1\" - %2").arg(m_file.fileName(), m_file.errorString());
        m_file.close();
        return false;
    }

    // Push every chunk to disk so an IDE crash can't lose recorded data.
    m_file.flush();

    m_partRows += rows;
    m_totalRows += rows;
    m_totalBytes += block.size();

    // Split before a part grows unwieldy (checked per chunk, so a part can
    // overshoot by at most one chunk's rows).
    if(((m_partRows >= RECORD_MAX_PART_ROWS) || (m_file.size() >= RECORD_MAX_PART_BYTES)) && (!rollPart()))
    {
        return false;
    }

    return true;
}

QString OpenMVChannelRecorder::status() const
{
    return m_part
        ? Tr::tr("%L1 rows (%L2) - part %L3").arg(m_totalRows).arg(recordBytesString(m_totalBytes)).arg(m_part + 1)
        : Tr::tr("%L1 rows (%L2)").arg(m_totalRows).arg(recordBytesString(m_totalBytes));
}

///////////////////////////////////////////////////////////////////////////////

// Per-series trace colors (cycled).
static const QColor WAVEFORM_COLORS[] = {
    QColor(0x5b, 0x9c, 0xf5), // blue
    QColor(0xf0, 0x55, 0x55), // red
    QColor(0x4e, 0xc9, 0x62), // green
    QColor(0xfc, 0xc0, 0x2c), // yellow
    QColor(0xc9, 0x6b, 0xf0), // purple
    QColor(0x2c, 0xd5, 0xd5), // cyan
};

OpenMVChannelWaveform::OpenMVChannelWaveform(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void OpenMVChannelWaveform::setData(const QByteArray &data, const QString &typecode,
                                    int samples, int series, double min, double max)
{
    m_seriesCount = qMax(1, series);
    m_samplesPerSeries = qMax(0, samples);
    m_min = min;
    m_max = (max != min) ? max : (min + 1.0);
    m_samples = decodeSamples(data, typecode, m_samplesPerSeries * m_seriesCount);
    QWidget::update();
}

void OpenMVChannelWaveform::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();

    painter.fillRect(rect(), Utils::creatorTheme()->color(Utils::Theme::BackgroundColorNormal));

    bool dark = Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface);

    // Grid lines at 25%, 50%, 75% (stronger on the light theme's white).
    QColor faint = Utils::creatorTheme()->color(Utils::Theme::TextColorNormal);
    faint.setAlpha(dark ? 13 : 30);
    painter.setPen(QPen(faint, 1));

    for(int q = 1; q <= 3; q++)
    {
        qreal y = h - ((q / 4.0) * h);
        painter.drawLine(QPointF(0, y), QPointF(w, y));
    }

    int per = m_samplesPerSeries;

    if((per < 2) || (int(m_samples.size()) < (per * m_seriesCount)))
    {
        return;
    }

    double range = m_max - m_min;

    for(int s = 0; s < m_seriesCount; s++)
    {
        QPolygonF line;
        line.reserve(per);

        for(int i = 0; i < per; i++)
        {
            // Interleaved: sample i of series s is at (i * seriesCount) + s.
            double value = double(m_samples.at((i * m_seriesCount) + s));
            line.append(QPointF((qreal(i) / (per - 1)) * w,
                                h - (qBound(0.0, (value - m_min) / range, 1.0) * h)));
        }

        // The dark theme's pastels wash out on white; deepen them there.
        QColor trace = WAVEFORM_COLORS[s % int(sizeof(WAVEFORM_COLORS) / sizeof(WAVEFORM_COLORS[0]))];

        if(!dark)
        {
            trace = trace.darker(130);
        }

        painter.setPen(QPen(trace, 1.0));
        painter.drawPolyline(line);
    }

    // The display range, max at the top edge and min at the bottom - drawn
    // last so the numbers stay legible over the traces.
    QFont small = font();
    small.setPointSizeF(qMax(6.0, small.pointSizeF() - 2.0));
    painter.setFont(small);

    painter.setPen(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal));

    QFontMetrics metrics(small);
    painter.drawText(QPointF(2, metrics.ascent() + 1), QString::number(m_max, 'g', 6));
    painter.drawText(QPointF(2, h - 1 - metrics.descent()), QString::number(m_min, 'g', 6));
}

///////////////////////////////////////////////////////////////////////////////

OpenMVChannelsView::OpenMVChannelsView(QWidget *parent) : QStackedWidget(parent)
{
    viewApplyBackground(this);

    // Page 0: a status message, centered and styled like the frame buffer's
    // "No Image" text.
    m_message = new QLabel;
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    addWidget(m_message);

    // Page 1: the scrollable channel browser.
    QScrollArea *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->viewport()->setAutoFillBackground(false);

    QWidget *container = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(container);
    // Rows inset their own text; hairlines and the depth/waveform graphics
    // run full-bleed to the view edges, like the histogram.
    layout->setContentsMargins(0, 4, 0, 4);
    layout->setSpacing(0);

    m_contentLayout = new QVBoxLayout;
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    layout->addLayout(m_contentLayout);

    layout->addStretch(1);

    scrollArea->setWidget(container);
    addWidget(scrollArea);

    reset();
}

void OpenMVChannelsView::showMessage(const QString &message)
{
    clearContent();
    m_message->setText(viewMessageHtml(message));
    setCurrentIndex(0);
}

void OpenMVChannelsView::reset()
{
    m_activeControls.clear();
    m_skipRenders.clear();
    showMessage(Tr::tr("Connect a camera to view channels"));
}

void OpenMVChannelsView::clearContent()
{
    while(QLayoutItem *item = m_contentLayout->takeAt(0))
    {
        delete item->widget();
        delete item;
    }

    m_records.clear();
    m_sections.clear();
    m_schema.clear();
}

QList<OpenMVChannelsView::Record> OpenMVChannelsView::decode(const QVariantList &channels,
                                                             QString *schema) const
{
    QList<Record> records;
    QStringList schemaParts;

    // Channels render in name order, like OpenMV Studio.
    QVariantList sorted = channels;
    std::sort(sorted.begin(), sorted.end(), [] (const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString() <
               b.toMap().value(QStringLiteral("name")).toString();
    });

    for(const QVariant &entry : sorted)
    {
        QVariantMap channel = entry.toMap();
        QString channelName = channel.value(QStringLiteral("name")).toString();
        quint8 flags = quint8(channel.value(QStringLiteral("flags")).toUInt());

        QCborParserError error;
        QCborValue root = QCborValue::fromCbor(channel.value(QStringLiteral("data")).toByteArray(), &error);

        if((error.error != QCborError::NoError) || (!root.isArray()))
        {
            continue;
        }

        QString baseName;
        QCborArray array = root.toArray();

        for(qsizetype i = 0; i < array.size(); i++)
        {
            QCborMap rec = array.at(i).toMap();

            if(rec.contains(qint64(CBOR_KEY_BN)))
            {
                baseName = rec.value(qint64(CBOR_KEY_BN)).toString();
            }

            Record record;
            record.channelName = channelName;
            record.flags = flags;
            record.name = baseName + rec.value(qint64(CBOR_KEY_N)).toString();
            record.wtype = rec.value(qint64(CBOR_KEY_W_TYPE)).toString();
            record.rec = rec;
            records.append(record);

            // Select/radio options are baked into their widgets at build, so
            // they're part of the schema: a script repopulating a select's
            // options (WiFi scan results, SD file lists) triggers a rebuild.
            QString options;

            if(rec.contains(qint64(CBOR_KEY_W_OPTS)))
            {
                QCborArray optionsArray = rec.value(qint64(CBOR_KEY_W_OPTS)).toArray();
                QStringList list;

                for(qsizetype j = 0; j < optionsArray.size(); j++)
                {
                    list.append(optionsArray.at(j).toString());
                }

                options = list.join(QLatin1Char(','));
            }

            schemaParts.append(QStringLiteral("%1:%2:%3:%4").arg(channelName, record.name, record.wtype, options));
        }
    }

    *schema = schemaParts.join(QLatin1Char('|'));
    return records;
}

void OpenMVChannelsView::buildContent(QList<Record> &records)
{
    clearContent();

    // Channels with more than one recordable graph (waveform or depth) get
    // a Record All bar under their section label, capturing every graph
    // into one multi-track file. With a single graph the per-graph button
    // already covers it, so no bar.
    QMap<QString, int> recordableCounts;

    for(const Record &record : records)
    {
        if((record.wtype == QStringLiteral("waveform")) || (record.wtype == QStringLiteral("depth")))
        {
            recordableCounts[record.channelName]++;
        }
    }

    // The per-graph CSV recording controls: Record/Stop plus a live
    // rows/size readout while data is being captured.
    auto addRecordBar = [this](Record &record, QVBoxLayout *rowLayout, int index) {
        QWidget *recordBar = new QWidget;
        QHBoxLayout *recordBarLayout = new QHBoxLayout(recordBar);
        recordBarLayout->setContentsMargins(6, 0, 6, 0); // text inset; the graph above is full-bleed
        recordBarLayout->setSpacing(8);

        record.recordButton = new QPushButton(Tr::tr("Record"));
        record.recordButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        recordBarLayout->addWidget(record.recordButton);

        record.recordStatus = viewNameLabel(QString());
        recordBarLayout->addWidget(record.recordStatus, 1);

        rowLayout->addWidget(recordBar);

        connect(record.recordButton, &QPushButton::clicked, this, [this, index] {
            toggleRecording(index);
        });
    };

    QString previousChannel;
    int currentSection = -1;
    int trackCounter = 0;

    for(int i = 0; i < records.size(); i++)
    {
        Record &record = records[i];

        if(record.channelName != previousChannel)
        {
            m_contentLayout->addWidget(viewSectionLabel(record.channelName));
            previousChannel = record.channelName;
            currentSection = -1;
            trackCounter = 0;

            if(recordableCounts.value(record.channelName, 0) > 1)
            {
                QWidget *bar = new QWidget;
                QHBoxLayout *barLayout = new QHBoxLayout(bar);
                barLayout->setContentsMargins(6, 3, 6, 3);
                barLayout->setSpacing(8);

                Section section;
                section.channelName = record.channelName;
                section.bar = bar;

                section.button = new QPushButton(Tr::tr("Record All"));
                section.button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
                barLayout->addWidget(section.button);

                section.status = viewNameLabel(QString());
                barLayout->addWidget(section.status, 1);

                m_contentLayout->addWidget(bar);
                m_sections.append(section);
                currentSection = m_sections.size() - 1;

                connect(section.button, &QPushButton::clicked, this, [this, currentSection] {
                    toggleGroupRecording(currentSection);
                });
            }
        }

        bool writable = (record.flags & CHANNEL_FLAG_WRITE) != 0;
        QString controlId = QStringLiteral("%1/%2").arg(record.channelName, record.name);
        QString channelName = record.channelName;
        QString recordName = record.name;

        if(record.wtype == QStringLiteral("depth"))
        {
            record.sectionIndex = currentSection;
            record.trackIndex = trackCounter++;

            // Header (name, size, range) above the colormapped image. The
            // image runs full-bleed; the header and record bar inset.
            QWidget *row = new QWidget;
            QVBoxLayout *rowLayout = new QVBoxLayout(row);
            rowLayout->setContentsMargins(0, 3, 0, 3);
            rowLayout->setSpacing(4);

            record.depthHeader = viewNameLabel(QString());
            record.depthHeader->setContentsMargins(6, 0, 6, 0);
            rowLayout->addWidget(record.depthHeader);

            record.depth = new OpenMVChannelDepth;
            rowLayout->addWidget(record.depth);

            addRecordBar(record, rowLayout, i);

            record.row = row;
            m_contentLayout->addWidget(row);
        }
        else if(record.wtype == QStringLiteral("waveform"))
        {
            record.sectionIndex = currentSection;
            record.trackIndex = trackCounter++;

            // Header (name, geometry, rate) above the traces. The plot runs
            // full-bleed; the header and record bar inset.
            QWidget *row = new QWidget;
            QVBoxLayout *rowLayout = new QVBoxLayout(row);
            rowLayout->setContentsMargins(0, 3, 0, 3);
            rowLayout->setSpacing(4);

            record.waveformHeader = viewNameLabel(QString());
            record.waveformHeader->setContentsMargins(6, 0, 6, 0);
            rowLayout->addWidget(record.waveformHeader);

            record.waveform = new OpenMVChannelWaveform;
            rowLayout->addWidget(record.waveform);

            addRecordBar(record, rowLayout, i);

            record.row = row;
            m_contentLayout->addWidget(row);
        }
        else if(record.wtype == QStringLiteral("toggle"))
        {
            record.toggle = new QCheckBox;
            record.toggle->setEnabled(writable);

            if(writable)
            {
                connect(record.toggle, &QCheckBox::clicked, this, [this, channelName, recordName] (bool checked) {
                    stageWrite(channelName, recordName, QCborValue(checked));
                });
            }

            record.row = viewRow(record.name, record.toggle);
            m_contentLayout->addWidget(record.row);
        }
        else if(record.wtype == QStringLiteral("slider"))
        {
            // Name and live value on top, the slider full-width below.
            QWidget *row = new QWidget;
            QVBoxLayout *rowLayout = new QVBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(0);

            QWidget *header = new QWidget;
            QHBoxLayout *headerLayout = new QHBoxLayout(header);
            headerLayout->setContentsMargins(4, 3, 4, 0);
            headerLayout->setSpacing(8);
            headerLayout->addWidget(viewNameLabel(record.name));
            headerLayout->addStretch(1);
            record.sliderValue = viewValueLabel();
            headerLayout->addWidget(record.sliderValue);
            QString unit = record.rec.value(qint64(CBOR_KEY_U)).toString();

            if(!unit.isEmpty())
            {
                headerLayout->addWidget(viewNameLabel(unit));
            }

            rowLayout->addWidget(header);

            record.slider = new QSlider(Qt::Horizontal);
            record.slider->setEnabled(writable);
            rowLayout->addWidget(record.slider);

            if(writable)
            {
                connect(record.slider, &QSlider::sliderPressed, this, [this, controlId] {
                    m_activeControls.insert(controlId);
                });

                connect(record.slider, &QSlider::sliderReleased, this, [this, controlId, i] {
                    m_activeControls.remove(controlId);

                    if(i < m_records.size())
                    {
                        const Record &r = m_records.at(i);
                        stageWrite(r.channelName, r.name, numberValue(
                            r.sliderMin + (r.slider->value() * r.sliderStep)));
                    }
                });

                connect(record.slider, &QSlider::valueChanged, this, [this, i] (int position) {
                    if(i < m_records.size())
                    {
                        const Record &r = m_records.at(i);
                        r.sliderValue->setText(displayValue(numberValue(
                            r.sliderMin + (position * r.sliderStep))));
                    }
                });
            }

            record.row = row;
            m_contentLayout->addWidget(row);
        }
        else if(record.wtype == QStringLiteral("spinbox"))
        {
            // Precise stepped entry (issue #157: +/- buttons in fixed
            // increments) - a spin box over the slider's min/max/step keys.
            // Named like the Settings Editor's element; the step decides the
            // decimals, so one type covers spinbox and doublespinbox.
            record.spinbox = new QDoubleSpinBox;
            record.spinbox->setEnabled(writable);
            // Type freely without a write per keystroke; valueChanged fires
            // on the arrows, Return, and focus-out.
            record.spinbox->setKeyboardTracking(false);

            if(writable)
            {
                connect(record.spinbox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
                        this, [this, channelName, recordName] (double value) {
                    stageWrite(channelName, recordName, numberValue(value));
                });
            }

            QList<QWidget *> values;
            values.append(record.spinbox);
            QString unit = record.rec.value(qint64(CBOR_KEY_U)).toString();

            if(!unit.isEmpty())
            {
                values.append(viewNameLabel(unit));
            }

            record.row = viewRow(record.name, values);
            m_contentLayout->addWidget(record.row);
        }
        else if(record.wtype == QStringLiteral("radio"))
        {
            // A select rendered as radio buttons - same options/value keys.
            QWidget *box = new QWidget;
            QHBoxLayout *boxLayout = new QHBoxLayout(box);
            boxLayout->setContentsMargins(0, 0, 0, 0);
            boxLayout->setSpacing(8);

            record.radio = new QButtonGroup(box);
            QCborArray options = record.rec.value(qint64(CBOR_KEY_W_OPTS)).toArray();

            for(qsizetype j = 0; j < options.size(); j++)
            {
                QRadioButton *button = new QRadioButton(options.at(j).toString());
                button->setEnabled(writable);
                record.radio->addButton(button);
                boxLayout->addWidget(button);
            }

            if(writable)
            {
                // clicked only fires on user interaction, so the patch's
                // programmatic setChecked() can't echo a write back.
                connect(record.radio, &QButtonGroup::buttonClicked, this, [this, channelName, recordName] (QAbstractButton *button) {
                    stageWrite(channelName, recordName, QCborValue(button->text()));
                });
            }

            record.row = viewRow(record.name, box);
            m_contentLayout->addWidget(record.row);
        }
        else if(record.wtype == QStringLiteral("lineedit"))
        {
            record.lineedit = new QLineEdit;
            record.lineedit->setEnabled(writable);

            if(writable)
            {
                // Writes on Return and focus-out, not per keystroke.
                connect(record.lineedit, &QLineEdit::editingFinished, this, [this, i] {
                    if(i < m_records.size())
                    {
                        const Record &r = m_records.at(i);
                        stageWrite(r.channelName, r.name, QCborValue(r.lineedit->text()));
                    }
                });
            }

            record.row = viewRow(record.name, record.lineedit);
            m_contentLayout->addWidget(record.row);
        }
        else if(record.wtype == QStringLiteral("text"))
        {
            // The Settings Editor's static label element ("label" here
            // already means a name/value readout row, matching OpenMV
            // Studio). Rich text rides in the record's value, so scripts can
            // update it live; the existing value-label patch handles that.
            record.value = new QLabel;
            record.value->setWordWrap(true);
            record.value->setTextInteractionFlags(Qt::TextBrowserInteraction);
            record.value->setOpenExternalLinks(true);

            QWidget *row = new QWidget;
            QVBoxLayout *rowLayout = new QVBoxLayout(row);
            rowLayout->setContentsMargins(6, 3, 6, 3);
            rowLayout->addWidget(record.value);

            record.row = row;
            m_contentLayout->addWidget(row);
        }
        else if(record.wtype == QStringLiteral("pushbutton"))
        {
            // A momentary action (trigger/calibrate/start): clicking writes
            // true to the record; there's no state to render back.
            record.pushbutton = new QPushButton(record.name);
            record.pushbutton->setEnabled(writable);

            if(writable)
            {
                connect(record.pushbutton, &QPushButton::clicked, this, [this, channelName, recordName] {
                    stageWrite(channelName, recordName, QCborValue(true));
                });
            }

            record.row = viewRow(QString(), record.pushbutton);
            m_contentLayout->addWidget(record.row);
        }
        else if(record.wtype == QStringLiteral("select"))
        {
            record.select = new QComboBox;
            record.select->setEnabled(writable);

            QCborArray options = record.rec.value(qint64(CBOR_KEY_W_OPTS)).toArray();

            for(qsizetype j = 0; j < options.size(); j++)
            {
                record.select->addItem(options.at(j).toString());
            }

            if(writable)
            {
                connect(record.select, &QComboBox::textActivated, this, [this, channelName, recordName] (const QString &text) {
                    stageWrite(channelName, recordName, QCborValue(text));
                });
            }

            record.row = viewRow(record.name, record.select);
            m_contentLayout->addWidget(record.row);
        }
        else // "label" and untyped records: [icon +] name, value, unit.
        {
            record.value = viewValueLabel();

            QList<QWidget *> values;
            values.append(record.value);
            QString unit = record.rec.value(qint64(CBOR_KEY_U)).toString();

            if(!unit.isEmpty())
            {
                values.append(viewNameLabel(unit));
            }

            QLabel *icon = (record.wtype == QStringLiteral("label"))
                ? unitIconLabel(unit) : Q_NULLPTR;

            if(icon)
            {
                QWidget *name = new QWidget;
                QHBoxLayout *nameLayout = new QHBoxLayout(name);
                nameLayout->setContentsMargins(0, 0, 0, 0);
                nameLayout->setSpacing(4);
                nameLayout->addWidget(icon);
                nameLayout->addWidget(viewNameLabel(record.name));
                record.row = viewRow(name, values);
            }
            else
            {
                record.row = viewRow(record.name, values);
            }

            m_contentLayout->addWidget(record.row);
        }
    }
}

void OpenMVChannelsView::patchContent(QList<Record> &records)
{
    for(int i = 0; i < records.size(); i++)
    {
        const Record &fresh = records.at(i);
        Record &built = m_records[i];
        built.rec = fresh.rec;
        built.flags = fresh.flags;

        // Collecting the data is the point of recording; the widgets are
        // just the monitor. Append before any render gating below.
        bool groupRecording = (built.sectionIndex >= 0) && (built.sectionIndex < m_sections.size())
            && m_sections.at(built.sectionIndex).recorder;

        if(built.recorder || groupRecording)
        {
            int samples, series;
            double period;
            QString typecode;

            if(built.depth)
            {
                // A depth frame is one row of w*h float32 values with no
                // sample period.
                int width = int(built.rec.value(qint64(CBOR_KEY_W)).toInteger());
                int height = int(built.rec.value(qint64(CBOR_KEY_H)).toInteger());
                samples = 1;
                series = qMax(1, width * height);
                period = 0.0;
                typecode = QStringLiteral("f");
            }
            else
            {
                samples = int(built.rec.value(qint64(CBOR_KEY_W)).toInteger());
                series = int(built.rec.value(qint64(CBOR_KEY_H)).toInteger(1));
                period = built.rec.value(qint64(CBOR_KEY_UT)).toDouble();
                typecode = built.rec.value(qint64(CBOR_KEY_VS)).toString();
            }

            double t = built.rec.contains(qint64(CBOR_KEY_T))
                ? built.rec.value(qint64(CBOR_KEY_T)).toDouble() : qQNaN();
            QByteArray data = built.rec.value(qint64(CBOR_KEY_VD)).toByteArray();

            if(built.recorder)
            {
                if(built.recorder->append(0, data, typecode, samples, series, t, period))
                {
                    built.recordStatus->setText(built.recorder->status());
                }
                else
                {
                    // A failed disk write ends the recording. The message box
                    // is queued: a modal loop here could rebuild m_records
                    // under this loop's feet.
                    QString error = built.recorder->errorString();
                    delete built.recorder;
                    built.recorder = Q_NULLPTR;
                    built.recordButton->setText(Tr::tr("Record"));
                    built.recordStatus->setText(error);
                    updateRecordButtonStates();

                    QMetaObject::invokeMethod(this, [this, error] {
                        QMessageBox::critical(this, Tr::tr("Record Channel"), error);
                    }, Qt::QueuedConnection);
                }
            }

            if(groupRecording)
            {
                Section &section = m_sections[built.sectionIndex];

                if(section.recorder->append(built.trackIndex, data, typecode, samples, series, t, period))
                {
                    section.status->setText(section.recorder->status());
                }
                else
                {
                    QString error = section.recorder->errorString();
                    delete section.recorder;
                    section.recorder = Q_NULLPTR;
                    section.button->setText(Tr::tr("Record All"));
                    section.status->setText(error);
                    updateRecordButtonStates();

                    QMetaObject::invokeMethod(this, [this, error] {
                        QMessageBox::critical(this, Tr::tr("Record Channel"), error);
                    }, Qt::QueuedConnection);
                }
            }
        }

        // A write is in flight on this channel: don't render pre-write data.
        if(m_skipRenders.value(built.channelName, 0) > 0)
        {
            continue;
        }

        QString controlId = QStringLiteral("%1/%2").arg(built.channelName, built.name);

        if(built.value)
        {
            built.value->setText(displayValue(recordValue(built.rec)));
        }

        if(built.depth)
        {
            int width = int(built.rec.value(qint64(CBOR_KEY_W)).toInteger());
            int height = int(built.rec.value(qint64(CBOR_KEY_H)).toInteger());
            double min = built.rec.value(qint64(CBOR_KEY_MIN)).toDouble();
            double max = built.rec.value(qint64(CBOR_KEY_MAX)).toDouble(1.0);

            built.depthHeader->setText(Tr::tr("%1 (%2x%3, %4-%5mm)").arg(built.name)
                .arg(width).arg(height).arg(qRound(min)).arg(qRound(max)));
            built.depth->setData(built.rec.value(qint64(CBOR_KEY_VD)).toByteArray(),
                                 width, height, min, max);
        }

        if(built.waveform)
        {
            int samples = int(built.rec.value(qint64(CBOR_KEY_W)).toInteger());
            int series = int(built.rec.value(qint64(CBOR_KEY_H)).toInteger(1));
            double min = built.rec.value(qint64(CBOR_KEY_MIN)).toDouble();
            double max = built.rec.value(qint64(CBOR_KEY_MAX)).toDouble(1.0);
            double period = built.rec.value(qint64(CBOR_KEY_UT)).toDouble();
            QString typecode = built.rec.value(qint64(CBOR_KEY_VS)).toString();

            QString rate = rateString(period);
            QString geometry = (series > 1)
                ? QStringLiteral("%L1x%L2").arg(samples).arg(series)
                : QStringLiteral("%L1").arg(samples);
            built.waveformHeader->setText(rate.isEmpty()
                ? Tr::tr("%1 (%2)").arg(built.name, geometry)
                : Tr::tr("%1 (%2 @ %3)").arg(built.name, geometry, rate));
            built.waveform->setData(built.rec.value(qint64(CBOR_KEY_VD)).toByteArray(),
                                    typecode, samples, series, min, max);
        }

        if(built.toggle && (!m_activeControls.contains(controlId)))
        {
            QSignalBlocker blocker(built.toggle);
            built.toggle->setChecked(recordValue(built.rec).toBool());
        }

        if(built.slider && (!m_activeControls.contains(controlId)))
        {
            double min = built.rec.value(qint64(CBOR_KEY_W_MIN)).toDouble();
            double max = built.rec.value(qint64(CBOR_KEY_W_MAX)).toDouble(100.0);
            double step = built.rec.value(qint64(CBOR_KEY_W_STEP)).toDouble(1.0);
            double value = recordValue(built.rec).toDouble();

            built.sliderMin = min;
            built.sliderStep = (step > 0.0) ? step : 1.0;

            QSignalBlocker blocker(built.slider);
            built.slider->setRange(0, sliderSteps(min, max, step));
            built.slider->setValue(qRound((value - min) / built.sliderStep));
            built.sliderValue->setText(displayValue(recordValue(built.rec)));
        }

        if(built.spinbox && (!built.spinbox->hasFocus()))
        {
            // hasFocus() plays the slider's drag guard: while the user is
            // typing or stepping, reads must not snap the box back.
            double min = built.rec.value(qint64(CBOR_KEY_W_MIN)).toDouble();
            double max = built.rec.value(qint64(CBOR_KEY_W_MAX)).toDouble(100.0);
            double step = built.rec.value(qint64(CBOR_KEY_W_STEP)).toDouble(1.0);
            step = (step > 0.0) ? step : 1.0;

            // Just enough decimals to represent the step (0.1 -> 1, 0.05 -> 2).
            int decimals = 0;

            for(double s = step; (s < 0.999999) && (decimals < 6); s *= 10.0)
            {
                decimals++;
            }

            QSignalBlocker blocker(built.spinbox);
            built.spinbox->setDecimals(decimals);
            built.spinbox->setRange(min, max);
            built.spinbox->setSingleStep(step);
            built.spinbox->setValue(recordValue(built.rec).toDouble());
        }

        if(built.radio)
        {
            // setChecked() doesn't emit clicked, so no blocker is needed.
            QString value = recordValue(built.rec).toString();

            for(QAbstractButton *button : built.radio->buttons())
            {
                if(button->text() == value)
                {
                    button->setChecked(true);
                    break;
                }
            }
        }

        if(built.lineedit && (!built.lineedit->hasFocus()))
        {
            // hasFocus() plays the drag guard: reads must not overwrite text
            // mid-typing. setText() doesn't emit editingFinished.
            built.lineedit->setText(recordValue(built.rec).toString());
        }

        if(built.select && (!m_activeControls.contains(controlId)))
        {
            QSignalBlocker blocker(built.select);
            built.select->setCurrentText(recordValue(built.rec).toString());
        }
    }

    // Each render drains one skip per channel.
    for(auto it = m_skipRenders.begin(); it != m_skipRenders.end(); )
    {
        if((--it.value()) <= 0)
        {
            it = m_skipRenders.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void OpenMVChannelsView::channelsData(const QVariantList &channels)
{
    if(channels.isEmpty())
    {
        showMessage(Tr::tr("No channels are being published by the camera"));
        return;
    }

    QString schema;
    QList<Record> records = decode(channels, &schema);

    if(records.isEmpty())
    {
        // Channels exist but none produced records: distinguish from the
        // no-channels case (a decode problem rather than nothing published).
        showMessage(Tr::tr("%n channel(s) published, but no records decoded", "", int(channels.size())));
        return;
    }

    if(schema != m_schema)
    {
        buildContent(records);
        m_schema = schema;
        m_records = records;
        m_activeControls.clear();
        m_skipRenders.clear();

        // The freshly-built widgets still need their first values.
        patchContent(records);
    }
    else
    {
        patchContent(records);
    }

    setCurrentIndex(1);
}

void OpenMVChannelsView::updateRecordButtonStates()
{
    // Group and per-graph recordings are mutually exclusive: while a Record
    // All runs the per-graph buttons are disabled, and while any per-graph
    // recording runs the Record All buttons are disabled.
    bool anyIndividual = false;
    bool anyGroup = false;

    for(const Record &record : m_records)
    {
        anyIndividual |= (record.recorder != Q_NULLPTR);
    }

    for(const Section &section : m_sections)
    {
        anyGroup |= (section.recorder != Q_NULLPTR);
    }

    for(const Record &record : m_records)
    {
        if(record.recordButton)
        {
            record.recordButton->setEnabled(!anyGroup);
        }
    }

    for(const Section &section : m_sections)
    {
        section.button->setEnabled(!anyIndividual);
    }
}

// The recorder track a record's data maps to: a depth frame is one value
// column per pixel with no gap column; a waveform is one column per series
// plus a gap column. Depth tracks are always named (their column count
// makes bare value_N names useless); waveform tracks are named only in
// multi-track files to keep the single-graph layout stable.
OpenMVChannelRecorder::Track OpenMVChannelsView::trackFor(const Record &record, bool named) const
{
    if(record.depth)
    {
        int width = int(record.rec.value(qint64(CBOR_KEY_W)).toInteger());
        int height = int(record.rec.value(qint64(CBOR_KEY_H)).toInteger());
        return {record.name, width * height, false};
    }

    int series = int(record.rec.value(qint64(CBOR_KEY_H)).toInteger(1));
    return {named ? record.name : QString(), qMax(1, series), true};
}

void OpenMVChannelsView::toggleRecording(int index)
{
    if((index < 0) || (index >= m_records.size()))
    {
        return;
    }

    if(m_records.at(index).recorder)
    {
        Record &record = m_records[index];
        record.recordStatus->setText(Tr::tr("Saved %1").arg(record.recorder->status()));
        delete record.recorder;
        record.recorder = Q_NULLPTR;
        record.recordButton->setText(Tr::tr("Record"));
        updateRecordButtonStates();
        return;
    }

    QString channelName = m_records.at(index).channelName;
    QString recordName = m_records.at(index).name;

    // Each graph remembers its own last save path ("OpenMV" matches the
    // plugin's SETTINGS_GROUP).
    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    const Utils::Key pathKey = Utils::keyFromString(
        QStringLiteral("OpenMV/LastChannelRecordPath/%1/%2").arg(channelName, recordName));

    QString suggestion = settings->value(pathKey).toString();

    if(suggestion.isEmpty())
    {
        QString safeName = recordName;
        safeName.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
        suggestion = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(safeName + QStringLiteral(".csv"));
    }

    QString path = QFileDialog::getSaveFileName(this, Tr::tr("Record \"%1\" To").arg(recordName),
        suggestion, Tr::tr("CSV Files (*.csv);;All Files (*)"));

    if(path.isEmpty())
    {
        return;
    }

    settings->setValue(pathKey, path);

    // Re-locate the record by name - data kept flowing while the file
    // dialog was up, and a schema change rebuilds m_records.
    for(int i = 0; i < m_records.size(); i++)
    {
        Record &record = m_records[i];

        if((record.channelName == channelName) && (record.name == recordName) && record.recordButton)
        {
            OpenMVChannelRecorder::Track track = trackFor(record, false);

            if(track.columns <= 0)
            {
                // A depth record that hasn't published a frame yet has no
                // dimensions - there's no column layout to record into.
                record.recordStatus->setText(Tr::tr("No data to record yet"));
                return;
            }

            OpenMVChannelRecorder *recorder = new OpenMVChannelRecorder(path, {track}, record.row);

            if(!recorder->ok())
            {
                QString error = recorder->errorString();
                delete recorder;
                QMessageBox::critical(this, Tr::tr("Record Channel"), error);
                return;
            }

            record.recorder = recorder;
            record.recordButton->setText(Tr::tr("Stop"));
            record.recordStatus->setText(Tr::tr("Recording..."));
            updateRecordButtonStates();
            return;
        }
    }
}

void OpenMVChannelsView::toggleGroupRecording(int index)
{
    if((index < 0) || (index >= m_sections.size()))
    {
        return;
    }

    if(m_sections.at(index).recorder)
    {
        Section &section = m_sections[index];
        section.status->setText(Tr::tr("Saved %1").arg(section.recorder->status()));
        delete section.recorder;
        section.recorder = Q_NULLPTR;
        section.button->setText(Tr::tr("Record All"));
        updateRecordButtonStates();
        return;
    }

    QString channelName = m_sections.at(index).channelName;

    // The whole-channel recording remembers its own last save path,
    // separate from the per-graph paths ("OpenMV" matches the plugin's
    // SETTINGS_GROUP).
    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    const Utils::Key pathKey = Utils::keyFromString(
        QStringLiteral("OpenMV/LastChannelRecordPath/%1/__all__").arg(channelName));

    QString suggestion = settings->value(pathKey).toString();

    if(suggestion.isEmpty())
    {
        QString safeName = channelName;
        safeName.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
        suggestion = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(safeName + QStringLiteral(".csv"));
    }

    QString path = QFileDialog::getSaveFileName(this, Tr::tr("Record \"%1\" To").arg(channelName),
        suggestion, Tr::tr("CSV Files (*.csv);;All Files (*)"));

    if(path.isEmpty())
    {
        return;
    }

    settings->setValue(pathKey, path);

    // Re-locate the section by name - data kept flowing while the file
    // dialog was up, and a schema change rebuilds m_sections.
    for(int s = 0; s < m_sections.size(); s++)
    {
        Section &section = m_sections[s];

        if(section.channelName != channelName)
        {
            continue;
        }

        // One track per graph, in build order (matching each record's
        // trackIndex).
        QList<OpenMVChannelRecorder::Track> tracks;

        for(const Record &record : m_records)
        {
            if((record.channelName == channelName) && (record.sectionIndex == s)
                && (record.waveform || record.depth))
            {
                OpenMVChannelRecorder::Track track = trackFor(record, true);

                if(track.columns <= 0)
                {
                    // A depth record without a frame yet has no dimensions;
                    // the file's column layout can't be fixed until every
                    // graph has one.
                    section.status->setText(Tr::tr("No data to record yet"));
                    return;
                }

                tracks.append(track);
            }
        }

        if(tracks.isEmpty())
        {
            return;
        }

        OpenMVChannelRecorder *recorder = new OpenMVChannelRecorder(path, tracks, section.bar);

        if(!recorder->ok())
        {
            QString error = recorder->errorString();
            delete recorder;
            QMessageBox::critical(this, Tr::tr("Record Channel"), error);
            return;
        }

        section.recorder = recorder;
        section.button->setText(Tr::tr("Stop"));
        section.status->setText(Tr::tr("Recording..."));
        updateRecordButtonStates();
        return;
    }
}

void OpenMVChannelsView::stageWrite(const QString &channelName, const QString &recordName,
                                    const QCborValue &value)
{
    // Skip the next couple of renders for this channel: a read may already be
    // in flight carrying pre-write data that would snap the control back.
    m_skipRenders[channelName] = 2;
    emit writeChannel(channelName, encodeWrite(recordName, value));
}

} // namespace Internal
} // namespace OpenMV
