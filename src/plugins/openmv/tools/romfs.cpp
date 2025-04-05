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

#define ROMFS_RECORD_KIND_UNUSED        (0)
#define ROMFS_RECORD_KIND_PADDING       (1)
#define ROMFS_RECORD_KIND_DATA_VERBATIM (2)
#define ROMFS_RECORD_KIND_DATA_POINTER  (3)
#define ROMFS_RECORD_KIND_DIRECTORY     (4)
#define ROMFS_RECORD_KIND_FILE          (5)

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
                }
                else if ((datakind == ROMFS_RECORD_KIND_DATA_POINTER) && (payloadlen >= 8))
                {
                    quint64 dp_size = decodeuint(&fs);
                    quint64 dp_data = decodeuint(&fs);
                    QFile file(dir.filePath(name));

                    if (file.open(QIODevice::WriteOnly)) {
                        file.write(reinterpret_cast<const char *>(filesystem + dp_data), dp_size);
                    }
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
        return false;
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

QByteArray VfsRomWriter::pad(const QByteArray &data)
{
    if (data.size() % m_alignment) {
        return data + QByteArray(m_alignment - (data.size() % m_alignment), '\x00');
    }

    return data;
}

QByteArray VfsRomWriter::pack(const QByteArray &header, const QByteArray &payload, bool padpayload)
{
    QByteArray payloadSizeBytes = encodeuint(payload.size());
    qsizetype size = header.size() + payloadSizeBytes.size();

    if (size % m_alignment) {
        payloadSizeBytes.prepend(QByteArray(m_alignment - (size % m_alignment), '\x80'));
    }

    return header + payloadSizeBytes + (padpayload ? pad(payload) : payload);
}

void VfsRomWriter::extend(const QByteArray &data)
{
    m_dirstack.last().second.append(data);
}

VfsRomWriter::VfsRomWriter(qsizetype alignment)
{
    QPair<QString, QByteArray> pair;
    pair.first = QString();
    pair.second = QByteArray();
    m_dirstack = QList<QPair<QString, QByteArray> >() << pair;
    m_alignment = alignment;
}

QByteArray VfsRomWriter::finalize()
{
    char header[3] = {};
    header[0] = ROMFS_HEADER_BYTE0;
    header[1] = ROMFS_HEADER_BYTE1;
    header[2] = ROMFS_HEADER_BYTE2;
    QPair<QString, QByteArray> pair = m_dirstack.takeLast();
    QByteArray encodedkind = QByteArray(header);
    QByteArray encodedlen = encodeuint(pair.second.size());
    qsizetype size = encodedkind.size() + encodedlen.size();

    if (size % m_alignment) {
        encodedlen.prepend(QByteArray(m_alignment - (size % m_alignment), '\x80'));
    }

    return encodedkind + encodedlen + pair.second;
}

void VfsRomWriter::opendir(const QString &dirname)
{
    QPair<QString, QByteArray> pair;
    pair.first = dirname;
    pair.second = QByteArray();
    m_dirstack.append(pair);
}

void VfsRomWriter::closedir()
{
    QPair<QString, QByteArray> pair = m_dirstack.takeLast();
    QByteArray bdirname = toAscii(pair.first);
    QByteArray bdirnamesize = encodeuint(bdirname.size());
    qsizetype size = bdirnamesize.size() + bdirname.size();

    if (size % m_alignment) {
        bdirnamesize.prepend(QByteArray(m_alignment - (size % m_alignment), '\x80'));
    }

    extend(pack(encodeuint(ROMFS_RECORD_KIND_DIRECTORY), bdirnamesize + bdirname + pair.second));
}

void VfsRomWriter::mkfile(const QString &filename, const QByteArray &filedata)
{
    QByteArray bfilename = toAscii(filename);
    QByteArray payload = pack(encodeuint(bfilename.size()) + bfilename + encodeuint(ROMFS_RECORD_KIND_DATA_VERBATIM), filedata, true);
    extend(pack(encodeuint(ROMFS_RECORD_KIND_FILE), payload));
}

void VfsRomWriter::mkfile(const QString &filename, quint64 filesize, quint64 fileoffset)
{
    QByteArray bfilename = toAscii(filename);
    QByteArray subpayload = encodeuint(filesize) + encodeuint(fileoffset);
    QByteArray payload = pack(encodeuint(bfilename.size()) + bfilename + encodeuint(ROMFS_RECORD_KIND_DATA_POINTER), subpayload, true);
    extend(pack(encodeuint(ROMFS_RECORD_KIND_FILE), payload));
}

} // namespace Internal
} // namespace OpenMV
