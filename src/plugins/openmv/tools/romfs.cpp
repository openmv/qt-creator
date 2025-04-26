/* Copyright (C) 2023-2024 OpenMV, LLC.
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

#include "romfs.h"

namespace OpenMV {
namespace Internal {

#define ROMFS_SIZE_MIN                  (4)

#define ROMFS_HEADER_BYTE0              (0x80 | 'R')
#define ROMFS_HEADER_BYTE1              (0x80 | 'M')
#define ROMFS_HEADER_BYTE2              (0x00 | '1')

#define ROMSF_HEADER_KIND               (((ROMFS_HEADER_BYTE0 & 0x7F) << 14) | \
                                        ((ROMFS_HEADER_BYTE1 & 0x7F) << 7) | \
                                        (ROMFS_HEADER_BYTE2 & 0x7F))

#define ROMFS_RECORD_KIND_UNUSED        (0)
#define ROMFS_RECORD_KIND_PADDING       (1)
#define ROMFS_RECORD_KIND_DATA_VERBATIM (2)
#define ROMFS_RECORD_KIND_DATA_POINTER  (3)
#define ROMFS_RECORD_KIND_DIRECTORY     (4)
#define ROMFS_RECORD_KIND_FILE          (5)

#define ROMFS_MIN_ALIGNMENT             (4)
#define ROMFS_FILEREC_ALIGNMENT         (8)
#define ROMFS_HEADER_ALIGNMENT          (16)

QByteArray toAscii(const QString &str)
{
    QByteArray asciiArray = str.toLatin1();

    for (char &ch : asciiArray) {
        if ((ch < 0x20) || (ch > 0x7E)) {
            ch = '?';
        }
    }

    return asciiArray;
}

quint64 VfsRomReader::decodeuint(const uint8_t **ptr)
{
    quint64 unum = 0;
    uint8_t val;
    const uint8_t *p = *ptr;

    do
    {
        if (p >= filesystem_end_2) {
            return 0;
        }

        val = *p++;
        unum = (unum << 7) | (val & 0x7f);
    }
    while ((val & 0x80) != 0);

    *ptr = p;

    return unum;
}

quint64 VfsRomReader::extractrecord(const uint8_t **fs, const uint8_t **fsnext)
{
    quint64 recordkind = decodeuint(fs);
    quint64 recordlen = decodeuint(fs);
    *fsnext = *fs + recordlen;
    return recordkind;
}

void VfsRomReader::unpackrecursive(const QString &path, const uint8_t *fs, const uint8_t *fstop)
{
    QDir dir(path);

    while (fs < fstop)
    {
        if (fs >= filesystem_end_2) {
            return;
        }

        const uint8_t *fsnext;
        quint64 recordkind = extractrecord(&fs, &fsnext);

        if ((recordkind == ROMFS_RECORD_KIND_DIRECTORY) || (recordkind == ROMFS_RECORD_KIND_FILE))
        {
            quint64 namelen = decodeuint(&fs);
            QString name = QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(fs), namelen));
            fs += namelen;

            if (recordkind == ROMFS_RECORD_KIND_DIRECTORY)
            {
                dir.mkpath(name);

                // Required to make sure creation timestamps are different on unpacked files.
                QThread::msleep(1);

                unpackrecursive(path + QDir::separator() + name, fs, fsnext);
            }
            else
            {
                const uint8_t *fstemp;
                quint64 datakind = extractrecord(&fs, &fstemp);
                quint64 payloadlen = fstemp - fs;

                if ((datakind == ROMFS_RECORD_KIND_DATA_VERBATIM) && (payloadlen > 0))
                {
                    QByteArray filedata = QByteArray(reinterpret_cast<const char *>(fs), payloadlen);
                    QFile file(dir.filePath(name));

                    if (file.open(QIODevice::WriteOnly)) {
                        file.write(filedata);
                        file.close();
                    }

                    // Required to make sure creation timestamps are different on unpacked files.
                    QThread::msleep(1);
                }
                else if ((datakind == ROMFS_RECORD_KIND_DATA_POINTER) && (payloadlen >= 8))
                {
                    quint64 dp_size = decodeuint(&fs);
                    quint64 dp_data = decodeuint(&fs);
                    QFile file(dir.filePath(name));

                    if (file.open(QIODevice::WriteOnly)) {
                        file.write(reinterpret_cast<const char *>(filesystem + dp_data), dp_size);
                        file.close();
                    }

                    // Required to make sure creation timestamps are different on unpacked files.
                    QThread::msleep(1);
                }
            }
        }

        fs = fsnext;
    }
}

VfsRomReader::VfsRomReader(const QByteArray &data)
{
    m_data = data;
    filesystem_end = filesystem = reinterpret_cast<const uint8_t *>(m_data.constData());
    filesystem_end_2 = filesystem_end + m_data.size();

    if (m_data.size() < ROMFS_SIZE_MIN) {
        return;
    }

    if (filesystem[0] != ROMFS_HEADER_BYTE0 || filesystem[1] != ROMFS_HEADER_BYTE1 || filesystem[2] != ROMFS_HEADER_BYTE2) {
        return;
    }

    extractrecord(&filesystem, &filesystem_end);
}

bool VfsRomReader::unpack(const QString &path)
{
    if (filesystem == filesystem_end) {
        return true;
    }

    unpackrecursive(path, filesystem, filesystem_end);
    return true;
}

QByteArray VfsRomWriter::encodeuint(quint64 value)
{
    QByteArray encoded;
    encoded.append(value & 0x7f);
    value >>= 7;

    while (value)
    {
        encoded.prepend(0x80 | (value & 0x7f));
        value >>= 7;
    }

    return encoded;
}

QByteArray VfsRomWriter::encoderecord(quint64 kind, const QByteArray &payload, int alignment, int offset, int padding)
{
    if (alignment)
    {
        // Calculate what the offset will be after kind and payload size.
        offset += QByteArray(encodeuint(kind) + encodeuint(payload.size())).size();
        padding = ((offset + (alignment - 1)) & (~(alignment - 1))) - offset;
    }

    QByteArray kindb = kind ? encodeuint(kind) : QByteArray();
    return kindb + QByteArray(padding, '\x80') + encodeuint(payload.size()) + payload;
}

QByteArray VfsRomWriter::encodefile(const QString &filename, const QByteArray &payload, int alignment, int offset)
{
    QByteArray bfilename = toAscii(filename);
    QByteArray nameRecord = encoderecord(0, bfilename);

    // Calculate what the offset will be after filekind and nameRecord.
    offset += 1 + (ROMFS_FILEREC_ALIGNMENT - 1) + nameRecord.size();

    QByteArray bpayload = encoderecord(ROMFS_RECORD_KIND_DATA_VERBATIM, payload, alignment, offset);

    return encoderecord(ROMFS_RECORD_KIND_FILE, nameRecord + bpayload, ROMFS_FILEREC_ALIGNMENT);
}

VfsRomWriter::VfsRomWriter(const QJsonArray &alignmentRules)
{
    QPair<QString, QByteArray> pair;
    pair.first = QString();
    pair.second = QByteArray();
    m_dirstack = QList<QPair<QString, QByteArray> >() << pair;
    m_offsetstack = QList<int>() << ROMFS_HEADER_ALIGNMENT;
    m_alignmentRules = alignmentRules;
    m_maxAlignment = ROMFS_MIN_ALIGNMENT;

    for (const QJsonValue &rule : m_alignmentRules)
    {
        m_maxAlignment = qMax(m_maxAlignment, rule.toObject().value(QStringLiteral("alignment")).toInt());
    }
}

QByteArray VfsRomWriter::finalize()
{
    return encoderecord(ROMSF_HEADER_KIND, m_dirstack.takeLast().second, ROMFS_HEADER_ALIGNMENT);
}

void VfsRomWriter::opendir(const QString &dirname)
{
    QPair<QString, QByteArray> pair;
    pair.first = dirname;
    pair.second = QByteArray();
    m_dirstack.append(pair);
    m_offsetstack.append(0);
}

void VfsRomWriter::closedir()
{
    QPair<QString, QByteArray> pair = m_dirstack.takeLast();
    m_offsetstack.takeLast();

    QByteArray bdirname = toAscii(pair.first);
    QByteArray nameRecord = encoderecord(0, bdirname);

    QByteArray record = encoderecord(ROMFS_RECORD_KIND_DIRECTORY, nameRecord + pair.second, m_maxAlignment, m_offsetstack.last());
    m_offsetstack.last() += record.size();
    m_dirstack.last().second.append(record);
}

void VfsRomWriter::mkfile(const QString &filename, const QByteArray &filedata)
{
    QString completeSuffix = QFileInfo(filename).completeSuffix().toLower();
    int minAlignment = ROMFS_MIN_ALIGNMENT;

    for (const QJsonValue &rule : m_alignmentRules)
    {
        QJsonObject obj = rule.toObject();

        if (completeSuffix == obj.value(QStringLiteral("extension")).toString().toLower())
        {
            minAlignment = obj.value(QStringLiteral("alignment")).toInt();
            break;
        }
    }

    QByteArray record = encodefile(filename, filedata, minAlignment, m_offsetstack.last());
    m_offsetstack.last() += record.size();
    m_dirstack.last().second.append(record);
}

} // namespace Internal
} // namespace OpenMV
