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

#include "mergedfilesystemmodel.h"

namespace OpenMV {
namespace Internal {

static QString cleanPath(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

MergedFilesystemModel::MergedFilesystemModel(QObject *parent) :
    QAbstractItemModel(parent), m_root(new Node)
{
}

MergedFilesystemModel::~MergedFilesystemModel()
{
    deleteNode(m_root);
}

void MergedFilesystemModel::deleteNode(Node *node)
{
    for (Node *child : node->children)
    {
        deleteNode(child);
    }

    delete node;
}

void MergedFilesystemModel::setRoots(const QStringList &roots)
{
    beginResetModel();

    deleteNode(m_root);
    m_root = new Node;
    m_byPath.clear();

    m_roots.clear();

    for (const QString &root : roots)
    {
        if (QFileInfo(root).isDir())
        {
            m_roots.append(cleanPath(root));
        }
    }

    buildChildren(m_root, m_roots);

    endResetModel();

    emit directoryLoaded(m_roots.isEmpty() ? QString() : m_roots.first());
}

void MergedFilesystemModel::buildChildren(Node *node, const QStringList &sourceDirs)
{
    // Gather entry names across every source directory, remembering the winning
    // (highest-priority = earliest) source's path and whether it is a directory,
    // and, for a merged directory, the list of source sub-directories to recurse.
    QStringList order;                         // names in first-seen (priority) order
    QHash<QString, QString> winningPath;        // name -> winning absolute path
    QHash<QString, bool> winningIsDir;          // name -> winning entry is a directory
    QHash<QString, QStringList> dirSources;     // name -> source sub-dirs (priority order)

    for (const QString &dir : sourceDirs)
    {
        const QFileInfoList entries = QDir(dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);

        for (const QFileInfo &entry : entries)
        {
            const QString name = entry.fileName();

            if (!winningPath.contains(name))
            {
                order.append(name);
                winningPath.insert(name, cleanPath(entry.absoluteFilePath()));
                winningIsDir.insert(name, entry.isDir());
            }

            if (entry.isDir())
            {
                dirSources[name].append(cleanPath(entry.absoluteFilePath()));
            }
        }
    }

    for (const QString &name : order)
    {
        Node *child = new Node;
        child->name = name;
        child->path = winningPath.value(name);
        child->isDir = winningIsDir.value(name);
        child->parent = node;
        node->children.append(child);
        m_byPath.insert(child->path, child);

        if (child->isDir)
        {
            buildChildren(child, dirSources.value(name));
        }
    }
}

MergedFilesystemModel::Node *MergedFilesystemModel::nodeForIndex(const QModelIndex &index) const
{
    return index.isValid() ? static_cast<Node *>(index.internalPointer()) : m_root;
}

QModelIndex MergedFilesystemModel::indexForNode(Node *node) const
{
    if ((node == Q_NULLPTR) || (node == m_root) || (node->parent == Q_NULLPTR))
    {
        return QModelIndex();
    }

    int row = node->parent->children.indexOf(node);
    return createIndex(row, 0, node);
}

QString MergedFilesystemModel::filePath(const QModelIndex &index) const
{
    Node *node = nodeForIndex(index);
    return (node == m_root) ? QString() : node->path;
}

bool MergedFilesystemModel::isDir(const QModelIndex &index) const
{
    Node *node = nodeForIndex(index);
    return (node == m_root) ? true : node->isDir;
}

QModelIndex MergedFilesystemModel::index(const QString &path, int column) const
{
    Node *node = m_byPath.value(cleanPath(path), Q_NULLPTR);
    return (node && (column == 0)) ? indexForNode(node) : QModelIndex();
}

bool MergedFilesystemModel::isUnderRoots(const QString &path) const
{
    const QString cleaned = cleanPath(path);

    for (const QString &root : m_roots)
    {
        if ((cleaned == root) || cleaned.startsWith(root + QLatin1Char('/')))
        {
            return true;
        }
    }

    return false;
}

QModelIndex MergedFilesystemModel::index(int row, int column, const QModelIndex &parent) const
{
    if ((column != 0) || (row < 0))
    {
        return QModelIndex();
    }

    Node *parentNode = nodeForIndex(parent);

    if (row >= parentNode->children.size())
    {
        return QModelIndex();
    }

    return createIndex(row, 0, parentNode->children.at(row));
}

QModelIndex MergedFilesystemModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
    {
        return QModelIndex();
    }

    return indexForNode(static_cast<Node *>(child.internalPointer())->parent);
}

int MergedFilesystemModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
    {
        return 0;
    }

    return nodeForIndex(parent)->children.size();
}

int MergedFilesystemModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return 1;
}

QVariant MergedFilesystemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }

    Node *node = static_cast<Node *>(index.internalPointer());

    switch (role)
    {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return node->name;
        case Qt::DecorationRole:
            return node->isDir
                ? m_iconProvider.icon(QFileIconProvider::Folder)
                : m_iconProvider.icon(QFileInfo(node->path));
        case Qt::ToolTipRole:
            return node->path;
        default:
            return QVariant();
    }
}

Qt::ItemFlags MergedFilesystemModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

} // namespace Internal
} // namespace OpenMV
