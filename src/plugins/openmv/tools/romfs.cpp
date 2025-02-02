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

#define ROMFS_HEADER                    "\xd2\xcd\x31"

#define ROMFS_RECORD_KIND_UNUSED        0
#define ROMFS_RECORD_KIND_PADDING       1
#define ROMFS_RECORD_KIND_DATA_VERBATIM 2
#define ROMFS_RECORD_KIND_DATA_POINTER  3
#define ROMFS_RECORD_KIND_DIRECTORY     4
#define ROMFS_RECORD_KIND_FILE          5

static QByteArray toAscii(const QString &str)
{
    QByteArray asciiArray = str.toLatin1();

    for (char &ch : asciiArray) {
        if ((ch < 0x20) || (ch > 0x7E)) {
            ch = '?';
        }
    }

    return asciiArray;
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

QByteArray VfsRomWriter::pack(quint64 kind, const QByteArray &payload)
{
    return encodeuint(kind) + encodeuint(payload.size()) + payload;
}

quint64 VfsRomWriter::extend(const QByteArray &data)
{
    dirstack.last().second.append(data);
    return dirstack.last().second.size();
}

VfsRomWriter::VfsRomWriter()
{
    QPair<QString, QByteArray> pair;
    pair.first = QString();
    pair.second = QByteArray();
    dirstack = QList<QPair<QString, QByteArray> >() << pair;
    offset = 0;
}

QByteArray VfsRomWriter::finalize()
{
    QPair<QString, QByteArray> pair = dirstack.takeFirst();
    QByteArray encodedkind = QByteArray(ROMFS_HEADER);
    QByteArray encodedlen = encodeuint(pair.second.size());

    if (((encodedkind.size() + encodedlen.size() + pair.second.size()) % 2) == 1) {
        encodedlen.prepend('\x80');
    }

    return encodedkind + encodedlen + pair.second;
}

void VfsRomWriter::opendir(const QString &dirname)
{
    QPair<QString, QByteArray> pair;
    pair.first = dirname;
    pair.second = QByteArray();
    dirstack.append(pair);
}

void VfsRomWriter::closedir()
{
    QPair<QString, QByteArray> pair = dirstack.takeFirst();
    QByteArray bdirname = toAscii(pair.first);
    QByteArray dirdata = encodeuint(bdirname.size()) + bdirname + pair.second;
    offset += extend(pack(ROMFS_RECORD_KIND_DIRECTORY, dirdata));
}

void VfsRomWriter::mkfile(const QString &filename, const QByteArray &filedata, quint64 alignment)
{
    QByteArray bfilename = toAscii(filename);
    QByteArray payload = encodeuint(bfilename.size()) + bfilename + pack(ROMFS_RECORD_KIND_DATA_VERBATIM, filedata);
    offset += extend(pack(ROMFS_RECORD_KIND_FILE, payload));
}

void VfsRomWriter::mkfile(const QString &filename, quint64 filedata[2])
{
    QByteArray bfilename = toAscii(filename);
    QByteArray subpayload = encodeuint(filedata[0]) + encodeuint(filedata[1]);
    QByteArray payload = encodeuint(bfilename.size()) + bfilename + pack(ROMFS_RECORD_KIND_DATA_POINTER, subpayload);
    offset += extend(pack(ROMFS_RECORD_KIND_FILE, payload));
}

} // namespace Internal
} // namespace OpenMV

