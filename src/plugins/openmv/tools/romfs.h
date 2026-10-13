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

#ifndef ROMFS_H
#define ROMFS_H

#include <QtCore>

namespace OpenMV {
namespace Internal {

QByteArray toAscii(const QString &str);

class VfsRomReader
{

public:
    VfsRomReader(const QByteArray &data);
    bool unpack(const QString &path);

private:
    quint64 decodeuint(const uint8_t **ptr);
    quint64 extractrecord(const uint8_t **fs, const uint8_t **fsnext);
    void unpackrecursive(const QString &path, const uint8_t *fs, const uint8_t *fstop);

    QByteArray m_data;
    const uint8_t *filesystem;
    const uint8_t *filesystem_end;
    const uint8_t *filesystem_end_2;
};

class VfsRomWriter
{

public:
    VfsRomWriter(const QJsonArray &alignmentRules);
    QByteArray finalize();
    void opendir(const QString &dirname);
    void closedir();
    void mkfile(const QString &filename, const QByteArray &filedata);

private:
    QByteArray encodeuint(quint64 value);
    QByteArray encoderecord(quint64 kind, const QByteArray &payload, int alignment = 0, int offset = 0, int padding = 0);
    QByteArray encodefile(const QString &filename, const QByteArray &payload, int alignment, int offset);

    QList<QPair<QString, QByteArray> > m_dirstack;
    QList<int> m_offsetstack;
    QJsonArray m_alignmentRules;
    int m_maxAlignment;
};

} // namespace Internal
} // namespace OpenMV

#endif // ROMFS_H
