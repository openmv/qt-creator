/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/
#include "differ.h"
#include "texteditor/fontsettings.h"
    const int lineCount = lines.count();
    const bool leftLineEqual = leftLines.count()
    const bool rightLineEqual = rightLines.count()
    if (line < newLines.count()) {
    const QStringList newLines = text.split(QLatin1Char('\n'));
    for (int line = 0; line < newLines.count(); ++line) {
        const int startPos = line > 0
                ? -1
                : lines->isEmpty() ? 0 : lines->last().text.count();
        const int endPos = line < newLines.count() - 1
                ? -1
                : lines->isEmpty() ? 0 : lines->last().text.count();
                                const QList<Diff> &rightDiffList)
    while (i <= leftDiffList.count() && j <= rightDiffList.count()) {
        const Diff leftDiff = i < leftDiffList.count()
                ? leftDiffList.at(i)
                : Diff(Diff::Equal);
        const Diff rightDiff = j < rightDiffList.count()
                ? rightDiffList.at(j)
                : Diff(Diff::Equal);
            if (j == rightDiffList.count() && lastLineEqual && leftDiff.text.startsWith(QLatin1Char('\n')))
            if (j == rightDiffList.count())
            if (i == leftDiffList.count() && lastLineEqual && rightDiff.text.startsWith(QLatin1Char('\n')))
            if (i == leftDiffList.count())
            const QStringList newLeftLines = leftDiff.text.split(QLatin1Char('\n'));
            const QStringList newRightLines = rightDiff.text.split(QLatin1Char('\n'));
            if (i < leftDiffList.count() || j < rightDiffList.count() || (leftLines.count() && rightLines.count())) {
                while (line < qMax(newLeftLines.count(), newRightLines.count())) {
                    const int commonLineCount = qMin(newLeftLines.count(),
                                                     newRightLines.count());
                                if (i == leftDiffList.count()
                                        && j == rightDiffList.count())
                    if ((line < commonLineCount - 1) // before the last common line in equality
                            || (line == commonLineCount - 1 // or the last common line in equality
                                && i == leftDiffList.count() // and it's the last iteration
                                && j == rightDiffList.count())) {
    for (int i = leftData.count(); i < rightData.count(); i++)
    for (int i = rightData.count(); i < leftData.count(); i++)
    const int visualLineCount = leftData.count();
    while (i < originalData.rows.count()) {
            while (i < originalData.rows.count()) {
            const bool last = i == originalData.rows.count(); // includes last line?
            const int lastLine = last
                    ? originalData.rows.count() : i - contextLineCount;
    while (i < originalData.rows.count()) {
        while (i < originalData.rows.count()) {
        line = startLineCharacter + textLine + QLatin1Char('\n');
            line += QLatin1String("\\ No newline at end of file\n");
        const int rowCount = chunkData.rows.count();
    for (int i = 0; i <= chunkData.rows.count(); i++) {
        const RowData &rowData = i < chunkData.rows.count()
                ? chunkData.rows.at(i)
                : RowData(TextLineData(TextLineData::Separator)); // dummy,
                                        // ensure we process buffers to the end.
                                        // rowData will be equal
            if (leftBuffer.count()) {
                for (int j = 0; j < leftBuffer.count(); j++) {
                    const QString line = makePatchLine(QLatin1Char('-'),
                                              leftBuffer.at(j).text,
                                              lastChunk,
                                              i == chunkData.rows.count()
                                                       && j == leftBuffer.count() - 1);
            if (rightBuffer.count()) {
                for (int j = 0; j < rightBuffer.count(); j++) {
                    const QString line = makePatchLine(QLatin1Char('+'),
                                              rightBuffer.at(j).text,
                                              lastChunk,
                                              i == chunkData.rows.count()
                                                       && j == rightBuffer.count() - 1);
            if (i < chunkData.rows.count()) {
                const QString line = makePatchLine(QLatin1Char(' '),
                                          rowData.rightLine.text,
                                          lastChunk,
                                          i == chunkData.rows.count() - 1);
    const QString chunkLine = QLatin1String("@@ -")
            + QLatin1Char(',')
            + QLatin1String(" +")
            + QLatin1Char(',')
            + QLatin1String(" @@")
            + QLatin1Char('\n');
    const QString rightFileInfo = QLatin1String("+++ ") + rightFileName + QLatin1Char('\n');
    const QString leftFileInfo = QLatin1String("--- ") + leftFileName + QLatin1Char('\n');
    for (int i = 0; i < fileDataList.count(); i++) {
            if (formatFlags & AddLevel)
                str << "a/";
            str << fileData.leftFileInfo.fileName << " and ";
            if (formatFlags & AddLevel)
                str << "b/";
            str << fileData.rightFileInfo.fileName << " differ\n";
            str << "--- ";
            if (formatFlags & AddLevel)
                str << "a/";
            str << fileData.leftFileInfo.fileName << "\n+++ ";
            if (formatFlags & AddLevel)
                str << "b/";
            str << fileData.rightFileInfo.fileName << '\n';
            for (int j = 0; j < fileData.chunks.count(); j++) {
                str << makePatch(fileData.chunks.at(j),
                                      (j == fileData.chunks.count() - 1)
                                      && fileData.lastChunkAtTheEndOfFile);
static QList<RowData> readLines(const QString &patch,
                                bool lastChunk,
                                bool *lastChunkAtTheEndOfFile,
                                bool *ok)
//    const QRegExp lineRegExp(QLatin1String("(?:\\n)"                // beginning of the line
//                                           "([ -\\+\\\\])([^\\n]*)" // -, +, \\ or space, followed by any no-newline character
//                                           "(?:\\n|$)"));           // end of line or file
    const QChar newLine = QLatin1Char('\n');
    const QStringList lines = patch.split(newLine);
    for (i = 0; i < lines.count(); i++) {
        const QString line = lines.at(i);
                i = lines.count(); // pretend as we've read all the lines (we just ignore the rest)
        QChar firstCharacter = line.at(0);
        if (firstCharacter == QLatin1Char('\\')) { // no new line marker
                    noNewLineInEqual = diffList.count() - 1;
                    noNewLineInDelete = diffList.count() - 1;
                    noNewLineInInsert = diffList.count() - 1;
            if (firstCharacter == QLatin1Char(' ')) { // common line
            } else if (firstCharacter == QLatin1Char('-')) { // deleted line
            } else if (firstCharacter == QLatin1Char('+')) { // inserted line
                    i = lines.count(); // pretend as we've read all the lines (we just ignore the rest)
            Diff diffToBeAdded(command, line.mid(1) + newLine);
                lastEqual = diffList.count() - 1;
                lastDelete = diffList.count() - 1;
                lastInsert = diffList.count() - 1;
    if (i < lines.count() // we broke before
            // or we have noNewLine in some equal line and in either delete or insert line
            || (noNewLineInEqual >= 0 && (noNewLineInDelete >= 0 || noNewLineInInsert >= 0))
            // or we have noNewLine in not the last equal line
            || (noNewLineInEqual >= 0 && noNewLineInEqual != lastEqual)
            // or we have noNewLine in not the last delete line or there is a equal line after the noNewLine for delete
            || (noNewLineInDelete >= 0 && (noNewLineInDelete != lastDelete || lastEqual > lastDelete))
            // or we have noNewLine in not the last insert line or there is a equal line after the noNewLine for insert
            || (noNewLineInInsert >= 0 && (noNewLineInInsert != lastInsert || lastEqual > lastInsert))) {
        diff.text = diff.text.left(diff.text.count() - 1);
        diff.text = diff.text.left(diff.text.count() - 1);
        diff.text = diff.text.left(diff.text.count() - 1);
static QList<ChunkData> readChunks(const QString &patch,
                                   bool *lastChunkAtTheEndOfFile,
                                   bool *ok)
    const QRegExp chunkRegExp(QLatin1String(
                                  // beginning of the line
                              "(?:\\n|^)"
                                  // @@ -leftPos[,leftCount] +rightPos[,rightCount] @@
                              "@@ -(\\d+)(?:,\\d+)? \\+(\\d+)(?:,\\d+)? @@"
                                  // optional hint (e.g. function name)
                              "(\\ +[^\\n]*)?"
                                  // end of line (need to be followed by text line)
                              "\\n"));
    bool readOk = false;
    int pos = chunkRegExp.indexIn(patch);
    if (pos == 0) {
        int endOfLastChunk = 0;
        do {
            const int leftStartingPos = chunkRegExp.cap(1).toInt();
            const int rightStartingPos = chunkRegExp.cap(2).toInt();
            const QString contextInfo = chunkRegExp.cap(3);
            if (endOfLastChunk > 0) {
                const QString lines = patch.mid(endOfLastChunk,
                                                pos - endOfLastChunk);
                chunkDataList.last().rows = readLines(lines,
                                                      false,
                                                      lastChunkAtTheEndOfFile,
                                                      &readOk);
                if (!readOk)
                    break;
            }
            pos += chunkRegExp.matchedLength();
            endOfLastChunk = pos;
            ChunkData chunkData;
            chunkData.leftStartingLineNumber = leftStartingPos - 1;
            chunkData.rightStartingLineNumber = rightStartingPos - 1;
            chunkData.contextInfo = contextInfo;
            chunkDataList.append(chunkData);
        } while ((pos = chunkRegExp.indexIn(patch, pos, QRegExp::CaretAtOffset)) != -1);

        if (endOfLastChunk > 0) {
            const QString lines = patch.mid(endOfLastChunk);
            chunkDataList.last().rows = readLines(lines,
                                                  true,
                                                  lastChunkAtTheEndOfFile,
                                                  &readOk);
        }
static FileData readDiffHeaderAndChunks(const QString &headerAndChunks,
                                        bool *ok)
    QString patch = headerAndChunks;
    const QRegExp leftFileRegExp(QLatin1String(
                                     "(?:\\n|^)-{3} "        // "--- "
                                     "([^\\t\\n]+)"          // "fileName1"
                                     "(?:\\t[^\\n]*)*\\n")); // optionally followed by: \t anything \t anything ...)
    const QRegExp rightFileRegExp(QLatin1String(
                                      "^\\+{3} "              // "+++ "
                                      "([^\\t\\n]+)"          // "fileName2"
                                      "(?:\\t[^\\n]*)*\\n")); // optionally followed by: \t anything \t anything ...)
    const QRegExp binaryRegExp(QLatin1String("^Binary files ([^\\t\\n]+) and ([^\\t\\n]+) differ$"));

    // followed either by leftFileRegExp or by binaryRegExp
    if (leftFileRegExp.indexIn(patch) == 0) {
        patch.remove(0, leftFileRegExp.matchedLength());
        fileData.leftFileInfo.fileName = leftFileRegExp.cap(1);
        if (rightFileRegExp.indexIn(patch) == 0) {
            patch.remove(0, rightFileRegExp.matchedLength());
            fileData.rightFileInfo.fileName = rightFileRegExp.cap(1);
    } else if (binaryRegExp.indexIn(patch) == 0) {
        fileData.leftFileInfo.fileName = binaryRegExp.cap(1);
        fileData.rightFileInfo.fileName = binaryRegExp.cap(2);
        fileData.binaryFiles = true;
        readOk = true;
static QList<FileData> readDiffPatch(const QString &patch,
                                     bool *ok)
    const QRegExp diffRegExp(QLatin1String("(?:\\n|^)"          // new line of the beginning of a patch
                                           "("                  // either
                                           "-{3} "              // ---
                                           "[^\\t\\n]+"         // filename1
                                           "(?:\\t[^\\n]*)*\\n" // optionally followed by: \t anything \t anything ...
                                           "\\+{3} "            // +++
                                           "[^\\t\\n]+"         // filename2
                                           "(?:\\t[^\\n]*)*\\n" // optionally followed by: \t anything \t anything ...
                                           "|"                  // or
                                           "Binary files "
                                           "[^\\t\\n]+"         // filename1
                                           " and "
                                           "[^\\t\\n]+"         // filename2
                                           " differ"
                                           ")"));               // end of or
    int pos = diffRegExp.indexIn(patch);
    if (pos >= 0) { // git style patch
                const QString headerAndChunks = patch.mid(lastPos,
                                                          pos - lastPos);
            pos += diffRegExp.matchedLength();
        } while ((pos = diffRegExp.indexIn(patch, pos)) != -1);
        if (lastPos >= 0 && readOk) {
            const QString headerAndChunks = patch.mid(lastPos,
                                                      patch.count() - lastPos - 1);
static bool fileNameEnd(const QChar &c)
    return c == QLatin1Char('\n') || c == QLatin1Char('\t');
}
static FileData readGitHeaderAndChunks(const QString &headerAndChunks,
                                       const QString &fileName,
                                       bool *ok)
{
    FileData fileData;
    fileData.leftFileInfo.fileName = fileName;
    fileData.rightFileInfo.fileName = fileName;
    QString patch = headerAndChunks;
    bool readOk = false;
    const QString devNull(QLatin1String("/dev/null"));
    // will be followed by: index 0000000..shasha, file "a" replaced by "/dev/null", @@ -0,0 +m,n @@
    const QRegExp newFileMode(QLatin1String("^new file mode \\d+\\n")); // new file mode octal
    // will be followed by: index shasha..0000000, file "b" replaced by "/dev/null", @@ -m,n +0,0 @@
    const QRegExp deletedFileMode(QLatin1String("^deleted file mode \\d+\\n")); // deleted file mode octal
    const QRegExp modeChangeRegExp(QLatin1String("^old mode \\d+\\nnew mode \\d+\\n"));
    const QRegExp indexRegExp(QLatin1String("^index (\\w+)\\.{2}(\\w+)(?: \\d+)?(\\n|$)")); // index cap1..cap2(optionally: octal)
    QString leftFileName = QLatin1String("a/") + fileName;
    QString rightFileName = QLatin1String("b/") + fileName;
    if (newFileMode.indexIn(patch) == 0) {
        fileData.fileOperation = FileData::NewFile;
        leftFileName = devNull;
        patch.remove(0, newFileMode.matchedLength());
    } else if (deletedFileMode.indexIn(patch) == 0) {
        fileData.fileOperation = FileData::DeleteFile;
        rightFileName = devNull;
        patch.remove(0, deletedFileMode.matchedLength());
    } else if (modeChangeRegExp.indexIn(patch) == 0) {
        patch.remove(0, modeChangeRegExp.matchedLength());
    }
    if (indexRegExp.indexIn(patch) == 0) {
        fileData.leftFileInfo.typeInfo = indexRegExp.cap(1);
        fileData.rightFileInfo.typeInfo = indexRegExp.cap(2);
        patch.remove(0, indexRegExp.matchedLength());
    }
    const QString binaryLine = QString::fromLatin1("Binary files ") + leftFileName
            + QLatin1String(" and ") + rightFileName + QLatin1String(" differ");
    const QString leftStart = QString::fromLatin1("--- ") + leftFileName;
    QChar leftFollow = patch.count() > leftStart.count() ? patch.at(leftStart.count()) : QLatin1Char('\n');
    // empty or followed either by leftFileRegExp or by binaryRegExp
    if (patch.isEmpty() && (fileData.fileOperation == FileData::NewFile
                         || fileData.fileOperation == FileData::DeleteFile)) {
        readOk = true;
    } else if (patch.startsWith(leftStart) && fileNameEnd(leftFollow)) {
        patch.remove(0, patch.indexOf(QLatin1Char('\n'), leftStart.count()) + 1);
        const QString rightStart = QString::fromLatin1("+++ ") + rightFileName;
        QChar rightFollow = patch.count() > rightStart.count() ? patch.at(rightStart.count()) : QLatin1Char('\n');
        // followed by rightFileRegExp
        if (patch.startsWith(rightStart) && fileNameEnd(rightFollow)) {
            patch.remove(0, patch.indexOf(QLatin1Char('\n'), rightStart.count()) + 1);
            fileData.chunks = readChunks(patch,
                                         &fileData.lastChunkAtTheEndOfFile,
                                         &readOk);
    } else if (patch == binaryLine) {
        readOk = true;
        fileData.binaryFiles = true;
    }
    if (ok)
        *ok = readOk;
    if (!readOk)
        return FileData();
    return fileData;
}
static FileData readCopyRenameChunks(const QString &copyRenameChunks,
                                     FileData::FileOperation fileOperation,
                                     const QString &leftFileName,
                                     const QString &rightFileName,
                                     bool *ok)
{
    FileData fileData;
    fileData.fileOperation = fileOperation;
    fileData.leftFileInfo.fileName = leftFileName;
    fileData.rightFileInfo.fileName = rightFileName;
    QString patch = copyRenameChunks;
    bool readOk = false;
    const QRegExp indexRegExp(QLatin1String("^index (\\w+)\\.{2}(\\w+)(?: \\d+)?(\\n|$)")); // index cap1..cap2(optionally: octal)
    if (fileOperation == FileData::CopyFile || fileOperation == FileData::RenameFile) {
        if (indexRegExp.indexIn(patch) == 0) {
            fileData.leftFileInfo.typeInfo = indexRegExp.cap(1);
            fileData.rightFileInfo.typeInfo = indexRegExp.cap(2);
            patch.remove(0, indexRegExp.matchedLength());
            const QString leftStart = QString::fromLatin1("--- a/") + leftFileName;
            QChar leftFollow = patch.count() > leftStart.count() ? patch.at(leftStart.count()) : QLatin1Char('\n');
            // followed by leftFileRegExp
            if (patch.startsWith(leftStart) && fileNameEnd(leftFollow)) {
                patch.remove(0, patch.indexOf(QLatin1Char('\n'), leftStart.count()) + 1);
                // followed by rightFileRegExp
                const QString rightStart = QString::fromLatin1("+++ b/") + rightFileName;
                QChar rightFollow = patch.count() > rightStart.count() ? patch.at(rightStart.count()) : QLatin1Char('\n');
                // followed by rightFileRegExp
                if (patch.startsWith(rightStart) && fileNameEnd(rightFollow)) {
                    patch.remove(0, patch.indexOf(QLatin1Char('\n'), rightStart.count()) + 1);
                    fileData.chunks = readChunks(patch,
                                                 &fileData.lastChunkAtTheEndOfFile,
                                                 &readOk);
                }
            }
        } else if (copyRenameChunks.isEmpty()) {
            readOk = true;
        }
    }
    if (ok)
        *ok = readOk;
    if (!readOk)
        return FileData();
    return fileData;
}
static QList<FileData> readGitPatch(const QString &patch, bool *ok)
{
    const QRegExp simpleGitRegExp(QLatin1String("(?:\\n|^)diff --git a/([^\\n]+) b/\\1\\n")); // diff --git a/cap1 b/cap1
    const QRegExp similarityRegExp(QLatin1String(
                  "(?:\\n|^)diff --git a/([^\\n]+) b/([^\\n]+)\\n" // diff --git a/cap1 b/cap2
                  "(?:dis)?similarity index \\d{1,3}%\\n"          // similarity / dissimilarity index xxx% (100% max)
                  "(copy|rename) from \\1\\n"                      // copy / rename from cap1
                  "\\3 to \\2\\n"));                               // copy / rename (cap3) to cap2
    bool readOk = false;

    bool simpleGitMatched;
    int pos = 0;
    auto calculateGitMatchAndPosition = [&]() {
        const int simpleGitPos = simpleGitRegExp.indexIn(patch, pos, QRegExp::CaretAtOffset);
        const int similarityPos = similarityRegExp.indexIn(patch, pos, QRegExp::CaretAtOffset);
        if (simpleGitPos < 0) {
            pos = similarityPos;
            simpleGitMatched = false;
            return;
        } else if (similarityPos < 0) {
            pos = simpleGitPos;
            simpleGitMatched = true;
            return;
        pos = qMin(simpleGitPos, similarityPos);
        simpleGitMatched = (pos == simpleGitPos);
    };
    // Set both pos and simpleGitMatched according to the first match:
    calculateGitMatchAndPosition();
    if (pos >= 0) { // git style patch
        readOk = true;
        int endOfLastHeader = 0;
        QString lastLeftFileName;
        QString lastRightFileName;
        FileData::FileOperation lastOperation = FileData::ChangeFile;
        do {
            if (endOfLastHeader > 0) {
                const QString headerAndChunks = patch.mid(endOfLastHeader,
                                                          pos - endOfLastHeader);

                FileData fileData;
                if (lastOperation == FileData::ChangeFile) {
                    fileData = readGitHeaderAndChunks(headerAndChunks,
                                                      lastLeftFileName,
                                                      &readOk);
                } else {
                    fileData = readCopyRenameChunks(headerAndChunks,
                                                    lastOperation,
                                                    lastLeftFileName,
                                                    lastRightFileName,
                                                    &readOk);
                }
                if (!readOk)
                    break;

                fileDataList.append(fileData);
            }
            if (simpleGitMatched) {
                const QString fileName = simpleGitRegExp.cap(1);
                pos += simpleGitRegExp.matchedLength();
                endOfLastHeader = pos;
                lastLeftFileName = fileName;
                lastRightFileName = fileName;
                lastOperation = FileData::ChangeFile;
            } else {
                lastLeftFileName = similarityRegExp.cap(1);
                lastRightFileName = similarityRegExp.cap(2);
                const QString operation = similarityRegExp.cap(3);
                pos += similarityRegExp.matchedLength();
                endOfLastHeader = pos;
                if (operation == QLatin1String("copy"))
                    lastOperation = FileData::CopyFile;
                else if (operation == QLatin1String("rename"))
                    lastOperation = FileData::RenameFile;
                else
                    break; // either copy or rename, otherwise broken
            }

            // give both pos and simpleGitMatched a new value for the next match
            calculateGitMatchAndPosition();
        } while (pos != -1);

        if (endOfLastHeader > 0 && readOk) {
            const QString headerAndChunks = patch.mid(endOfLastHeader,
                                                      patch.count() - endOfLastHeader - 1);

            FileData fileData;
            if (lastOperation == FileData::ChangeFile) {

                fileData = readGitHeaderAndChunks(headerAndChunks,
                                                  lastLeftFileName,
                                                  &readOk);
            } else {
                fileData = readCopyRenameChunks(headerAndChunks,
                                                lastOperation,
                                                lastLeftFileName,
                                                lastRightFileName,
                                                &readOk);
            }
            if (readOk)
                fileDataList.append(fileData);
        }
QList<FileData> DiffUtils::readPatch(const QString &patch, bool *ok)
    QString croppedPatch = patch;
    // Crop e.g. "-- \n1.9.4.msysgit.0\n\n" at end of file
    const QRegExp formatPatchEndingRegExp(QLatin1String("(\\n-- \\n\\S*\\n\\n$)"));
    const int pos = formatPatchEndingRegExp.indexIn(patch);
    if (pos != -1)
        croppedPatch = patch.left(pos + 1); // crop the ending for git format-patch

    fileDataList = readGitPatch(croppedPatch, &readOk);
        fileDataList = readDiffPatch(croppedPatch, &readOk);