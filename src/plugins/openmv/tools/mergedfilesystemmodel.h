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

#ifndef MERGEDFILESYSTEMMODEL_H
#define MERGEDFILESYSTEMMODEL_H

#include <QtCore>
#include <QtWidgets>

// A read-only item model that presents several on-disk directory trees ("roots")
// overlaid into one merged tree, mimicking the slice of QFileSystemModel the
// Model Zoo browser needs. It exists because QFileSystemModel roots at a single
// directory, but third-party model repositories live in their own folders
// (outside the resource-managed "models" tree) and must appear merged with
// OpenMV's models.
//
// Merge rules (match the firmware/examples override policy):
//   - roots are given highest priority first;
//   - directories with the same name across roots merge (union of children);
//   - a file with the same relative path in several roots is taken from the
//     highest-priority (earliest) root; the others are shadowed.
//
// The tree is built eagerly (model trees are small) and directoryLoaded() is
// emitted once afterwards, matching how the browser waits for QFileSystemModel.
// Only column 0 (name + icon) is provided.

namespace OpenMV {
namespace Internal {

class MergedFilesystemModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    explicit MergedFilesystemModel(QObject *parent = Q_NULLPTR);
    ~MergedFilesystemModel();

    // (Re)build the merged tree. roots are absolute directory paths, highest
    // priority first. Emits directoryLoaded() when done.
    void setRoots(const QStringList &roots);
    QStringList roots() const { return m_roots; }

    // QFileSystemModel-compatible helpers used by the Model Zoo browser.
    QString filePath(const QModelIndex &index) const;
    bool isDir(const QModelIndex &index) const;
    QModelIndex index(const QString &path, int column = 0) const;

    // True if path is one of the roots or lives under one -- bounds the browser's
    // walk up the tree looking for an index.html.
    bool isUnderRoots(const QString &path) const;

    // QAbstractItemModel.
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

signals:
    // Mimics QFileSystemModel::directoryLoaded; emitted once after setRoots().
    void directoryLoaded(const QString &path);

private:
    struct Node
    {
        QString name;               // display name
        QString path;               // winning root's absolute path for this entry
        bool isDir = false;
        Node *parent = Q_NULLPTR;
        QList<Node *> children;
    };

    // Merge the parallel source directories (same relative position across roots,
    // highest priority first) into node's children.
    void buildChildren(Node *node, const QStringList &sourceDirs);

    Node *nodeForIndex(const QModelIndex &index) const;
    QModelIndex indexForNode(Node *node) const;
    static void deleteNode(Node *node);

    Node *m_root;                   // invisible root; its children are the merged top level
    QStringList m_roots;
    QHash<QString, Node *> m_byPath; // cleaned absolute path -> node, for index(path)
    QFileIconProvider m_iconProvider;
};

} // namespace Internal
} // namespace OpenMV

#endif // MERGEDFILESYSTEMMODEL_H
