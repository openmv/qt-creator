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
#include "openmvchannelrecorder.h"
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
    CBOR_KEY_T  = 6,   // time: waveform chunk timestamp (integer us, or SenML float seconds)
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

// A camera on a single-precision-float build can't hold microsecond
// resolution in a float timestamp of seconds, so it sends an integer count of
// microseconds; a float t (older/other senders) stays SenML seconds. Both
// normalize to seconds here, NaN when the sender omits the field.
static double chunkTime(const QCborMap &rec)
{
    if(!rec.contains(qint64(CBOR_KEY_T)))
    {
        return qQNaN();
    }

    QCborValue tValue = rec.value(qint64(CBOR_KEY_T));
    return tValue.isInteger() ? (tValue.toInteger() / 1e6) : tValue.toDouble();
}

// The next free <name>.<n>.<suffix> in a folder. Recording a section twice
// would otherwise write the second capture over the first, and the numbering
// is the shape Edge Impulse reads a label and index out of.
static QString numberedRecordPath(const QDir &dir, const QString &base, const QString &suffix)
{
    for(int n = 1; ; n++)
    {
        QString path = dir.filePath(QStringLiteral("%1.%2.%3").arg(base).arg(n).arg(suffix));

        if(!QFileInfo::exists(path))
        {
            return path;
        }
    }
}

// A record or channel name reduced to something a file system will take.
static QString recordFileName(const QString &name)
{
    QString safe = name;
    safe.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    return safe;
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

// The scrolling window is a couple of chunks wide -- enough to read the
// waveform's shape, the way the single-chunk plot this replaced did -- with
// a long tail of history behind it to pan back into.
static const double WAVEFORM_VIEW_CHUNKS = 2.0;
static const double WAVEFORM_VIEW_MIN_SAMPLES = 256.0;
static const double WAVEFORM_VIEW_MAX_SAMPLES = 1024.0;
static const double WAVEFORM_HISTORY_MULTIPLE = 50.0;
// What the History menu offers, as screens of history kept behind the view.
static const double WAVEFORM_HISTORY_CHOICES[] = {10.0, 50.0, 250.0};

static QString readoutNumber(double value, int digits = 4);
static const double WAVEFORM_HELD_HISTORY_MULTIPLE = 10.0;

// "OpenMV" matches the plugin's SETTINGS_GROUP.
static Utils::Key plotPathKey(const QString &key)
{
    return Utils::keyFromString(QStringLiteral("OpenMV/LastChannelPlotPath/%1").arg(key));
}

static Utils::Key plotAutoScaleKey(const QString &key)
{
    return Utils::keyFromString(QStringLiteral("OpenMV/ChannelPlotAutoScale/%1").arg(key));
}

static Utils::Key plotHistoryKey(const QString &key)
{
    return Utils::keyFromString(QStringLiteral("OpenMV/ChannelPlotHistory/%1").arg(key));
}

OpenMVChannelWaveform::OpenMVChannelWaveform(QWidget *parent) : QCustomPlot(parent)
{
    setFixedHeight(130);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectLegend);
    setNoAntialiasingOnDrag(true);
    setPlottingHint(QCP::phFastPolylines, true);
    setAutoAddPlottableToLegend(false);

    // Dragging or zooming means the user is looking at something specific, so
    // stop yanking the view forward; double click hands them back the live end.
    connect(this, &QCustomPlot::mousePress, this, [this] (QMouseEvent *event) {
        // Only a left drag pans, so only a left press means the user is
        // leaving the live end. A right click is opening the context menu and
        // a legend click is aimed at a trace; neither should stop following.
        if((event->button() != Qt::LeftButton)
        || (legend->visible() && legend->rect().contains(event->pos())))
        {
            return;
        }

        m_following = false;
    });
    connect(this, &QCustomPlot::mouseWheel, this, [this] { m_following = false; });

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested,
            this, &OpenMVChannelWaveform::showContextMenu);

    // Clicking a legend entry hides and shows that trace, which is the only
    // practical way to read one series out of an overlaid three-axis plot.
    connect(this, &QCustomPlot::legendClick, this,
            [this] (QCPLegend *, QCPAbstractLegendItem *item, QMouseEvent *) {
        QCPPlottableLegendItem *plottableItem = qobject_cast<QCPPlottableLegendItem *>(item);

        if(plottableItem)
        {
            for(int s = 0; s < graphCount(); s++)
            {
                if(graph(s) == plottableItem->plottable())
                {
                    setSeriesVisible(s, !graph(s)->visible());
                }
            }

            updateReadout();
            replot(QCustomPlot::rpQueuedReplot);
        }
    });
    connect(this, &QCustomPlot::mouseDoubleClick, this, [this] {
        m_following = true;
        yAxis->setRange(m_min, m_max);
        if(m_hasData) xAxis->setRange(m_end - viewSpan(), m_end);
        replot(QCustomPlot::rpQueuedReplot);
    });

    // A vertical rule at the cursor, and a corner box reading out the values
    // it crosses. Created once; the per-series tracer dots come and go with
    // the series.
    m_cursor = new QCPItemStraightLine(this);
    m_cursor->setVisible(false);
    m_cursor->point1->setTypeY(QCPItemPosition::ptAxisRectRatio);
    m_cursor->point2->setTypeY(QCPItemPosition::ptAxisRectRatio);

    m_pinned = new QCPItemStraightLine(this);
    m_pinned->setVisible(false);
    m_pinned->point1->setTypeY(QCPItemPosition::ptAxisRectRatio);
    m_pinned->point2->setTypeY(QCPItemPosition::ptAxisRectRatio);

    // The armed level, and where the crossing landed once it fires.
    m_triggerLine = new QCPItemStraightLine(this);
    m_triggerLine->setVisible(false);
    m_triggerLine->point1->setTypeX(QCPItemPosition::ptAxisRectRatio);
    m_triggerLine->point2->setTypeX(QCPItemPosition::ptAxisRectRatio);

    m_triggerMark = new QCPItemStraightLine(this);
    m_triggerMark->setVisible(false);
    m_triggerMark->point1->setTypeY(QCPItemPosition::ptAxisRectRatio);
    m_triggerMark->point2->setTypeY(QCPItemPosition::ptAxisRectRatio);

    m_readout = new QCPItemText(this);
    m_readout->setVisible(false);
    m_readout->setPositionAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_readout->setTextAlignment(Qt::AlignLeft);
    m_readout->position->setType(QCPItemPosition::ptAxisRectRatio);
    m_readout->position->setCoords(0.0, 0.0);
    m_readout->setPadding(QMargins(4, 2, 4, 2));

    // The display range, inset from the corners the way the hand-painted plot
    // drew it: max against the top edge, min against the bottom.
    m_maxLabel = new QCPItemText(this);
    m_maxLabel->setPositionAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_maxLabel->position->setType(QCPItemPosition::ptAxisRectRatio);
    m_maxLabel->position->setCoords(0.0, 0.0);
    m_maxLabel->setPadding(QMargins(6, 1, 0, 0));

    m_minLabel = new QCPItemText(this);
    m_minLabel->setPositionAlignment(Qt::AlignBottom | Qt::AlignLeft);
    m_minLabel->position->setType(QCPItemPosition::ptAxisRectRatio);
    m_minLabel->position->setCoords(0.0, 1.0);
    m_minLabel->setPadding(QMargins(6, 0, 0, 1));

    connect(this, &QCustomPlot::mouseMove, this, [this] (QMouseEvent *event) {
        m_hovering = axisRect()->rect().contains(event->pos());
        m_cursorKey = xAxis->pixelToCoord(event->pos().x());
        updateReadout();
        replot(QCustomPlot::rpQueuedReplot);
    });

    // Panning or zooming changes which samples the statistics cover.
    connect(xAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
            this, [this] { updateVerticalRange(); updateDrawQuality(); updateReadout(); });

    // Zooming the vertical axis by hand has to move the corner numbers too.
    connect(yAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
            this, [this] { updateRangeLabels(); });

    applyTheme();
}

void OpenMVChannelWaveform::leaveEvent(QEvent *event)
{
    QCustomPlot::leaveEvent(event);

    m_hovering = false;
    updateReadout();
    replot(QCustomPlot::rpQueuedReplot);
}

double OpenMVChannelWaveform::viewSpan() const
{
    // A couple of chunks wide, but held between bounds at both ends. How
    // much a publisher sends at a time describes its cadence, not how much
    // of its signal is worth looking at: ten samples a chunk would give a
    // window of dots, and sixteen hundred would cram a hundred and fifty
    // cycles into the pane. Neither number has anything to do with the
    // signal.
    double samples = qBound(WAVEFORM_VIEW_MIN_SAMPLES,
                            qMax(1, m_samplesPerChunk) * WAVEFORM_VIEW_CHUNKS,
                            WAVEFORM_VIEW_MAX_SAMPLES);

    return samples * (m_timeAxis ? m_period : 1.0);
}

void OpenMVChannelWaveform::applyTheme()
{
    bool dark = Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface);
    QColor text = Utils::creatorTheme()->color(Utils::Theme::TextColorNormal);

    // Grid as faint as the hand-drawn one it replaces; the axis line and ticks
    // a step stronger so the scale still reads against the traces.
    QColor grid = text;
    grid.setAlpha(dark ? 13 : 30);
    QColor line = text;
    line.setAlpha(dark ? 60 : 90);

    // Take the pane's own background rather than the theme colour directly, so
    // the plot always matches whatever viewApplyBackground() painted behind it.
    setBackground(QBrush(palette().color(QPalette::Window)));

    QFont small = font();
    small.setPointSizeF(qMax(6.0, small.pointSizeF() - 2.0));

    // Tick labels sit muted and inside the plot, so the trace still runs to
    // the pane edges instead of being framed by an axis gutter.
    QColor muted = text;
    muted.setAlpha(m_spectrum ? 255 : 120);

    for(QCPAxis *axis : {xAxis, yAxis})
    {
        axis->setBasePen(m_spectrum ? QPen(line) : QPen(Qt::NoPen));
        axis->setTickPen(QPen(line));
        axis->setSubTickPen(QPen(Qt::NoPen));
        axis->setTickLengthIn(m_spectrum ? 0 : 3);
        axis->setTickLengthOut(m_spectrum ? 4 : 0);
        axis->setTickLabelSide(m_spectrum ? QCPAxis::lsOutside : QCPAxis::lsInside);
        axis->setTickLabelColor(muted);
        axis->setTickLabelFont(small);
        axis->setLabelColor(text);
        axis->setLabelFont(small);
        axis->grid()->setPen(QPen(grid, 1));
        axis->grid()->setZeroLinePen(Qt::NoPen);
        // Few enough to read at a glance rather than a ruler's worth.
        axis->ticker()->setTickCount(3);
    }

    // The range already reads off the corner labels, so the vertical scale
    // only needs its grid lines.
    yAxis->setTickLabels(m_spectrum);

    if(m_spectrum)
    {
        axisRect()->setAutoMargins(QCP::msLeft | QCP::msBottom);
        axisRect()->setMargins(QMargins());
    }
    else
    {
        axisRect()->setAutoMargins(QCP::msNone);
        axisRect()->setMargins(QMargins());
    }

    // The legend floats over the traces, so it is kept small and washed into
    // the background rather than boxed like a chart legend on paper.
    QColor fill = palette().color(QPalette::Window);
    fill.setAlpha(200);
    legend->setBrush(QBrush(fill));
    legend->setBorderPen(QPen(grid));
    legend->setTextColor(text);
    legend->setFont(small);
    legend->setSelectedFont(small);
    legend->setIconSize(14, 8);
    legend->setRowSpacing(0);
    legend->setMargins(QMargins(4, 2, 4, 2));

    // Selection is only the click mechanism here; highlighting the entry
    // afterwards would read as state the plot does not actually have.
    legend->setSelectedTextColor(text);
    legend->setSelectedBorderPen(QPen(grid));
    legend->setSelectedIconBorderPen(Qt::NoPen);
    legend->setSelectedBrush(QBrush(fill));

    axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignRight);

    if(m_cursor)
    {
        m_cursor->setPen(QPen(line));
    }

    if(m_pinned)
    {
        // Solid where the live cursor is plain, so the fixed one reads as
        // placed rather than as following the pointer.
        QPen pen(text);
        pen.setWidthF(1.0);
        m_pinned->setPen(pen);
    }

    if(m_readout)
    {
        m_readout->setFont(small);
        m_readout->setColor(text);
        m_readout->setBrush(QBrush(fill));
        m_readout->setPen(QPen(grid));
    }

    for(QCPItemTracer *tracer : m_tracers)
    {
        tracer->setBrush(QBrush(palette().color(QPalette::Window)));
    }

    for(QCPItemText *label : {m_maxLabel, m_minLabel})
    {
        if(label)
        {
            label->setFont(small);
            label->setColor(text);
        }
    }
}

void OpenMVChannelWaveform::changeEvent(QEvent *event)
{
    QCustomPlot::changeEvent(event);

    // The pane themes its children by palette, so the plot only learns its
    // background once it has been parented into the view.
    if(event->type() == QEvent::PaletteChange)
    {
        applyTheme();
        replot(QCustomPlot::rpQueuedReplot);
    }
}

void OpenMVChannelWaveform::setSeriesVisible(int series, bool visible)
{
    if(visible)
    {
        m_hiddenSeries.remove(series);
    }
    else
    {
        m_hiddenSeries.insert(series);
    }

    // The entry stays in the legend either way -- greyed is what tells the
    // user the trace is hidden rather than absent.
    QCPPlottableLegendItem *item = legend->itemWithPlottable(graph(series));

    if(item)
    {
        QColor color = Utils::creatorTheme()->color(Utils::Theme::TextColorNormal);
        color.setAlpha(visible ? 255 : 90);
        item->setTextColor(color);
    }

    applyMode();
}

void OpenMVChannelWaveform::resetHistory(int series, double min, double max, double period,
                                         const QStringList &names)
{
    m_seriesCount = series;
    m_min = min;
    m_max = max;
    m_period = period;
    m_seriesNames = names;
    m_timeAxis = (period > 0.0);
    m_next = 0.0;
    m_end = 0.0;
    m_hasData = false;
    m_following = true;
    m_lastData.clear();

    clearGraphs();
    legend->clearItems();

    for(QCPItemTracer *tracer : m_tracers)
    {
        removeItem(tracer);
    }

    m_tracers.clear();

    m_hiddenSeries.clear();
    m_hasPinned = false; // its key belongs to the history being discarded
    m_stats.clear();

    for(int s = 0; s < series; s++)
    {
        m_stats.append(SeriesStats());
    }

    // Time-domain traces first, so graph(s) indexes a series in either domain.
    for(int s = 0; s < series; s++)
    {
        QCPGraph *g = addGraph();
        // Traces cycle through the shared plot palette (theme-adjusted).
        g->setPen(QPen(viewPlotColor(s % ViewPlotColorCount), 1.0));
        // Named by the sender when it says so. Failing that a lone trace takes
        // the record's own name, and overlaid ones are numbered -- a bare
        // colour is not enough to tell three axes apart.
        QString fallback = ((series == 1) && (!m_title.isEmpty()))
            ? m_title : Tr::tr("Series %1").arg(s + 1);
        g->setName((s < names.size()) ? names.at(s) : fallback);
        g->addToLegend();

        // Deliberately not given its graph yet: setGraph() positions the
        // tracer immediately, and the graph it would be handed here has just
        // been created and holds nothing. applyMode() attaches it once there
        // are samples to sit on, and it stays hidden until then.
        QCPItemTracer *tracer = new QCPItemTracer(this);
        tracer->setInterpolating(false);
        tracer->setStyle(QCPItemTracer::tsCircle);
        tracer->setSize(5);
        tracer->setPen(QPen(viewPlotColor(s % ViewPlotColorCount)));
        tracer->setVisible(false);
        m_tracers.append(tracer);
    }

    // Their spectra, kept off the legend: the same names already label them.
    for(int s = 0; s < series; s++)
    {
        QCPGraph *g = addGraph();
        g->setPen(QPen(viewPlotColor(s % ViewPlotColorCount), 1.0));
        g->setName(graph(s)->name());
        g->setVisible(false);
    }

    // A single trace is already identified by the row's own header.
    legend->setVisible(series > 1);

    yAxis->setRange(m_min, m_max);
    xAxis->setRange(0.0, viewSpan());
    applyMode();
    applyTheme();
}

// Spectrum sizing: enough bins to resolve something useful, capped so a
// 16 kHz series does not spend the poll interval in the transform.
static const int WAVEFORM_FFT_MIN = 64;
static const int WAVEFORM_FFT_MAX = 4096;
static const double WAVEFORM_FLOOR_DB = -120.0;

// In-place radix-2 FFT, n a power of two. Small enough to keep here rather
// than take a dependency for one transform.
static void waveformFft(QVector<double> &re, QVector<double> &im)
{
    const double pi = 3.14159265358979323846;
    int n = re.size();

    for(int i = 1, j = 0; i < n; i++)
    {
        int bit = n >> 1;

        for(; j & bit; bit >>= 1)
        {
            j ^= bit;
        }

        j ^= bit;

        if(i < j)
        {
            re.swapItemsAt(i, j);
            im.swapItemsAt(i, j);
        }
    }

    for(int len = 2; len <= n; len <<= 1)
    {
        int half = len / 2;
        double angle = (-2.0 * pi) / len;

        for(int i = 0; i < n; i += len)
        {
            for(int k = 0; k < half; k++)
            {
                double wr = qCos(angle * k);
                double wi = qSin(angle * k);
                double ur = re.at(i + k);
                double ui = im.at(i + k);
                double vr = (re.at(i + k + half) * wr) - (im.at(i + k + half) * wi);
                double vi = (re.at(i + k + half) * wi) + (im.at(i + k + half) * wr);

                re[i + k] = ur + vr;
                im[i + k] = ui + vi;
                re[i + k + half] = ur - vr;
                im[i + k + half] = ui - vi;
            }
        }
    }
}

// The window a bin is multiplied by before the transform. Hann is the
// sensible default; a rectangular window is only right when the capture is
// already an exact number of cycles.
static double windowGain(int window, int i, int n)
{
    const double pi = 3.14159265358979323846;
    double phase = (2.0 * pi * i) / (n - 1);

    switch(window)
    {
        case 1: return 0.54 - (0.46 * qCos(phase));                            // Hamming
        case 2: return 0.42 - (0.5 * qCos(phase)) + (0.08 * qCos(2.0 * phase)); // Blackman
        case 3: return 1.0;                                                     // rectangular
        default: return 0.5 * (1.0 - qCos(phase));                              // Hann
    }
}

// Each window loses amplitude, and the reference a bin is measured against
// has to lose the same amount or a full scale tone would not read 0 dB.
static double windowCoherentGain(int window)
{
    switch(window)
    {
        case 1: return 0.54;
        case 2: return 0.42;
        case 3: return 1.0;
        default: return 0.5;
    }
}

void OpenMVChannelWaveform::computeSpectrum()
{

    m_fftSize = 0;
    m_nyquist = m_timeAxis ? (0.5 / m_period) : 0.0;

    for(int s = 0; s < m_seriesCount; s++)
    {
        QCPGraph *source = graph(s);
        QCPGraph *target = graph(m_seriesCount + s);
        int available = int(source->data()->size());
        int n = 1;

        while((n * 2) <= qMin(available, WAVEFORM_FFT_MAX))
        {
            n *= 2;
        }

        if(n < WAVEFORM_FFT_MIN)
        {
            target->data()->clear();
            continue;
        }

        m_fftSize = n;

        QVector<double> re(n);
        QVector<double> im(n, 0.0);
        QCPGraphDataContainer::const_iterator it = source->data()->constEnd() - n;
        double mean = 0.0;

        for(int i = 0; i < n; i++, ++it)
        {
            re[i] = it->value;
            mean += it->value;
        }

        mean /= n;

        for(int i = 0; i < n; i++)
        {
            // Windowed, with the offset removed first: a mic centred at
            // 32768 would otherwise bury every tone under its own DC bin.
            re[i] = (re.at(i) - mean) * windowGain(m_spectrumWindow, i, n);
        }

        waveformFft(re, im);

        // Full scale is a sine spanning the record's display range, and the
        // window's coherent gain is part of the same reference, so a full
        // scale tone reads 0 dB whichever window is in use.
        double reference = ((m_max - m_min) * 0.5) * (n / 2.0)
            * windowCoherentGain(m_spectrumWindow);
        // Bin zero is what is left of the offset that was subtracted out, so
        // it carries no signal -- and on a log axis it is the widest decade on
        // screen. Start at the first bin that means something.
        int bins = n / 2;
        QVector<double> keys(bins - 1);
        QVector<double> values(bins - 1);

        for(int i = 1; i < bins; i++)
        {
            double magnitude = qSqrt((re.at(i) * re.at(i)) + (im.at(i) * im.at(i)));
            keys[i - 1] = m_timeAxis ? ((i * 2.0 * m_nyquist) / n) : i;
            values[i - 1] = (magnitude > 0.0)
                ? qMax(WAVEFORM_FLOOR_DB, 20.0 * std::log10(magnitude / reference))
                : WAVEFORM_FLOOR_DB;
        }

        // Averaging settles a noisy floor; peak hold keeps the loudest bin
        // seen, which is how a burst that has already passed stays readable.
        if(m_spectrumAveraging || m_spectrumPeakHold)
        {
            if(m_spectrumHeld.size() != m_seriesCount)
            {
                m_spectrumHeld.resize(m_seriesCount);
            }

            QVector<double> &held = m_spectrumHeld[s];

            if(held.size() != values.size())
            {
                held = values;
            }
            else
            {
                for(int i = 0; i < values.size(); i++)
                {
                    held[i] = m_spectrumPeakHold
                        ? qMax(held.at(i), values.at(i))
                        : (held.at(i) + (0.3 * (values.at(i) - held.at(i))));
                }
            }

            values = held;
        }

        target->setData(keys, values, true);
    }
}

void OpenMVChannelWaveform::applyMode()
{
    for(int s = 0; s < m_seriesCount; s++)
    {
        bool shown = !m_hiddenSeries.contains(s);

        graph(s)->setVisible(shown && (!m_spectrum));
        graph(m_seriesCount + s)->setVisible(shown && m_spectrum);

        // Handing a tracer a graph repositions it there and then, and a
        // tracer over a graph with no samples warns every time. Only retarget
        // when it changes and there is something to sit on; updateReadout()
        // keeps it hidden until then.
        QCPGraph *target = m_spectrum ? graph(m_seriesCount + s) : graph(s);

        if((m_tracers.at(s)->graph() != target) && (!target->data()->isEmpty()))
        {
            m_tracers.at(s)->setGraph(target);
        }
    }

    if(m_spectrum)
    {
        // The units are named in the row header instead: an axis label costs a
        // whole text row of margin out of a plot this short.
        xAxis->setLabel(QString());
        yAxis->setLabel(QString());

        // A log axis spreads the low end out, which is where anything
        // interesting sits in a signal with harmonics. Zero has no place on
        // one, so the sweep starts at the first bin instead.
        bool logarithmic = m_logFrequency && m_timeAxis && (m_fftSize > 0);

        if(logarithmic != (xAxis->scaleType() == QCPAxis::stLogarithmic))
        {
            xAxis->setScaleType(logarithmic ? QCPAxis::stLogarithmic : QCPAxis::stLinear);
            xAxis->setTicker(logarithmic
                ? QSharedPointer<QCPAxisTicker>(new QCPAxisTickerLog)
                : QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));
            xAxis->ticker()->setTickCount(3);
        }

        if(m_following)
        {
            double top = m_timeAxis ? m_nyquist : qMax(1, m_fftSize / 2);
            // The window smears the removed offset across the first couple
            // of bins, which on a log axis is a long ragged run-in to the
            // noise floor. Open past it.
            xAxis->setRange(logarithmic ? ((6.0 * m_nyquist) / m_fftSize) : 0.0, top);
            yAxis->setRange(WAVEFORM_FLOOR_DB, 0.0);
        }
    }
    else
    {
        xAxis->setLabel(QString());
        yAxis->setLabel(QString());
        xAxis->setScaleType(QCPAxis::stLinear);

        if(m_following)
        {
            yAxis->setRange(m_min, m_max);

            if(m_hasData)
            {
                // Until there is a window's worth of history, show what there
                // is rather than scrolling a mostly empty pane: a 100Hz record
                // takes a couple of seconds to fill one, and watching a trace
                // creep in from the right reads as a fault rather than as a
                // graph that has not been running long.
                double low = m_end - viewSpan();

                if(!graph(0)->data()->isEmpty())
                {
                    low = qMax(low, graph(0)->data()->constBegin()->key);
                }

                xAxis->setRange((low < m_end) ? low : (m_end - viewSpan()), m_end);
            }
        }
    }

    updateRangeLabels();
    m_minLabel->setVisible(m_hasData && (!m_spectrum));
}

void OpenMVChannelWaveform::updateDrawQuality()
{
    QCPRange range = xAxis->range();
    qint64 visible = 0;

    for(int s = 0; s < m_seriesCount; s++)
    {
        QCPGraph *shown = m_spectrum ? graph(m_seriesCount + s) : graph(s);
        visible += (shown->data()->findEnd(range.upper) - shown->data()->findBegin(range.lower));
    }

    // Adaptive sampling draws a min/max envelope per pixel column and the fast
    // polyline routine cuts corners; both read as a rougher line. A couple of
    // chunks across the pane is a few hundred points, where a plain
    // antialiased polyline is both prettier and quick enough -- so only reach
    // for them well past the point where the difference stops being visible.
    bool fast = visible > qint64(width() * 4);

    setPlottingHint(QCP::phFastPolylines, fast);

    for(int i = 0; i < graphCount(); i++)
    {
        graph(i)->setAdaptiveSampling(fast);
    }
}

void OpenMVChannelWaveform::setSpectrum(bool enabled)
{
    if(m_spectrum == enabled)
    {
        return;
    }

    m_spectrum = enabled;
    m_spectrumHeld.clear();
    // Whatever range the other domain was parked at means nothing here, and
    // the smoothed statistics now measure a different quantity.
    m_following = true;

    for(SeriesStats &stat : m_stats)
    {
        stat.valid = false;
    }

    computeSpectrum();
    applyMode();
    applyTheme(); // the axis chrome differs between the two domains
    updateDrawQuality();
    updateReadout();
    replot(QCustomPlot::rpQueuedReplot);
}

void OpenMVChannelWaveform::setSettingsKey(const QString &key)
{
    m_settingsKey = key;

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    m_autoScale = settings->value(plotAutoScaleKey(key), true).toBool();
    m_history = settings->value(plotHistoryKey(key), WAVEFORM_HISTORY_MULTIPLE).toDouble();

    if(!m_autoScale)
    {
        yAxis->setRange(m_min, m_max);
    }

    updateVerticalRange();
    updateRangeLabels();
    replot(QCustomPlot::rpQueuedReplot);
}

void OpenMVChannelWaveform::armTrigger(bool armed)
{
    // Discard any captured window and pick the live stream back up first --
    // setPaused() disarms as part of going live, so arming has to come after
    // it rather than before.
    setPaused(false);

    m_triggerArmed = armed;
    m_triggerHasPrevious = false;

    // Arming without a level having been set puts it in the middle of the
    // record's range, which is where a crossing is most likely to be useful.
    if(armed && (!m_triggerHasLevel))
    {
        m_triggerLevel = (m_min + m_max) * 0.5;
        m_triggerHasLevel = true;
    }

    updateTriggerItems();
    replot(QCustomPlot::rpQueuedReplot);
}

void OpenMVChannelWaveform::updateTriggerItems()
{
    // Dashed, in the trace palette's warning colour: these mark a setting
    // rather than any measured value. Faint until armed, so a level can be
    // placed and seen before the graph is waiting on it.
    QPen pen(viewPlotColor(ViewPlotOrange));
    pen.setStyle(Qt::DashLine);
    QColor color = pen.color();
    color.setAlpha(m_triggerArmed ? 255 : 110);
    pen.setColor(color);

    m_triggerLine->setPen(pen);
    m_triggerMark->setPen(pen);

    // Shown from the moment a level exists: placing one and seeing nothing
    // would be aiming at something invisible.
    m_triggerLine->setVisible(m_triggerHasLevel && (!m_spectrum));
    m_triggerLine->point1->setCoords(0.0, m_triggerLevel);
    m_triggerLine->point2->setCoords(1.0, m_triggerLevel);

    m_triggerMark->setVisible(m_triggered && (!m_spectrum));
}

void OpenMVChannelWaveform::checkTrigger(const QVector<double> &keys,
                                         const QVector<double> &values)
{
    for(int i = 0; i < values.size(); i++)
    {
        double value = values.at(i);

        // A crossing needs the sample before it, which for the first sample
        // of a chunk is the last sample of the one before.
        if(m_triggerHasPrevious)
        {
            bool crossed = m_triggerRising
                ? ((m_triggerPrevious < m_triggerLevel) && (value >= m_triggerLevel))
                : ((m_triggerPrevious > m_triggerLevel) && (value <= m_triggerLevel));

            if(crossed)
            {
                double key = keys.at(i);

                m_triggered = true;
                m_triggerMark->point1->setCoords(key, 0.0);
                m_triggerMark->point2->setCoords(key, 1.0);

                // A quarter in, so the samples leading up to the event are on
                // screen as well as the ones after it.
                double span = viewSpan();
                m_following = false;
                xAxis->setRange(key - (span * 0.25), key + (span * 0.75));

                // Freezing here keeps exactly the window that was captured;
                // anything appended after would scroll the event away.
                setPaused(true);
                return;
            }
        }

        m_triggerPrevious = value;
        m_triggerHasPrevious = true;
    }
}

void OpenMVChannelWaveform::setPaused(bool paused)
{
    if(m_paused == paused)
    {
        return;
    }

    m_paused = paused;

    // The samples that arrived while frozen were never plotted, so butting
    // the next chunk onto the old trace would draw a continuous stream that
    // never happened. Resuming starts a new one instead.
    if(!m_paused)
    {
        // Releasing a captured graph puts it back on the live stream, which
        // means disarming: leaving it armed would let the very next crossing
        // freeze it again, and it would never come back.
        m_triggered = false;
        m_triggerArmed = false;
        m_triggerHasPrevious = false;
        resetHistory(m_seriesCount, m_min, m_max, m_period, m_seriesNames);
        updateTriggerItems();
    }

    emit pausedChanged(m_paused);
    replot(QCustomPlot::rpQueuedReplot);
}

void OpenMVChannelWaveform::updateVerticalRange()
{
    if(m_spectrum || (!m_autoScale) || (!m_hasData))
    {
        return;
    }

    QCPRange range = xAxis->range();
    double low = 0.0;
    double high = 0.0;
    bool any = false;

    for(int s = 0; s < m_seriesCount; s++)
    {
        if(!graph(s)->visible())
        {
            continue;
        }

        QCPGraphDataContainer::const_iterator begin = graph(s)->data()->findBegin(range.lower);
        QCPGraphDataContainer::const_iterator end = graph(s)->data()->findEnd(range.upper);

        for(QCPGraphDataContainer::const_iterator it = begin; it != end; ++it)
        {
            if((!any) || (it->value < low))
            {
                low = it->value;
            }

            if((!any) || (it->value > high))
            {
                high = it->value;
            }

            any = true;
        }
    }

    if(!any)
    {
        return;
    }

    // A flat trace has no span to pad, so it gets an arbitrary one rather
    // than a zero-height axis.
    double pad = (high - low) * 0.05;
    yAxis->setRange(low - ((pad > 0.0) ? pad : 0.5), high + ((pad > 0.0) ? pad : 0.5));
}

void OpenMVChannelWaveform::updateRangeLabels()
{
    m_maxLabel->setText(QString::number(yAxis->range().upper, 'g', 6));
    m_minLabel->setText(QString::number(yAxis->range().lower, 'g', 6));
}

void OpenMVChannelWaveform::setShowStats(bool enabled)
{
    if(m_showStats == enabled)
    {
        return;
    }

    m_showStats = enabled;

    updateReadout();
    replot(QCustomPlot::rpQueuedReplot);
}

void OpenMVChannelWaveform::saveImage()
{
    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    const Utils::Key pathKey = plotPathKey(m_settingsKey);
    QString suggestion = m_settingsKey.isEmpty() ? QString() : settings->value(pathKey).toString();

    if(suggestion.isEmpty())
    {
        suggestion = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(recordFileName(m_title.isEmpty() ? QStringLiteral("plot") : m_title)
                + QStringLiteral(".png"));
    }

    QString path = QFileDialog::getSaveFileName(this, Tr::tr("Save Plot Image"),
        suggestion, Tr::tr("PNG Image (*.png);;PDF Document (*.pdf)"));

    if(path.isEmpty())
    {
        return;
    }

    if(!m_settingsKey.isEmpty())
    {
        settings->setValue(pathKey, path);
    }

    // A plot this short is only a few hundred pixels tall on screen, which is
    // useless in a report or a bug thread, so the raster export is rendered at
    // three times the widget's size and stamped with a matching resolution.
    // (A PDF is already resolution independent.)
    bool ok = path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)
        ? savePdf(path)
        : savePng(path, 0, 0, 3.0, -1, 288, QCP::ruDotsPerInch);

    if(!ok)
    {
        QMessageBox::critical(this, Tr::tr("Save Plot Image"),
                              Tr::tr("Failed to write \"%1\"!").arg(path));
    }
}

void OpenMVChannelWaveform::showContextMenu(const QPoint &pos)
{
    // Saving is the only thing left worth a menu: the view modes are buttons
    // and following the live end again is a double click.
    QMenu menu;

    QAction *scale = menu.addAction(Tr::tr("Auto Scale"));
    scale->setCheckable(true);
    scale->setChecked(m_autoScale);
    scale->setEnabled(!m_spectrum);

    QAction *pin = menu.addAction(m_hasPinned ? Tr::tr("Clear Marker") : Tr::tr("Drop Marker Here"));
    menu.addSeparator();

    QMenu *trigger = menu.addMenu(Tr::tr("Trigger"));
    trigger->setEnabled(!m_spectrum);

    QAction *arm = trigger->addAction(m_triggered ? Tr::tr("Re-arm") : Tr::tr("Arm"));
    arm->setCheckable(true);
    arm->setChecked(m_triggerArmed && (!m_triggered));

    QAction *level = trigger->addAction(Tr::tr("Set Level Here"));
    trigger->addSeparator();

    QAction *rising = trigger->addAction(Tr::tr("Rising Edge"));
    rising->setCheckable(true);
    rising->setChecked(m_triggerRising);

    QAction *falling = trigger->addAction(Tr::tr("Falling Edge"));
    falling->setCheckable(true);
    falling->setChecked(!m_triggerRising);

    // Which trace is watched only matters when there is more than one.
    QList<QAction *> sources;

    if(m_seriesCount > 1)
    {
        trigger->addSeparator();

        for(int s = 0; s < m_seriesCount; s++)
        {
            QAction *source = trigger->addAction(graph(s)->name());
            source->setCheckable(true);
            source->setChecked(s == m_triggerSeries);
            sources.append(source);
        }
    }

    QMenu *historyMenu = menu.addMenu(Tr::tr("History"));
    QList<QAction *> histories;

    for(double choice : WAVEFORM_HISTORY_CHOICES)
    {
        // Labelled by how much data that actually keeps, since a count of
        // screens means nothing without knowing how wide one is.
        double span = viewSpan() * choice;
        QAction *history = historyMenu->addAction(m_timeAxis
            ? ((span >= 60.0) ? Tr::tr("%1 min").arg(readoutNumber(span / 60.0, 2))
                              : Tr::tr("%1 s").arg(readoutNumber(span, 2)))
            : Tr::tr("%1 samples").arg(qRound(span)));
        history->setCheckable(true);
        history->setChecked(qFuzzyCompare(choice, m_history));
        histories.append(history);
    }

    QMenu *spectrumMenu = menu.addMenu(Tr::tr("Spectrum"));
    spectrumMenu->setEnabled(m_spectrum);

    QAction *averaging = spectrumMenu->addAction(Tr::tr("Averaging"));
    averaging->setCheckable(true);
    averaging->setChecked(m_spectrumAveraging);

    QAction *peakHold = spectrumMenu->addAction(Tr::tr("Peak Hold"));
    peakHold->setCheckable(true);
    peakHold->setChecked(m_spectrumPeakHold);

    QAction *logFrequency = spectrumMenu->addAction(Tr::tr("Log Frequency"));
    logFrequency->setCheckable(true);
    logFrequency->setChecked(m_logFrequency);

    spectrumMenu->addSeparator();
    QList<QAction *> windows;
    const QStringList windowNames = {Tr::tr("Hann"), Tr::tr("Hamming"),
                                     Tr::tr("Blackman"), Tr::tr("Rectangular")};

    for(int w = 0; w < windowNames.size(); w++)
    {
        QAction *window = spectrumMenu->addAction(windowNames.at(w));
        window->setCheckable(true);
        window->setChecked(w == m_spectrumWindow);
        windows.append(window);
    }

    menu.addSeparator();
    QAction *save = menu.addAction(Tr::tr("Save Image..."));

    QAction *chosen = menu.exec(mapToGlobal(pos));

    if(chosen == save)
    {
        saveImage();
    }
    else if((chosen == averaging) || (chosen == peakHold) || (chosen == logFrequency)
         || windows.contains(chosen))
    {
        if(chosen == averaging)
        {
            m_spectrumAveraging = averaging->isChecked();
            m_spectrumPeakHold = m_spectrumPeakHold && (!m_spectrumAveraging);
        }
        else if(chosen == peakHold)
        {
            // Holding the loudest bin and averaging towards the latest one
            // are opposite answers to the same question.
            m_spectrumPeakHold = peakHold->isChecked();
            m_spectrumAveraging = m_spectrumAveraging && (!m_spectrumPeakHold);
        }
        else if(chosen == logFrequency)
        {
            m_logFrequency = logFrequency->isChecked();
            m_following = true;
        }
        else
        {
            m_spectrumWindow = int(windows.indexOf(chosen));
        }

        m_spectrumHeld.clear();
        computeSpectrum();
        applyMode();
        updateReadout();
        replot(QCustomPlot::rpQueuedReplot);
    }
    else if(histories.contains(chosen))
    {
        m_history = WAVEFORM_HISTORY_CHOICES[histories.indexOf(chosen)];

        if(!m_settingsKey.isEmpty())
        {
            ExtensionSystem::PluginManager::settings()
                ->setValue(plotHistoryKey(m_settingsKey), m_history);
        }
    }
    else if(chosen == pin)
    {
        m_hasPinned = !m_hasPinned;
        m_pinnedKey = xAxis->pixelToCoord(pos.x());
        updateReadout();
        replot(QCustomPlot::rpQueuedReplot);
    }
    else if(chosen == arm)
    {
        armTrigger(arm->isChecked());
    }
    else if(chosen == level)
    {
        // Set from where the menu was opened, which is how a scope's level is
        // placed: point at the value you want to catch.
        m_triggerLevel = yAxis->pixelToCoord(pos.y());
        m_triggerHasLevel = true;
        updateTriggerItems();
        replot(QCustomPlot::rpQueuedReplot);
    }
    else if((chosen == rising) || (chosen == falling))
    {
        m_triggerRising = (chosen == rising);
        m_triggerHasPrevious = false;
    }
    else if(sources.contains(chosen))
    {
        m_triggerSeries = int(sources.indexOf(chosen));
        m_triggerHasPrevious = false;
    }
    else if(chosen == scale)
    {
        m_autoScale = scale->isChecked();

        if(!m_settingsKey.isEmpty())
        {
            ExtensionSystem::PluginManager::settings()
                ->setValue(plotAutoScaleKey(m_settingsKey), m_autoScale);
        }

        // Turning it off hands the axis back to the range the record
        // declares, which is what the graph opened with.
        if(!m_autoScale)
        {
            yAxis->setRange(m_min, m_max);
        }

        updateVerticalRange();
        updateRangeLabels();
        replot(QCustomPlot::rpQueuedReplot);
    }
}

// The sample a graph holds at a key, or none when the key is off its ends.
static bool valueAtKey(QCPGraph *graph, double key, double *value)
{
    QCPGraphDataContainer::const_iterator it = graph->data()->findBegin(key);

    if(it == graph->data()->constEnd())
    {
        return false;
    }

    *value = it->value;
    return true;
}

// Enough digits to tell neighbouring samples apart without turning the
// readout into a wall of decimals.
static QString readoutNumber(double value, int digits)
{
    return QString::number(value, 'g', digits);
}

// Statistics settle towards their true value over about a second rather than
// jumping with every poll. The cursor readout is deliberately not smoothed:
// that one has to be the sample actually under the pointer.
static void smoothStat(double &value, double sample, bool valid)
{
    value = valid ? (value + (0.2 * (sample - value))) : sample;
}

void OpenMVChannelWaveform::updateReadout()
{
    if(!m_hasData)
    {
        m_cursor->setVisible(false);
        m_pinned->setVisible(false);
        m_readout->setVisible(false);
        m_maxLabel->setVisible(false);
        m_minLabel->setVisible(false);

        for(QCPItemTracer *tracer : m_tracers)
        {
            tracer->setVisible(false);
        }

        return;
    }

    QStringList lines;
    QCPRange range = xAxis->range();

    if(m_hovering)
    {
        // Values where the cursor crosses each trace.
        if(m_spectrum)
        {
            lines.append(m_timeAxis
                ? Tr::tr("f %1 Hz").arg(readoutNumber(m_cursorKey))
                : Tr::tr("bin %1").arg(qRound(m_cursorKey)));
        }
        else
        {
            lines.append(m_timeAxis
                ? Tr::tr("t %1 s").arg(readoutNumber(m_cursorKey))
                : Tr::tr("n %1").arg(qRound(m_cursorKey)));
        }

        if(m_hasPinned)
        {
            double delta = m_cursorKey - m_pinnedKey;

            // The reciprocal is what the measurement is usually for: mark two
            // peaks and read the rate between them.
            lines.append((m_timeAxis && (!m_spectrum) && (!qFuzzyIsNull(delta)))
                ? Tr::tr("dt %1 s  %2 Hz").arg(readoutNumber(delta),
                    readoutNumber(1.0 / qAbs(delta)))
                : Tr::tr("dx %1").arg(readoutNumber(delta)));
        }

        for(int s = 0; s < m_seriesCount; s++)
        {
            QCPGraph *shown = m_spectrum ? graph(m_seriesCount + s) : graph(s);

            // A tracer rides on its graph's samples, so it can only be shown
            // once there are some: a cleared history (or a spectrum with too
            // few samples to transform) leaves it with nothing to sit on, and
            // it complains once per replot. The pointer can still be over the
            // plot at that moment -- a modal dialog takes the mouse without
            // ever sending a leave event.
            bool populated = !shown->data()->isEmpty();

            m_tracers.at(s)->setGraphKey(m_cursorKey);
            m_tracers.at(s)->setVisible(shown->visible() && populated);

            if((!shown->visible()) || (!populated))
            {
                continue;
            }

            QCPGraphDataContainer::const_iterator it = shown->data()->findBegin(m_cursorKey);

            if(it != shown->data()->constEnd())
            {
                double pinnedValue = 0.0;

                // With a cursor pinned the interesting number is the step
                // between the two, not the value at one of them.
                if(m_hasPinned && valueAtKey(shown, m_pinnedKey, &pinnedValue))
                {
                    lines.append(m_spectrum
                        ? Tr::tr("%1 %2 dB  d %3").arg(shown->name(),
                            readoutNumber(it->value), readoutNumber(it->value - pinnedValue))
                        : Tr::tr("%1 %2  d %3").arg(shown->name(),
                            readoutNumber(it->value), readoutNumber(it->value - pinnedValue)));
                }
                else
                {
                    lines.append(m_spectrum
                        ? Tr::tr("%1 %2 dB").arg(shown->name(), readoutNumber(it->value))
                        : QStringLiteral("%1 %2").arg(shown->name(), readoutNumber(it->value)));
                }
            }
        }
    }
    else
    {
        for(QCPItemTracer *tracer : m_tracers)
        {
            tracer->setVisible(false);
        }
    }

    if(m_showStats)
    {
        // What the visible window holds: for a spectrum the strongest bin in
        // it, and for a waveform the span each trace covers plus its RMS,
        // which is what tells a quiet microphone from a loud one.
        for(int s = 0; s < m_seriesCount; s++)
        {
            QCPGraph *shown = m_spectrum ? graph(m_seriesCount + s) : graph(s);

            if(!shown->visible())
            {
                continue;
            }

            QCPGraphDataContainer::const_iterator begin = shown->data()->findBegin(range.lower);
            QCPGraphDataContainer::const_iterator end = shown->data()->findEnd(range.upper);

            if(m_spectrum)
            {
                double peakKey = 0.0;
                double peak = WAVEFORM_FLOOR_DB;

                for(QCPGraphDataContainer::const_iterator it = begin; it != end; ++it)
                {
                    if(it->value > peak)
                    {
                        peak = it->value;
                        peakKey = it->key;
                    }
                }

                if(peak > WAVEFORM_FLOOR_DB)
                {
                    SeriesStats &stat = m_stats[s];
                    smoothStat(stat.low, peakKey, stat.valid);
                    smoothStat(stat.high, peak, stat.valid);
                    stat.valid = true;

                    lines.append(m_timeAxis
                        ? Tr::tr("%1 peak %2 Hz %3 dB").arg(shown->name(),
                            readoutNumber(stat.low, 3), readoutNumber(stat.high, 3))
                        : Tr::tr("%1 peak bin %2 %3 dB").arg(shown->name(),
                            QString::number(qRound(stat.low)), readoutNumber(stat.high, 3)));
                }

                continue;
            }

            double low = 0.0;
            double high = 0.0;
            double square = 0.0;
            qint64 count = 0;

            for(QCPGraphDataContainer::const_iterator it = begin; it != end; ++it)
            {
                if(!count || (it->value < low))
                {
                    low = it->value;
                }

                if(!count || (it->value > high))
                {
                    high = it->value;
                }

                square += (it->value * it->value);
                count++;
            }

            if(count)
            {
                SeriesStats &stat = m_stats[s];
                smoothStat(stat.low, low, stat.valid);
                smoothStat(stat.high, high, stat.valid);
                smoothStat(stat.rms, qSqrt(square / count), stat.valid);
                stat.valid = true;

                lines.append(Tr::tr("%1 %2..%3 rms %4").arg(shown->name(),
                    readoutNumber(stat.low, 3), readoutNumber(stat.high, 3),
                    readoutNumber(stat.rms, 3)));
            }
        }
    }

    m_pinned->setVisible(m_hasPinned);
    m_pinned->point1->setCoords(m_pinnedKey, 0.0);
    m_pinned->point2->setCoords(m_pinnedKey, 1.0);

    m_cursor->setVisible(m_hovering);
    m_cursor->point1->setCoords(m_cursorKey, 0.0);
    m_cursor->point2->setCoords(m_cursorKey, 1.0);

    // Held off the corner by a few pixels so the box does not sit against the
    // plot edges. The anchor is a ratio, so the inset is worked back out of
    // the axis rect's current size.
    m_readout->position->setCoords(6.0 / qMax(1, axisRect()->width()),
                                   4.0 / qMax(1, axisRect()->height()));

    m_readout->setText(lines.join(QLatin1Char('\n')));
    m_readout->setVisible(!lines.isEmpty());

    // The readout takes the top-left corner while it is up.
    m_maxLabel->setVisible(m_hasData && (!m_spectrum) && lines.isEmpty());
}

void OpenMVChannelWaveform::setData(const QByteArray &data, const QString &typecode,
                                    int samples, int series, double min, double max,
                                    double period, double t, const QStringList &names)
{
    if(m_paused)
    {
        return;
    }

    int count = qMax(1, series);
    int per = qMax(0, samples);
    double top = (max != min) ? max : (min + 1.0);

    // The poll re-reads the newest chunk faster than scripts publish it;
    // appending a repeat would draw the same samples twice. (An absent
    // timestamp is NaN, which never compares equal to itself.)
    bool sameTime = (qIsNaN(t) && qIsNaN(m_lastT)) || (t == m_lastT);

    if(m_hasData && sameTime && (data == m_lastData))
    {
        return;
    }

    if((count != m_seriesCount) || (min != m_min) || (top != m_max)
    || (period != m_period) || (names != m_seriesNames))
    {
        resetHistory(count, min, top, period, names);
    }

    m_samplesPerChunk = per;

    if(per < 1)
    {
        return;
    }

    QVector<float> decoded = decodeSamples(data, typecode, per * count);

    if(decoded.size() < (per * count))
    {
        return;
    }

    // Each chunk is butted against the previous one rather than placed at its
    // device timestamp. A script's publish cadence rarely matches the rate it
    // declares -- 32 ms of samples arriving every 100 ms would plot as holes,
    // and an oversized 1.28 s buffer arriving just as often as overlapping
    // runs. What the plot shows is therefore the sample stream itself. The
    // timestamps still do their real work in the recorder, which reports the
    // gap between chunks in its own column.
    double dt = m_timeAxis ? m_period : 1.0;
    double base = m_next;

    QVector<double> keys(per);

    for(int i = 0; i < per; i++)
    {
        keys[i] = base + (i * dt);
    }

    double end = base + ((per - 1) * dt);

    // Trimming to the usual depth while the user is looking through history
    // would delete the very samples they panned back to, so the window only
    // widens; the multiple still bounds how far memory can grow.
    double keep = end - (viewSpan() * m_history
        * (m_following ? 1.0 : WAVEFORM_HELD_HISTORY_MULTIPLE));

    for(int s = 0; s < count; s++)
    {
        QVector<double> values(per);

        for(int i = 0; i < per; i++)
        {
            // Interleaved: sample i of series s is at (i * seriesCount) + s.
            values[i] = double(decoded.at((i * count) + s));
        }

        graph(s)->addData(keys, values, true);
        graph(s)->data()->removeBefore(keep);

        if(m_triggerArmed && (!m_triggered) && (s == qBound(0, m_triggerSeries, count - 1)))
        {
            checkTrigger(keys, values);
        }
    }

    m_lastData = data;
    m_lastT = t;
    m_next = base + (per * dt);
    m_end = end;
    m_hasData = true;

    if(m_spectrum)
    {
        computeSpectrum();
    }

    applyMode();
    updateTriggerItems();
    updateVerticalRange();
    updateDrawQuality();
    updateReadout();
    replot(QCustomPlot::rpQueuedReplot);
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
    m_pendingWrites.clear();
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

// The plots' mouse controls are otherwise undiscoverable: nothing on a graph
// looks clickable, and the spectrum and export live behind a right click that
// nobody would think to try.
static QWidget *plotHintRow()
{
    QWidget *row = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(row);
    layout->setContentsMargins(4, 5, 4, 4);
    layout->setSpacing(0);

    QLabel *label = viewNameLabel(Tr::tr(
        "Graphs: drag to pan, scroll to zoom, double click to follow live, "
        "right click to save."));
    label->setWordWrap(true);
    layout->addWidget(label);

    return row;
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

        // The plot's view modes sit beside Record rather than in its context
        // menu: they are flipped while reading the data, and a checked button
        // says which mode the graph is in without opening anything.
        if(record.waveform)
        {
            OpenMVChannelWaveform *plot = record.waveform;

            QPushButton *spectrum = new QPushButton(Tr::tr("Spectrum"));
            spectrum->setCheckable(true);
            spectrum->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
            recordBarLayout->addWidget(spectrum);

            connect(spectrum, &QPushButton::toggled, plot, [plot] (bool checked) {
                plot->setSpectrum(checked);
            });

            QPushButton *pause = new QPushButton(Tr::tr("Pause"));
            pause->setCheckable(true);
            pause->setToolTip(Tr::tr("Freeze the graph. Recording is not affected."));
            pause->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
            recordBarLayout->addWidget(pause);

            connect(pause, &QPushButton::toggled, plot, [plot] (bool checked) {
                plot->setPaused(checked);
            });

            connect(plot, &OpenMVChannelWaveform::pausedChanged, pause, [pause] (bool paused) {
                QSignalBlocker blocker(pause);
                pause->setChecked(paused);
            });

            QPushButton *stats = new QPushButton(Tr::tr("Stats"));
            stats->setCheckable(true);
            stats->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
            recordBarLayout->addWidget(stats);

            connect(stats, &QPushButton::toggled, plot, [plot] (bool checked) {
                plot->setShowStats(checked);
            });
        }

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

            // Header (name, size, range) above the colormapped image, all inset
            // from the pane edge, with the separator below running full-bleed
            // like viewRow()'s.
            QWidget *content = new QWidget;
            QVBoxLayout *contentLayout = new QVBoxLayout(content);
            contentLayout->setContentsMargins(0, 3, 0, 5);   // extra room under the record bar
            contentLayout->setSpacing(4);

            record.depthHeader = viewNameLabel(QString());
            record.depthHeader->setContentsMargins(6, 0, 6, 0);
            contentLayout->addWidget(record.depthHeader);

            // The image runs full-bleed; the header and record bar inset.
            record.depth = new OpenMVChannelDepth;
            contentLayout->addWidget(record.depth);

            addRecordBar(record, contentLayout, i);

            QWidget *row = new QWidget;
            QVBoxLayout *rowLayout = new QVBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(0);
            rowLayout->addWidget(content);
            rowLayout->addWidget(viewHairline());

            record.row = row;
            m_contentLayout->addWidget(row);
        }
        else if(record.wtype == QStringLiteral("waveform"))
        {
            record.sectionIndex = currentSection;

            // Header (name, geometry, rate) above the traces, all inset from the
            // pane edge, with the separator below running full-bleed like
            // viewRow()'s.
            QWidget *content = new QWidget;
            QVBoxLayout *contentLayout = new QVBoxLayout(content);
            contentLayout->setContentsMargins(0, 3, 0, 5);   // extra room under the record bar
            contentLayout->setSpacing(4);

            record.waveformHeader = viewNameLabel(QString());
            record.waveformHeader->setContentsMargins(6, 0, 6, 0);
            contentLayout->addWidget(record.waveformHeader);

            // The plot runs full-bleed; its min/max labels carry the same inset
            // as the header so the text still lines up.
            record.waveform = new OpenMVChannelWaveform;
            record.waveform->setTitle(record.name);
            record.waveform->setSettingsKey(QStringLiteral("%1/%2")
                .arg(record.channelName, record.name));
            contentLayout->addWidget(record.waveform);

            addRecordBar(record, contentLayout, i);

            QWidget *row = new QWidget;
            QVBoxLayout *rowLayout = new QVBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(0);
            rowLayout->addWidget(content);
            rowLayout->addWidget(viewHairline());

            record.row = row;
            m_contentLayout->addWidget(row);
        }
        else if(record.wtype == QStringLiteral("toggle"))
        {
            record.toggle = new HoverGlowCheckBox;
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

            // Inset the slider to the same margin as the header's name label so
            // the two line up, rather than running full-bleed to the pane edges.
            QWidget *sliderRow = new QWidget;
            QHBoxLayout *sliderLayout = new QHBoxLayout(sliderRow);
            // Bottom margin matches the 3px viewRow() leaves under its content,
            // so the slider does not sit right on the separator.
            sliderLayout->setContentsMargins(4, 0, 4, 3);
            sliderLayout->setSpacing(0);

            record.slider = new HoverGlowSlider(Qt::Horizontal);
            record.slider->setEnabled(writable);
            sliderLayout->addWidget(record.slider);
            rowLayout->addWidget(sliderRow);

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

                        // Keyboard arrows, wheel, and trough clicks change the
                        // value without a handle drag (no sliderPressed/Released),
                        // so write those immediately. During a drag isSliderDown()
                        // is true and the write is deferred to sliderReleased to
                        // avoid a write per intermediate step. Programmatic
                        // setValue() in the render path is wrapped in a
                        // QSignalBlocker, so this can't echo a read back.
                        if(!r.slider->isSliderDown())
                        {
                            stageWrite(r.channelName, r.name, numberValue(
                                r.sliderMin + (position * r.sliderStep)));
                        }
                    }
                });
            }

            // viewRow() ends every other row with a separator; this row is built
            // by hand, so add one to match.
            rowLayout->addWidget(viewHairline());

            record.row = row;
            m_contentLayout->addWidget(row);
        }
        else if(record.wtype == QStringLiteral("spinbox"))
        {
            // Precise stepped entry (issue #157: +/- buttons in fixed
            // increments) - a spin box over the slider's min/max/step keys.
            // Named like the Settings Editor's element; the step decides the
            // decimals, so one type covers spinbox and doublespinbox.
            record.spinbox = new HoverGlowSpinBox;
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
                QRadioButton *button = new HoverGlowRadioButton(options.at(j).toString());
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

            // Same structure viewRow() gives every other row: the text insets
            // from the pane edge, the separator below runs full-bleed.
            QWidget *content = new QWidget;
            QVBoxLayout *contentLayout = new QVBoxLayout(content);
            contentLayout->setContentsMargins(6, 3, 6, 3);
            contentLayout->setSpacing(0);
            contentLayout->addWidget(record.value);

            QWidget *row = new QWidget;
            QVBoxLayout *rowLayout = new QVBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(0);
            rowLayout->addWidget(content);
            rowLayout->addWidget(viewHairline());

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

    // Explained once, under everything, rather than per graph.
    for(const Record &record : records)
    {
        if(record.waveform)
        {
            m_contentLayout->addWidget(plotHintRow());
            break;
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
        if(built.recorder)
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

            double t = chunkTime(built.rec);
            QByteArray data = built.rec.value(qint64(CBOR_KEY_VD)).toByteArray();

            if(built.recorder)
            {
                if(built.recorder->append(data, typecode, samples, series, t, period))
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
        }

        // A write is in flight on this channel: don't render pre-write data.
        if(m_skipRenders.value(built.channelName, 0) > 0)
        {
            continue;
        }

        QString controlId = QStringLiteral("%1/%2").arg(built.channelName, built.name);

        // Hold a just-written control at the value the user set until the
        // device echoes it back, so a pre-write read still in flight can't snap
        // it back. Releases when the read matches the written value, or when the
        // deadline passes (the device never reported it, e.g. it clamped it).
        if(m_pendingWrites.contains(controlId))
        {
            if(recordValue(built.rec) == m_pendingWrites.value(controlId).first)
            {
                m_pendingWrites.remove(controlId);
            }
            else if(QDateTime::currentMSecsSinceEpoch() < m_pendingWrites.value(controlId).second)
            {
                continue;
            }
            else
            {
                m_pendingWrites.remove(controlId);
            }
        }

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
            // In the spectrum the header carries the units, so the plot itself
            // needs no axis labels eating its height.
            if(built.waveform->spectrum())
            {
                built.waveformHeader->setText(rate.isEmpty()
                    ? Tr::tr("%1 spectrum (dB per bin)").arg(built.name)
                    : Tr::tr("%1 spectrum (dB per Hz)").arg(built.name));
            }
            else
            {
                built.waveformHeader->setText(rate.isEmpty()
                    ? Tr::tr("%1 (%2)").arg(built.name, geometry)
                    : Tr::tr("%1 (%2 @ %3)").arg(built.name, geometry, rate));
            }
            // A waveform has no select options, so that key carries the
            // series names when the sender labels its traces.
            QStringList names;

            for(const QCborValue &option : built.rec.value(qint64(CBOR_KEY_W_OPTS)).toArray())
            {
                names.append(option.toString());
            }

            built.waveform->setData(built.rec.value(qint64(CBOR_KEY_VD)).toByteArray(),
                                    typecode, samples, series, min, max,
                                    period, chunkTime(built.rec), names);
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
        m_pendingWrites.clear();

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
        anyIndividual |= (record.recorder && (!record.groupStarted));
        anyGroup |= record.groupStarted;
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

void OpenMVChannelsView::setDevice(const QString &type, const QString &id)
{
    m_deviceType = type;
    m_deviceId = id;
}

OpenMVChannelRecorder::Info OpenMVChannelsView::infoFor(const Record &record) const
{
    OpenMVChannelRecorder::Info info;
    info.name = record.name;
    info.deviceType = m_deviceType;
    info.deviceName = m_deviceId;
    info.unit = record.rec.value(qint64(CBOR_KEY_U)).toString();
    info.min = record.rec.value(qint64(CBOR_KEY_MIN)).toDouble();
    info.max = record.rec.value(qint64(CBOR_KEY_MAX)).toDouble();

    if(record.depth)
    {
        // A depth frame is one row of w*h float32 values with no sample rate.
        int width = int(record.rec.value(qint64(CBOR_KEY_W)).toInteger());
        int height = int(record.rec.value(qint64(CBOR_KEY_H)).toInteger());
        info.columns = width * height;
        info.typecode = QStringLiteral("f");
        info.gap = false;
        return info;
    }

    info.columns = qMax(1, int(record.rec.value(qint64(CBOR_KEY_H)).toInteger(1)));
    info.typecode = record.rec.value(qint64(CBOR_KEY_VS)).toString();
    info.period = record.rec.value(qint64(CBOR_KEY_UT)).toDouble();

    // A waveform has no select options, so that key carries the series names.
    for(const QCborValue &option : record.rec.value(qint64(CBOR_KEY_W_OPTS)).toArray())
    {
        info.series.append(option.toString());
    }

    return info;
}

bool OpenMVChannelsView::startRecorder(Record &record, const QString &path,
                                       OpenMVChannelRecorder::Format format, bool group)
{
    OpenMVChannelRecorder::Info info = infoFor(record);

    if(info.columns <= 0)
    {
        // A depth record that hasn't published a frame yet has no dimensions,
        // so there is no column layout to record into.
        record.recordStatus->setText(Tr::tr("No data to record yet"));
        return false;
    }

    QString reason = OpenMVChannelRecorder::unsupportedReason(format, info);

    if(!reason.isEmpty())
    {
        QMessageBox::critical(this, Tr::tr("Record Channel"), reason);
        return false;
    }

    OpenMVChannelRecorder *recorder = new OpenMVChannelRecorder(path, format, info, record.row);

    if(!recorder->ok())
    {
        QString error = recorder->errorString();
        delete recorder;
        QMessageBox::critical(this, Tr::tr("Record Channel"), error);
        return false;
    }

    record.recorder = recorder;
    record.groupStarted = group;
    record.recordButton->setText(Tr::tr("Stop"));
    record.recordStatus->setText(Tr::tr("Recording..."));
    return true;
}

void OpenMVChannelsView::stopRecorder(Record &record)
{
    if(!record.recorder)
    {
        return;
    }

    record.recordStatus->setText(Tr::tr("Saved %1").arg(record.recorder->status()));
    delete record.recorder;
    record.recorder = Q_NULLPTR;
    record.groupStarted = false;
    record.recordButton->setText(Tr::tr("Record"));
}

void OpenMVChannelsView::toggleRecording(int index)
{
    if((index < 0) || (index >= m_records.size()))
    {
        return;
    }

    if(m_records.at(index).recorder)
    {
        stopRecorder(m_records[index]);
        updateRecordButtonStates();
        return;
    }

    QString channelName = m_records.at(index).channelName;
    QString recordName = m_records.at(index).name;

    // Decide what this record can be written as before offering anything, so
    // a format is never picked and then refused.
    OpenMVChannelRecorder::Info info = infoFor(m_records.at(index));

    if(info.columns <= 0)
    {
        m_records[index].recordStatus->setText(Tr::tr("No data to record yet"));
        return;
    }

    QList<OpenMVChannelRecorder::Format> formats =
        OpenMVChannelRecorder::supportedFormats(info);

    if(formats.isEmpty())
    {
        QMessageBox::critical(this, Tr::tr("Record Channel"),
            OpenMVChannelRecorder::unsupportedReason(OpenMVChannelRecorder::Csv, info));
        return;
    }

    // Each graph remembers its own last save path ("OpenMV" matches the
    // plugin's SETTINGS_GROUP).
    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    const Utils::Key pathKey = Utils::keyFromString(
        QStringLiteral("OpenMV/LastChannelRecordPath/%1/%2").arg(channelName, recordName));

    QString suggestion = settings->value(pathKey).toString();

    if(suggestion.isEmpty())
    {
        // Numbered, so the name already has the shape Edge Impulse reads a
        // label and index out of; the dialog's name field is where a real
        // label gets typed.
        suggestion = numberedRecordPath(
            QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)),
            recordFileName(recordName), OpenMVChannelRecorder::suffixFor(formats.first()));
    }

    QString filter;
    QString path = QFileDialog::getSaveFileName(this, Tr::tr("Record \"%1\" To").arg(recordName),
        suggestion, OpenMVChannelRecorder::filterString(formats), &filter);

    if(path.isEmpty())
    {
        return;
    }

    settings->setValue(pathKey, path);
    OpenMVChannelRecorder::Format format = OpenMVChannelRecorder::formatForFilter(filter, path);

    // Re-locate the record by name - data kept flowing while the file
    // dialog was up, and a schema change rebuilds m_records.
    for(int i = 0; i < m_records.size(); i++)
    {
        Record &record = m_records[i];

        if((record.channelName == channelName) && (record.name == recordName) && record.recordButton)
        {
            if(startRecorder(record, path, format, false))
            {
                updateRecordButtonStates();
            }

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

    QString channelName = m_sections.at(index).channelName;
    int running = 0;

    for(const Record &record : m_records)
    {
        running += (record.groupStarted && (record.channelName == channelName)) ? 1 : 0;
    }

    if(running)
    {
        for(int i = 0; i < m_records.size(); i++)
        {
            if(m_records.at(i).groupStarted && (m_records.at(i).channelName == channelName))
            {
                stopRecorder(m_records[i]);
            }
        }

        Section &section = m_sections[index];
        section.status->setText(Tr::tr("Saved %Ln file(s)", Q_NULLPTR, running));
        section.button->setText(Tr::tr("Record All"));
        updateRecordButtonStates();
        return;
    }

    // Every graph writes its own file here, so what is being chosen is a
    // folder to put them in -- and a folder dialog has nowhere to offer a file
    // type, so the format is asked for separately. Both are remembered per
    // channel ("OpenMV" matches the plugin's SETTINGS_GROUP).
    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    const Utils::Key folderKey = Utils::keyFromString(
        QStringLiteral("OpenMV/LastChannelRecordFolder/%1").arg(channelName));
    const Utils::Key formatKey = Utils::keyFromString(
        QStringLiteral("OpenMV/LastChannelRecordFormat/%1").arg(channelName));

    QString suggestion = settings->value(folderKey).toString();

    if(suggestion.isEmpty())
    {
        suggestion = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }

    QString folder = QFileDialog::getExistingDirectory(this,
        Tr::tr("Record \"%1\" To Folder").arg(channelName), suggestion);

    if(folder.isEmpty())
    {
        return;
    }

    // Only formats every graph in the section can be written as: one choice
    // covers them all, so a format that suits the mic but not the depth map
    // has no business being on the list.
    QList<OpenMVChannelRecorder::Format> formats;
    bool first = true;

    for(const Record &record : m_records)
    {
        if((record.channelName != channelName) || (!(record.waveform || record.depth)))
        {
            continue;
        }

        QList<OpenMVChannelRecorder::Format> supported =
            OpenMVChannelRecorder::supportedFormats(infoFor(record));

        if(first)
        {
            formats = supported;
            first = false;
        }
        else
        {
            for(int f = formats.size() - 1; f >= 0; f--)
            {
                if(!supported.contains(formats.at(f)))
                {
                    formats.removeAt(f);
                }
            }
        }
    }

    if(formats.isEmpty())
    {
        QMessageBox::critical(this, Tr::tr("Record Channel"),
            Tr::tr("No format can hold every graph in \"%1\". "
                   "Record them one at a time.").arg(channelName));
        return;
    }

    // Both graphs of one take want the same label, and neither has a name
    // field of its own here, so it is asked for once alongside the format.
    QStringList names = OpenMVChannelRecorder::formatNames(formats);
    const Utils::Key labelKey = Utils::keyFromString(
        QStringLiteral("OpenMV/LastChannelRecordLabel/%1").arg(channelName));

    QDialog dialog(this);
    dialog.setWindowTitle(Tr::tr("Record \"%1\"").arg(channelName));

    QComboBox *formatBox = new QComboBox;
    formatBox->addItems(names);
    formatBox->setCurrentIndex(qBound(0, settings->value(formatKey, 0).toInt(), names.size() - 1));

    QLineEdit *labelEdit = new QLineEdit(settings->value(labelKey).toString());
    labelEdit->setPlaceholderText(Tr::tr("optional, names the files for Edge Impulse"));

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QFormLayout *form = new QFormLayout(&dialog);
    form->addRow(Tr::tr("Format:"), formatBox);
    form->addRow(Tr::tr("Label:"), labelEdit);
    form->addRow(buttons);

    if(dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    OpenMVChannelRecorder::Format format = formats.at(formatBox->currentIndex());
    QString label = recordFileName(labelEdit->text().trimmed());

    settings->setValue(folderKey, folder);
    settings->setValue(formatKey, formatBox->currentIndex());
    settings->setValue(labelKey, label);

    QString suffix = OpenMVChannelRecorder::suffixFor(format);
    int started = 0;

    for(int s = 0; s < m_sections.size(); s++)
    {
        Section &section = m_sections[s];

        if(section.channelName != channelName)
        {
            continue;
        }

        for(int i = 0; i < m_records.size(); i++)
        {
            Record &record = m_records[i];

            if((record.channelName != channelName) || (record.sectionIndex != s)
            || (!(record.waveform || record.depth)) || (!record.recordButton))
            {
                continue;
            }

            // Each file is named after the graph it holds, which is the only
            // thing that distinguishes them, and numbered so a second capture
            // into the same folder does not land on the first.
            QString file = numberedRecordPath(QDir(folder), label.isEmpty()
                ? recordFileName(record.name)
                : (recordFileName(record.name) + QLatin1Char('_') + label), suffix);

            if(startRecorder(record, file, format, true))
            {
                started++;
            }
        }

        if(started)
        {
            section.button->setText(Tr::tr("Stop"));
            section.status->setText(Tr::tr("Recording %Ln file(s)...", Q_NULLPTR, started));
        }

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

    // Then hold this specific control at the written value until the device
    // echoes it back (or 3s passes), so a slow write round-trip can't let a
    // later pre-write read snap it back after the coarse skip count drains.
    const QString controlId = QStringLiteral("%1/%2").arg(channelName, recordName);
    m_pendingWrites[controlId] = qMakePair(value, QDateTime::currentMSecsSinceEpoch() + 3000);

    emit writeChannel(channelName, encodeWrite(recordName, value));
}

} // namespace Internal
} // namespace OpenMV
