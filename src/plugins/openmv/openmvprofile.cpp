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

#include "openmvprofile.h"
#include "openmvtr.h"

#include <coreplugin/icore.h>
#include <utils/theme/theme.h>
#include <utils/pathchooser.h>
#include <utils/utilsicons.h>

#define LAST_PROFILE_DIALOG_GEOMETRY "OpenMVProfileDialogGeometry"
#define LAST_PROFILE_DIALOG_HEADER_GEOMETRY "OpenMVProfileDialogHeaderGeometry"
#define LAST_PROFILE_DIALOG_HEADER_GEOMETRY_COLUMN_COUNT "OpenMVProfileDialogHeaderGeometryColumnCount"
#define LAST_PROFILE_DIALOG_HEADER_SORT_COLUMN "OpenMVProfileDialogHeaderSortColumn"
#define LAST_PROFILE_DIALOG_HEADER_SORT_ORDER "OpenMVProfileDialogHeaderSortOrder"
#define LAST_PROFILE_DIALOG_RUN "OpenMVProfileDialogRun"
#define LAST_PROFILE_DIALOG_FIRMWARE_PATH "OpenMVProfileDialogFirmwarePath"
#define LAST_PROFILE_DIALOG_FILTER_TEXT "OpenMVProfileDialogFilterText"
#define LAST_PROFILE_DIALOG_TREE "OpenMVProfileDialogTree"
#define LAST_PROFILE_DIALOG_MODE "OpenMVProfileDialogMode"
#define LAST_PROFILE_DIALOG_EVENT "OpenMVProfileDialogEvent"

#include "elfio/elfio/elfio.hpp"

#if defined(__has_include)
#  if __has_include(<cxxabi.h>)
#    define OPENMV_HAVE_CXA_DEMANGLE 1
#  endif
#endif

#ifdef OPENMV_HAVE_CXA_DEMANGLE
#include <cxxabi.h>
#include <cstdlib>
#endif

namespace OpenMV {
namespace Internal {

static inline qulonglong norm64(qulonglong a)
{
    return a & ~qulonglong(1);
}

inline QString maybeDemangle(const char *name, bool demangle)
{
#ifndef OPENMV_HAVE_CXA_DEMANGLE
    Q_UNUSED(demangle);
    return QString::fromUtf8(name ? name : "");
#else
    if (!demangle || !name) return QString::fromUtf8(name ? name : "");
    int status = 0;
    size_t len = 0;
    char* dem = abi::__cxa_demangle(name, nullptr, &len, &status);
    if (status == 0 && dem) {
        QString out = QString::fromUtf8(dem);
        std::free(dem);
        return out;
    }
    if (dem) std::free(dem);
    return QString::fromUtf8(name);
#endif
}

bool loadElfFunctionMap(const QString &elfPath,
                        QHash<qulonglong, QString> &out,
                        QMap<qulonglong, qulonglong> &outSize,
                        QString *errorOut = nullptr,
                        bool maskThumb = true,
                        bool demangle = false)
{
    out.clear();
    outSize.clear();

    ELFIO::elfio reader;

    if (!reader.load(elfPath.toStdString())) {
        if (errorOut) *errorOut = Tr::tr("Failed to open or parse ELF: %1").arg(elfPath);
        return false;
    }

    // First: .symtab (more complete), then .dynsym
    std::unique_ptr<ELFIO::section> symtab = nullptr;
    std::unique_ptr<ELFIO::section> dynsym = nullptr;

    for (const auto &sec : reader.sections) {
        if (!sec) continue;
        if (sec->get_type() == ELFIO::SHT_SYMTAB || sec->get_type() == ELFIO::SHT_DYNSYM) {
            ELFIO::symbol_section_accessor syms(reader, sec.get());
            for (ELFIO::Elf_Xword i = 0; i < syms.get_symbols_num(); i++) {
                std::string name;
                ELFIO::Elf64_Addr value = 0;
                ELFIO::Elf_Xword size = 0;
                unsigned char bind = 0, type = 0, other = 0;
                ELFIO::Elf_Half shndx = 0;

                if (!syms.get_symbol(i, name, value, size, bind, type, shndx, other)) continue;
                if (type != ELFIO::STT_FUNC) continue;
                if (value == 0) continue;
                if (shndx == ELFIO::SHN_UNDEF) continue;

                qulonglong addr = static_cast<qulonglong>(value);
                if (maskThumb) addr = norm64(addr);

                // Prefer first seen (.symtab usually precedes .dynsym in our pass),
                // but if not present yet, insert.
                if (!out.contains(addr)) {
                    out.insert(addr, maybeDemangle(name.c_str(), demangle));
                }

                if (size > 0 && !outSize.contains(addr)) {
                    outSize.insert(addr, size);
                }
            }
        }
    }

    // Not an error if stripped (no symbols), but tell caller if they asked.
    if (out.isEmpty() && errorOut) {
        *errorOut = Tr::tr("No function symbols found (.symtab/.dynsym missing or stripped)");
    }

    return true;
}

QModelIndex OpenMVProfileModel::index(int row, int column, const QModelIndex &parentIdx) const
{
    if (row < 0 || column < 0 || column >= (ColumnCount + eventSize_)) return QModelIndex();

    if (!parentIdx.isValid()) {
        if (row >= roots_.size()) return QModelIndex();
        Node *n = roots_.at(row);
        return createIndex(row, column, n);
    }

    Node *p = static_cast<Node*>(parentIdx.internalPointer());
    if (!p || row >= p->children.size()) return QModelIndex();
    Node *n = p->children.at(row);
    return createIndex(row, column, n);
}

QModelIndex OpenMVProfileModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) return QModelIndex();
    Node *n = static_cast<Node*>(child.internalPointer());
    if (!n || !n->parent) return QModelIndex();

    Node *p = n->parent;
    int prow = p->rowInParent();
    if (prow < 0) return QModelIndex();
    return createIndex(prow, 0, p);
}

int OpenMVProfileModel::rowCount(const QModelIndex &parentIdx) const
{
    if (parentIdx.column() > 0) return 0;
    if (!parentIdx.isValid()) return roots_.size();
    Node* n = static_cast<Node*>(parentIdx.internalPointer());
    return n ? n->children.size() : 0;
}

static inline int clamp255(int v) { return std::max(0, std::min(255, v)); }
static inline int lerp(int a, int b, double t) { return clamp255(a + int((b - a) * t)); }

QColor getColorByPercentage(double percentage, bool darkMode)
{
    auto darkRamp = [&]() -> QColor {
        if (percentage >= 50) {
            double t = std::min(1.0, (percentage - 50.0) / 50.0);
            return QColor(255, lerp(120, 0, t), lerp(120, 0, t)); // deep red
        } else if (percentage >= 30) {
            double t = (percentage - 30.0) / 20.0;
            return QColor(255, lerp(160, 200, t), lerp(160, 120, t)); // red-orange → red
        } else if (percentage >= 20) {
            double t = (percentage - 20.0) / 10.0;
            return QColor(255, lerp(200, 255, t), lerp(180, 160, t)); // dark orange
        } else if (percentage >= 15) {
            double t = (percentage - 15.0) / 5.0;
            return QColor(255, lerp(220, 255, t), lerp(180, 200, t)); // golden
        } else if (percentage >= 10) {
            double t = (percentage - 10.0) / 5.0;
            return QColor(lerp(255, 180, t), 255, lerp(180, 255, t)); // yellow
        } else if (percentage >= 5) {
            double t = (percentage - 5.0) / 5.0;
            return QColor(lerp(180, 255, t), 255, lerp(180, 255, t)); // green
        } else if (percentage >= 2) {
            double t = (percentage - 2.0) / 3.0;
            return QColor(lerp(160, 255, t), lerp(255, 200, t), lerp(160, 255, t)); // green
        } else if (percentage >= 1) {
            double t = (percentage - 1.0) / 1.0;
            return QColor(lerp(140, 255, t), lerp(200, 255, t), lerp(255, 160, t)); // teal
        } else {
            return Utils::creatorTheme()->color(Utils::Theme::TextColorNormal);
        }
    };

    auto lightRamp = [&]() -> QColor {
        if (percentage >= 50) {
            double t = std::min(1.0, (percentage - 50.0) / 50.0);
            return QColor(lerp(200, 255, t), lerp(32, 0, t), lerp(32, 0, t));   // deep red
        } else if (percentage >= 30) {
            double t = (percentage - 30.0) / 20.0;
            return QColor(lerp(220, 255, t), lerp(110, 60, t), lerp(20, 10, t));  // red-orange → red
        } else if (percentage >= 20) {
            double t = (percentage - 20.0) / 10.0;
            return QColor(lerp(220, 240, t), lerp(140, 100, t), lerp(10, 0, t));  // dark orange
        } else if (percentage >= 15) {
            double t = (percentage - 15.0) / 5.0;
            return QColor(lerp(200, 220, t), lerp(150, 130, t), lerp(20, 10, t));  // golden
        } else if (percentage >= 10) {
            double t = (percentage - 10.0) / 5.0;
            return QColor(lerp(170, 190, t), lerp(150, 160, t), lerp(20, 10, t));  // dark yellow/olive
        } else if (percentage >= 5) {
            double t = (percentage - 5.0) / 5.0;
            return QColor(lerp(50, 70, t), lerp(150, 170, t), lerp(50, 70, t)); // deeper green
        } else if (percentage >= 2) {
            double t = (percentage - 2.0) / 3.0;
            return QColor(lerp(40, 55, t), lerp(120, 150, t), lerp(40, 55, t)); // dark green
        } else if (percentage >= 1) {
            double t = (percentage - 1.0) / 1.0;
            return QColor(lerp(40, 50, t), lerp(130, 150, t), lerp(160, 180, t)); // deep teal
        } else {
            return Utils::creatorTheme()->color(Utils::Theme::TextColorNormal);
        }
    };

    return darkMode ? darkRamp() : lightRamp();
}

QVariant OpenMVProfileModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid()) return QVariant();
    Node *n = static_cast<Node*>(idx.internalPointer());
    if (!n) return QVariant();

    const profile_record_t &r = n->rec;
    uint32_t call_count = qMax(r.call_count, uint32_t(1));
    uint64_t totalTicks = qMax(totalTicks_, qulonglong(1));
    float percentage = 100.0f * r.total_ticks / totalTicks;

    if (role == Qt::ForegroundRole) {
        return getColorByPercentage(percentage, Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface));
    }

    if (role == Qt::DisplayRole) {
        if (idx.column() >= ColumnCount && idx.column() < (ColumnCount + eventSize_)) {
            int eventIdx = idx.column() - ColumnCount;
            return static_cast<qulonglong>(r.events.at(eventIdx));
        }

        switch (idx.column()) {
            case Address: {
                qulonglong a = norm64(r.address);
                const QString name = symMap_.value(a);
                if (!name.isEmpty()) return QStringLiteral("%1").arg(name);
                return QStringLiteral("0x%1").arg(a, 8, 16, QLatin1Char('0'));
            }
            case Calls:      return static_cast<qulonglong>(r.call_count);
            case MinTicks:   return static_cast<qulonglong>(r.min_ticks);
            case MaxTicks:   return static_cast<qulonglong>(r.max_ticks);
            case TotalTicks: return static_cast<qulonglong>(r.total_ticks);
            case AvgTicks:   return static_cast<qulonglong>(r.total_ticks / call_count);
            case AvgCycles:  return static_cast<qulonglong>(r.total_cycles / call_count);
            case Percentage: return QString(QStringLiteral("%1%")).arg(percentage, 0, 'f', 2);
            default:         return QVariant();
        }
    }

    if (role == Qt::UserRole) {
        if (idx.column() >= ColumnCount && idx.column() < (ColumnCount + eventSize_)) {
            int eventIdx = idx.column() - ColumnCount;
            return static_cast<qulonglong>(r.events.at(eventIdx));
        }

        switch (idx.column()) {
            case Address:    return static_cast<qulonglong>(norm64(r.address));
            case Calls:      return static_cast<qulonglong>(r.call_count);
            case MinTicks:   return static_cast<qulonglong>(r.min_ticks);
            case MaxTicks:   return static_cast<qulonglong>(r.max_ticks);
            case TotalTicks: return static_cast<qulonglong>(r.total_ticks);
            case AvgTicks:   return static_cast<qulonglong>(r.total_ticks / call_count);
            case AvgCycles:  return static_cast<qulonglong>(r.total_cycles / call_count);
            case Percentage: return static_cast<float>(100.0f * r.total_ticks / totalTicks);
            default:         return QVariant();
        }
    }

    return QVariant();
}

QVariant OpenMVProfileModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();

    if (section >= ColumnCount && section < (ColumnCount + eventSize_)) {
        return headerData_.value(section, Tr::tr("Event %1").arg(section - ColumnCount));
    }

    switch (section) {
        case Address:    return Tr::tr("Function");
        case Calls:      return Tr::tr("Calls");
        case MinTicks:   return Tr::tr("Min μs");
        case MaxTicks:   return Tr::tr("Max μs");
        case TotalTicks: return Tr::tr("Total μs");
        case AvgTicks:   return Tr::tr("Average μs");
        case AvgCycles:  return Tr::tr("Average Cycles");
        case Percentage: return Tr::tr("Percentage");
        default:         return QVariant();
    }
}

Qt::ItemFlags OpenMVProfileModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void OpenMVProfileModel::sort(int column, Qt::SortOrder order)
{
    lastSortColumn_ = column;
    lastSortOrder_ = order;

    // Let the view know we're about to reorder rows
    layoutAboutToBeChanged();

    auto less = [&](Node *a, Node *b) -> bool {
        const profile_record_t &ra = a->rec;
        const profile_record_t &rb = b->rec;
        uint32_t ra_call_count = qMax(ra.call_count, uint32_t(1));
        uint32_t rb_call_count = qMax(rb.call_count, uint32_t(1));
        qulonglong totalTicks = qMax(totalTicks_, qulonglong(1));

        auto cmpNum = [&](qulonglong x, qulonglong y) {
            return (order == Qt::AscendingOrder) ? (x < y) : (x > y);
        };

        auto cmpStr = [&](const QString &x, const QString &y) {
            const int r = QString::localeAwareCompare(x, y);
            return (order == Qt::AscendingOrder) ? (r < 0) : (r > 0);
        };

        if (column >= ColumnCount && column < (ColumnCount + eventSize_)) {
            int eventIdx = column - ColumnCount;
            return cmpNum(a->rec.events.at(eventIdx), b->rec.events.at(eventIdx));
        }

        switch (column) {
            case Address: {
                const qulonglong ax = norm64(ra.address);
                const qulonglong bx = norm64(rb.address);
                QString an = symMap_.value(ax);
                QString bn = symMap_.value(bx);
                if (an.isEmpty()) an = QStringLiteral("0x%1").arg(ax, 8, 16, QLatin1Char('0'));
                if (bn.isEmpty()) bn = QStringLiteral("0x%1").arg(bx, 8, 16, QLatin1Char('0'));
                return cmpStr(an, bn);
            }
            case Calls:      return cmpNum(ra.call_count,                         rb.call_count);
            case MinTicks:   return cmpNum(ra.min_ticks,                          rb.min_ticks);
            case MaxTicks:   return cmpNum(ra.max_ticks,                          rb.max_ticks);
            case TotalTicks: return cmpNum(ra.total_ticks,                        rb.total_ticks);
            case AvgTicks:   return cmpNum(ra.total_ticks / ra_call_count,        rb.total_ticks / rb_call_count);
            case AvgCycles:  return cmpNum(ra.total_cycles / ra_call_count,       rb.total_cycles / rb_call_count);
            case Percentage: return cmpNum(100.0f * ra.total_ticks / totalTicks, 100.0f * rb.total_ticks / totalTicks);
            default:         return false;
        }
    };

    // Sort roots and recursively sort each node's children
    auto sortList = [&](auto &&self, QList<Node *> &list) -> void {
        std::sort(list.begin(), list.end(), less);
        for (Node *n : list)
            if (!n->children.isEmpty())
                self(self, n->children);
    };

    sortList(sortList, roots_);

    // Update cached root row indices (used in parent())
    for (int i = 0; i < roots_.size(); i++) roots_[i]->rootRow = i;

    layoutChanged();
}

void OpenMVProfileModel::setRecords(const QList<profile_record_t> &records, bool tree)
{
    totalTicks_ = 0;
    eventSize_ = 0;
    for (const profile_record_t &r : records) {
        totalTicks_ += r.total_ticks;
        eventSize_ = qMax(eventSize_, r.events.size());
    }

    // initial build (no nodes yet) -> build once
    if (allNodes_.isEmpty()) {
        records_ = records;
        rebuildTree(tree);
        beginResetModel();
        endResetModel();
        return;
    }

    // subsequent updates -> incremental
    updateRecords(records, tree);
}

void OpenMVProfileModel::updateRecords(const QList<profile_record_t> &newRecords, bool tree)
{
    // 1) Build quick lookup of incoming records by normalized start address
    QHash<qulonglong, profile_record_t> incoming;
    incoming.reserve(newRecords.size());
    for (const auto &r : newRecords) incoming.insert(norm64(r.address), r);

    // 2) Remove nodes that disappeared (bottom-up, recursively)
    auto pruneList = [&](auto &&self, Node *parent, QList<Node *> &list) -> void {
        // iterate backwards so indices stay valid as we remove
        for (int i = list.size() - 1; i >= 0; i--) {
            Node *n = list.at(i);

            // Recurse first: remove children that are gone
            if (!n->children.isEmpty()) self(self, n, n->children);

            const qulonglong key = norm64(n->rec.address);
            if (!incoming.contains(key)) {
                // remove this node from the model
                const QModelIndex pIdx = parent ? indexForNode(parent) : QModelIndex();
                const int row = parent ? i : n->rootRow;

                beginRemoveRows(pIdx, row, row);
                list.removeAt(i);
                if (!parent) {
                    // keep rootRow correct for remaining roots
                    for (int r = row; r < roots_.size(); r++) roots_[r]->rootRow = r;
                }
                endRemoveRows();

                byAddress_.remove(key);
                allNodes_.removeOne(n);
                delete n;
            }
        }
    };

    // Kick it off from roots
    pruneList(pruneList, nullptr, roots_);

    // 3) Upsert (insert new / update existing), and move if parent changed
    for (auto it = incoming.begin(); it != incoming.end(); it++) {
        const qulonglong addr = it.key();
        const profile_record_t &in = it.value();

        Node *n = byAddress_.value(addr, nullptr);
        const qulonglong newParentStart = tree ? functionStartFor(norm64(in.caller)) : 0;
        Node *newParent = newParentStart ? byAddress_.value(newParentStart, nullptr) : nullptr;

        if (!n) {
            // INSERT
            n = new Node;
            n->rec = in;
            byAddress_.insert(addr, n);
            allNodes_.append(n);

            if (newParent) {
                n->parent = newParent;
                const int dstRow = newParent->children.size();
                QModelIndex dstParentIdx = indexForNode(newParent);
                beginInsertRows(dstParentIdx, dstRow, dstRow);
                newParent->children.append(n);
                endInsertRows();
            } else {
                n->parent = nullptr;
                const int dstRow = roots_.size();
                beginInsertRows(QModelIndex(), dstRow, dstRow);
                n->rootRow = dstRow;
                roots_.append(n);
                endInsertRows();
            }

            continue;
        }

        // EXISTING: check for parent change
        Node *oldParent = n->parent;
        if (oldParent != newParent) {
            // MOVE
            QModelIndex srcParentIdx = oldParent ? indexForNode(oldParent) : QModelIndex();
            int srcRow = n->rowInParent();

            // detach from old parent list
            if (oldParent) {
                beginMoveRows(srcParentIdx, srcRow, srcRow,
                              newParent ? indexForNode(newParent) : QModelIndex(),
                              newParent ? newParent->children.size() : roots_.size());
                oldParent->children.removeAt(srcRow);
                if (newParent) {
                    n->parent = newParent;
                    newParent->children.append(n);
                } else {
                    n->parent = nullptr;
                    n->rootRow = roots_.size();
                    roots_.append(n);
                }
                endMoveRows();
            } else {
                // moving from root to child or between roots
                beginMoveRows(QModelIndex(), n->rootRow, n->rootRow,
                              newParent ? indexForNode(newParent) : QModelIndex(),
                              newParent ? newParent->children.size() : roots_.size());
                roots_.removeAt(n->rootRow);
                for (int i = n->rootRow; i < roots_.size(); i++) roots_[i]->rootRow = i;
                if (newParent) {
                    n->parent = newParent;
                    newParent->children.append(n);
                } else {
                    n->parent = nullptr;
                    n->rootRow = roots_.size();
                    roots_.append(n);
                }
                endMoveRows();
            }
        }

        // Update numeric fields in-place & emit dataChanged if something changed
        const profile_record_t before = n->rec;
        n->rec = in;

        bool changed =
            before.address      != n->rec.address      ||
            before.caller       != n->rec.caller       ||
            before.call_count   != n->rec.call_count   ||
            before.min_ticks    != n->rec.min_ticks    ||
            before.max_ticks    != n->rec.max_ticks    ||
            before.total_ticks  != n->rec.total_ticks  ||
            before.total_cycles != n->rec.total_cycles ||
            before.events       != n->rec.events;

        if (changed) {
            const QModelIndex left = indexForNode(n, 0);
            const int lastCol = (ColumnCount - 1) + qMax(0, eventSize_);
            const QModelIndex right = indexForNode(n, lastCol);
            emit dataChanged(left, right, { Qt::DisplayRole, Qt::UserRole });
        }
    }

    if (lastSortColumn_ >= 0) sort(lastSortColumn_, lastSortOrder_);
}

void OpenMVProfileModel::clear()
{
    beginResetModel();
    records_.clear();
    clearNodes();
    endResetModel();
}

void OpenMVProfileModel::clearNodes()
{
    qDeleteAll(allNodes_);
    allNodes_.clear();
    roots_.clear();
    byAddress_.clear();
}

QModelIndex OpenMVProfileModel::indexForNode(Node *n, int column) const
{
    if (!n) return QModelIndex();
    if (!n->parent) return createIndex(n->rootRow, column, n);
    return createIndex(n->rowInParent(), column, n);
}

void OpenMVProfileModel::rebuildTree(bool tree)
{
    clearNodes();
    allNodes_.reserve(records_.size());

    for (int i = 0; i < records_.size(); i++) {
        Node *n = new Node;
        n->rec = records_[i];
        allNodes_.append(n);
        byAddress_.insert(norm64(records_[i].address), n);
    }

    for (int i = 0; i < allNodes_.size(); i++) {
        Node *n = allNodes_.at(i);
        uint32_t callerAddr = tree ? functionStartFor(norm64(n->rec.caller)) : 0;
        Node *parent = byAddress_.value(callerAddr, nullptr);

        if (parent && parent != n) {
            n->parent = parent;
            parent->children.append(n);
        } else {
            n->parent = nullptr;
            n->rootRow = roots_.size();
            roots_.append(n);
        }
    }

    if (lastSortColumn_ >= 0) sort(lastSortColumn_, lastSortOrder_);
}

qulonglong OpenMVProfileModel::functionStartFor(qulonglong pc) const
{
    if (symMapSizes_.isEmpty()) return 0;
    auto it = symMapSizes_.upperBound(pc);  // first start > pc
    if (it == symMapSizes_.begin()) return 0;
    --it;                                   // candidate with start <= pc
    const qulonglong start = it.key();
    const qulonglong size  = it.value();
    return (pc < start + size) ? start : 0;
}

static QString selectionToText(QTreeView *view, int role = Qt::DisplayRole)
{
    const QAbstractItemModel *model = view->model();
    if (!model) return {};

    const QItemSelectionModel *sel  = view->selectionModel();
    if (!sel)  return {};

    const QModelIndexList idxs = sel->selectedIndexes();
    if (idxs.isEmpty()) return {};

    if (idxs.size() == 1)
    {
        QString s = idxs.first().data(role).toString();
        s.replace('\t', ' ').replace('\n', ' ');
        return s;
    }

    // Build the set of selected columns
    QSet<int> selectedCols;
    for (const QModelIndex &ix : idxs) selectedCols.insert(ix.column());

    // Get columns in the *visual* order shown by the header; keep only selected & visible
    QList<int> colsInVisualOrder;
    QHeaderView *hdr = view->header();
    const int sectionCount = hdr->count();

    for (int visual = 0; visual < sectionCount; ++visual)
    {
        int logical = hdr->logicalIndex(visual);
        if (logical >= 0 && selectedCols.contains(logical)) colsInVisualOrder.append(logical);
    }

    // Group selection by (parent,row) -> list of indexes
    QMultiMap<QModelIndex, QModelIndex> byRowKey; // key = index(row,0,parent)
    for (const QModelIndex &ix : idxs) byRowKey.insert(model->index(ix.row(), 0, ix.parent()), ix);
    QString out;

    // Header row: in the same visual order
    {
        QStringList headerFields;
        headerFields.reserve(colsInVisualOrder.size());

        for (int c : colsInVisualOrder)
        {
            QString h = model->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
            h.replace('\t', ' ').replace('\n', ' ');
            headerFields << h;
        }

        out += headerFields.join('\t') + QLatin1Char('\n');
    }

    // Data rows (preserve visual column order; blank if that column wasn't selected in that row)
    for (auto it = byRowKey.cbegin(); it != byRowKey.cend(); )
    {
        const QModelIndex rowKey = it.key();
        const QList<QModelIndex> rowCells = byRowKey.values(rowKey);
        it = byRowKey.upperBound(rowKey); // advance

        // Map logical column -> index for this row
        QHash<int, QModelIndex> colToIndex;
        colToIndex.reserve(rowCells.size());
        for (const QModelIndex &ix : rowCells) colToIndex.insert(ix.column(), ix);

        QStringList fields;
        fields.reserve(colsInVisualOrder.size());

        for (int c : colsInVisualOrder)
        {
            const QModelIndex ix = colToIndex.value(c);
            QString s;

            if (ix.isValid())
            {
                s = ix.data(role).toString();
                s.replace('\t', ' ').replace('\n', ' ');
            }

            fields << s;
        }

        out += fields.join('\t') + QLatin1Char('\n');
    }

    return out;
}

OpenMVProfileView::OpenMVProfileView(Utils::QtcSettings *settings, QWidget *parent) : QDialog(parent), m_settings(settings)
{
    setWindowFlags(windowFlags() | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    setWindowTitle(Tr::tr("Code Profiler"));
    setMinimumSize(QSize(480, 480));
    setSizeGripEnabled(true);

    QVBoxLayout *vlayout = new QVBoxLayout(this);

    QWidget *toolWidget = new QWidget;
    QHBoxLayout *toolWidgetLayout = new QHBoxLayout(toolWidget);
    toolWidgetLayout->setContentsMargins(0, 0, 0, 0);
    vlayout->addWidget(toolWidget);

    m_runButton = new QToolButton(this);
    m_runButton->setCheckable(true);
    m_runButton->setChecked(m_settings->value(LAST_PROFILE_DIALOG_RUN, true).toBool());
    m_runButton->setIcon(m_runButton->isChecked() ? Utils::Icons::STOP_SMALL_TOOLBAR.icon() : Utils::Icons::RUN_SMALL_TOOLBAR.icon());
    toolWidgetLayout->addWidget(m_runButton);

    connect(m_runButton, &QToolButton::toggled, this, [this](bool checked) {
        m_runButton->setIcon(checked ? Utils::Icons::STOP_SMALL_TOOLBAR.icon() : Utils::Icons::RUN_SMALL_TOOLBAR.icon());
    });

    QWidget *toolWidget2 = new QWidget;
    QHBoxLayout *toolWidgetLayout2 = new QHBoxLayout(toolWidget2);
    toolWidgetLayout2->setContentsMargins(0, 0, 0, 0);

    QRadioButton *flatMode = new QRadioButton(Tr::tr("Flat"), this);
    flatMode->setChecked(!m_settings->value(LAST_PROFILE_DIALOG_TREE, false).toBool());
    toolWidgetLayout2->addWidget(flatMode);

    m_treeMode = new QRadioButton(Tr::tr("Tree"), this);
    m_treeMode->setChecked(m_settings->value(LAST_PROFILE_DIALOG_TREE, false).toBool());
    toolWidgetLayout2->addWidget(m_treeMode);

    toolWidgetLayout->addWidget(toolWidget2);

    QWidget *pathChooserWidget = new QWidget;
    QFormLayout *pathChooserWidgetLayout = new QFormLayout(pathChooserWidget);
    pathChooserWidgetLayout->setContentsMargins(0, 0, 0, 0);
    m_pathChooser = new Utils::PathChooser();
    m_pathChooser->setExpectedKind(Utils::PathChooser::File);
    m_pathChooser->setPromptDialogTitle(Tr::tr("Firmware Path"));
    m_pathChooser->setPromptDialogFilter(Tr::tr("Firmware ELF (*.elf)"));
    m_pathChooser->setFilePath(Utils::FilePath::fromVariant(m_settings->value(LAST_PROFILE_DIALOG_FIRMWARE_PATH, QDir::homePath())));
    m_pathChooser->setHistoryCompleter(LAST_PROFILE_DIALOG_FIRMWARE_PATH, false);
    pathChooserWidgetLayout->addRow(Tr::tr("Firmware Path"), m_pathChooser);
    toolWidgetLayout->addWidget(pathChooserWidget);

    for (QPushButton *btn : m_pathChooser->findChildren<QPushButton *>()) {
        btn->setAutoDefault(false);
        btn->setDefault(false);
    }

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(Tr::tr("Filter functions..."));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setText(m_settings->value(LAST_PROFILE_DIALOG_FILTER_TEXT, "").toString());
    m_filterEdit->setMaximumWidth(m_filterEdit->fontMetrics().averageCharWidth() * 64);
    toolWidgetLayout->addWidget(m_filterEdit);

    QWidget *toolWidget3 = new QWidget;
    QHBoxLayout *toolWidgetLayout3 = new QHBoxLayout(toolWidget3);
    toolWidgetLayout3->setContentsMargins(0, 0, 0, 0);

    QRadioButton *inclusiveMode = new QRadioButton(Tr::tr("Inclusive"), this);
    inclusiveMode->setChecked(!m_settings->value(LAST_PROFILE_DIALOG_MODE, false).toBool());
    toolWidgetLayout3->addWidget(inclusiveMode);

    m_exclusiveMode = new QRadioButton(Tr::tr("Exclusive"), this);
    m_exclusiveMode->setChecked(m_settings->value(LAST_PROFILE_DIALOG_MODE, false).toBool());
    toolWidgetLayout3->addWidget(m_exclusiveMode);

    toolWidgetLayout->addWidget(toolWidget3);

    connect(m_exclusiveMode, &QRadioButton::toggled, this, [this](bool checked) {
        emit setProfileMode(checked ? 1 : 0); // 1 = exclusive, 0 = inclusive
    });

    emit setProfileMode(m_exclusiveMode->isChecked() ? 1 : 0);

    QPushButton *resetButton = new QPushButton(Tr::tr("Reset"), this);
    resetButton->setAutoDefault(false);
    resetButton->setDefault(false);
    toolWidgetLayout->addWidget(resetButton);

    connect(resetButton, &QPushButton::clicked, this, [this] {
        emit profileReset();
        m_model->clear();
        m_statusLabel->clear();
    });

    m_treeView = new QTreeView(this);
    m_model = new OpenMVProfileModel(this);
    m_treeView->setModel(m_model);
    m_treeView->setSortingEnabled(true);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setRootIsDecorated(true);
    m_treeView->setExpandsOnDoubleClick(true);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    vlayout->addWidget(m_treeView);

    QAction *copyAct = new QAction(tr("Copy"), m_treeView);
    copyAct->setShortcut(QKeySequence::Copy);
    copyAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_treeView->addAction(copyAct);
    connect(copyAct, &QAction::triggered, this, [this] {
        const QString text = selectionToText(m_treeView, Qt::DisplayRole);
        if (!text.isEmpty()) QGuiApplication::clipboard()->setText(text);
    });

    QAction *selectAllAct = new QAction(tr("Select All"), m_treeView);
    selectAllAct->setShortcut(QKeySequence::SelectAll);
    selectAllAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_treeView->addAction(selectAllAct);
    connect(selectAllAct, &QAction::triggered, this, [this]{
        m_treeView->selectAll();
    });

    connect(m_treeView, &QTreeView::customContextMenuRequested, this, [copyAct, selectAllAct] {
        QMenu menu;
        menu.addAction(copyAct);
        menu.addSeparator();
        menu.addAction(selectAllAct);
        menu.exec(QCursor::pos());
    });

    QSortFilterProxyModel *filterModel = new QSortFilterProxyModel(this);
    filterModel->setSourceModel(m_model);
    filterModel->setFilterKeyColumn(OpenMVProfileModel::Address);
    filterModel->setRecursiveFilteringEnabled(true);
    filterModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    filterModel->setSortRole(Qt::UserRole);
    m_treeView->setModel(filterModel);
    restoreEventCountersAndHeaders();

    connect(m_filterEdit, &QLineEdit::textChanged, this, [this, filterModel] (const QString &text) {
        if (text.isEmpty()) {
            filterModel->setFilterRegularExpression(QRegularExpression());
            m_treeView->collapseAll();
        }
        else
        {
            filterModel->setFilterRegularExpression(QRegularExpression(QRegularExpression::escape(text), QRegularExpression::CaseInsensitiveOption));
            m_treeView->expandAll();
        }
    });

    emit m_filterEdit->textChanged(m_filterEdit->text());

    QHeaderView *header = m_treeView->header();
    header->setSectionsClickable(true);
    header->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(header, &QHeaderView::customContextMenuRequested, this, [this, header] (const QPoint& pos) {
        const int section = header->logicalIndexAt(pos);
        if (section >= OpenMVProfileModel::ColumnCount) {
            QString name = selectEventForColumn(section - OpenMVProfileModel::ColumnCount, header->mapToGlobal(pos));
            if (!name.isEmpty()) {
                m_model->setHeaderDataValue(section, name);
            }
        }
    });

    connect(m_pathChooser, &Utils::PathChooser::validChanged, this, [this] (bool validState) {
        if (validState) {
            QString err;

            if (!loadElfFunctionMap(m_pathChooser->filePath().toString(), m_funcNames, m_funcSizes, &err, true, true))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Code Profiler"), err);
            }
        } else {
            m_funcNames.clear();
            m_funcSizes.clear();
        }

        m_model->setSymbolMap(m_funcNames, m_funcSizes);
    });

    QWidget *hWdiget = new QWidget;
    QHBoxLayout *hlayout = new QHBoxLayout(hWdiget);
    hlayout->setContentsMargins(0, 0, 0, 0);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    hlayout->addWidget(m_statusLabel);

    vlayout->addWidget(hWdiget);

    m_columnCount = m_settings->value(LAST_PROFILE_DIALOG_HEADER_GEOMETRY_COLUMN_COUNT, OpenMVProfileModel::ColumnCount).toInt();

    if(m_settings->contains(LAST_PROFILE_DIALOG_GEOMETRY))
    {
        restoreGeometry(m_settings->value(LAST_PROFILE_DIALOG_GEOMETRY).toByteArray());
        header->restoreState(m_settings->value(QString(QStringLiteral(LAST_PROFILE_DIALOG_HEADER_GEOMETRY "_%1")).arg(m_columnCount).toUtf8()).toByteArray());
        m_treeView->sortByColumn(m_settings->value(LAST_PROFILE_DIALOG_HEADER_SORT_COLUMN, OpenMVProfileModel::TotalTicks).toInt(),
                                 static_cast<Qt::SortOrder>(m_settings->value(LAST_PROFILE_DIALOG_HEADER_SORT_ORDER, Qt::DescendingOrder).toInt()));
    }
    else
    {
        resize(800, 600);
        m_treeView->sortByColumn(OpenMVProfileModel::TotalTicks, Qt::DescendingOrder);
    }

#ifndef Q_OS_MAC
    m_styleSheet = QStringLiteral( // https://doc.qt.io/qt-5/stylesheet-examples.html#customizing-qtreeview
    "QTreeView::branch:has-children:!has-siblings:closed,QTreeView::branch:closed:has-children:has-siblings{border-image:none;image:url(:/core/images/branch-closed-%1.png);}"
    "QTreeView::branch:open:has-children:!has-siblings,QTreeView::branch:open:has-children:has-siblings{border-image:none;image:url(:/core/images/branch-open-%1.png);}"
    ).arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("dark") : QStringLiteral("light"));
#endif

    m_highDPIStyleSheet = QString(m_styleSheet).replace(QStringLiteral(".png"), QStringLiteral("_2x.png"));
    m_devicePixelRatio = 0;
}

void OpenMVProfileView::setRecords(const QList<profile_record_t> &records)
{
    if(m_runButton->isChecked() && records.size()) {
        m_model->setRecords(records, m_treeMode->isChecked());

        uint64_t call_count = 0;
        uint64_t total_ticks = 0;
        uint64_t total_cycles = 0;
        uint64_t events_size = 0;
        uint64_t total_events = 0;

        for (const profile_record_t &r : records) {
            call_count += r.call_count;
            total_ticks += r.total_ticks;
            total_cycles += r.total_cycles;
            events_size = qMax(events_size, uint64_t(r.events.size()));
            for (const auto &e : r.events) total_events += e;
        }

        if (m_columnCount != (OpenMVProfileModel::ColumnCount + events_size)) {
            m_columnCount = OpenMVProfileModel::ColumnCount + events_size;
            m_treeView->header()->restoreState(m_settings->value(QString(QStringLiteral(LAST_PROFILE_DIALOG_HEADER_GEOMETRY "_%1")).arg(m_columnCount).toUtf8()).toByteArray());
        }

        if (events_size > 0)
        {
            m_statusLabel->setText(Tr::tr("Functions: %1, Total Calls: %2, Total μs: %3, Total Cycles: %4, Total Events: %5")
                                   .arg(records.size())
                                   .arg(call_count)
                                   .arg(total_ticks)
                                   .arg(total_cycles)
                                   .arg(total_events));
        }
        else
        {
            m_statusLabel->setText(Tr::tr("Functions: %1, Total Calls: %2, Total μs: %3, Total Cycles: %4")
                                   .arg(records.size())
                                   .arg(call_count)
                                   .arg(total_ticks)
                                   .arg(total_cycles));
        }
    }
}

OpenMVProfileView::~OpenMVProfileView()
{
    m_settings->setValue(LAST_PROFILE_DIALOG_GEOMETRY, saveGeometry());
    m_settings->setValue(QString(QStringLiteral(LAST_PROFILE_DIALOG_HEADER_GEOMETRY "_%1")).arg(m_model->columnCount()).toUtf8(), m_treeView->header()->saveState());
    m_settings->setValue(LAST_PROFILE_DIALOG_HEADER_GEOMETRY_COLUMN_COUNT, m_model->columnCount());
    m_settings->setValue(LAST_PROFILE_DIALOG_HEADER_SORT_COLUMN, m_treeView->header()->sortIndicatorSection());
    m_settings->setValue(LAST_PROFILE_DIALOG_HEADER_SORT_ORDER, m_treeView->header()->sortIndicatorOrder());
    m_settings->setValue(LAST_PROFILE_DIALOG_RUN, m_runButton->isChecked());
    m_settings->setValue(LAST_PROFILE_DIALOG_FIRMWARE_PATH, m_pathChooser->filePath().toVariant());
    m_settings->setValue(LAST_PROFILE_DIALOG_FILTER_TEXT, m_filterEdit->text());
    m_settings->setValue(LAST_PROFILE_DIALOG_TREE, m_treeMode->isChecked());
    m_settings->setValue(LAST_PROFILE_DIALOG_MODE, m_exclusiveMode->isChecked());
}

struct Ev { const char *name; int id; const char *group; };

const Ev evs[] = {
    // Core / Architectural
    {"SW_INCR",0x0000,"Core / Architectural"},
    {"LD_RETIRED",0x0006,"Core / Architectural"},
    {"ST_RETIRED",0x0007,"Core / Architectural"},
    {"INST_RETIRED",0x0008,"Core / Architectural"},
    {"UNALIGNED_LDST_RETIRED",0x000F,"Core / Architectural"},
    {"CPU_CYCLES",0x0011,"Core / Architectural"},
    {"MEM_ACCESS",0x0013,"Core / Architectural"},
    {"CHAIN",0x001E,"Core / Architectural"},
    {"STALL",0x003C,"Core / Architectural"},

    // Exceptions
    {"EXC_TAKEN",0x0009,"Exceptions"},
    {"EXC_RETURN",0x000A,"Exceptions"},

    // Branches & Flow
    {"PC_WRITE_RETIRED",0x000C,"Branches && Flow"},
    {"BR_IMMED_RETIRED",0x000D,"Branches && Flow"},
    {"BR_RETURN_RETIRED",0x000E,"Branches && Flow"},
    {"BR_RETIRED",0x0021,"Branches && Flow"},
    {"BR_MIS_PRED_RETIRED",0x0022,"Branches && Flow"},

    // Stalls
    {"STALL_FRONTEND",0x0023,"Stalls"},
    {"STALL_BACKEND",0x0024,"Stalls"},

    // Caches & Memory
    {"L1I_CACHE_REFILL",0x0001,"Caches && Memory"},
    {"L1D_CACHE_REFILL",0x0003,"Caches && Memory"},
    {"L1D_CACHE",0x0004,"Caches && Memory"},
    {"L1I_CACHE",0x0014,"Caches && Memory"},
    {"L1D_CACHE_WB",0x0015,"Caches && Memory"},
    {"BUS_ACCESS",0x0019,"Caches && Memory"},
    {"BUS_CYCLES",0x001D,"Caches && Memory"},
    {"LL_CACHE_RD",0x0036,"Caches && Memory"},
    {"LL_CACHE_MISS_RD",0x0037,"Caches && Memory"},
    {"L1D_CACHE_MISS_RD",0x0039,"Caches && Memory"},
    {"L1D_CACHE_RD",0x0040,"Caches && Memory"},
    {"MEMORY_ERROR",0x001A,"Caches && Memory"},

    // Loop & Security
    {"LE_RETIRED",0x0100,"Loop && Security"},
    {"LE_CANCEL",0x0108,"Loop && Security"},
    {"SE_CALL_S",0x0114,"Loop && Security"},
    {"SE_CALL_NS",0x0115,"Loop && Security"},

    // DWT
    {"DWT_CMPMATCH0",0x0118,"DWT"},
    {"DWT_CMPMATCH1",0x0119,"DWT"},
    {"DWT_CMPMATCH2",0x011A,"DWT"},
    {"DWT_CMPMATCH3",0x011B,"DWT"},
    {"DWT_CMPMATCH4",0x011C,"DWT"},
    {"DWT_CMPMATCH5",0x011D,"DWT"},
    {"DWT_CMPMATCH6",0x011E,"DWT"},
    {"DWT_CMPMATCH7",0x011F,"DWT"},

    // MVE
    {"MVE_INST_RETIRED",0x0200,"MVE"},
    {"MVE_FP_RETIRED",0x0204,"MVE"},
    {"MVE_FP_HP_RETIRED",0x0208,"MVE"},
    {"MVE_FP_SP_RETIRED",0x020C,"MVE"},
    {"MVE_FP_MAC_RETIRED",0x0214,"MVE"},
    {"MVE_INT_RETIRED",0x0224,"MVE"},
    {"MVE_INT_MAC_RETIRED",0x0228,"MVE"},
    {"MVE_LDST_RETIRED",0x0238,"MVE"},
    {"MVE_LD_RETIRED",0x023C,"MVE"},
    {"MVE_ST_RETIRED",0x0240,"MVE"},
    {"MVE_LDST_CONTIG_RETIRED",0x0244,"MVE"},
    {"MVE_LD_CONTIG_RETIRED",0x0248,"MVE"},
    {"MVE_ST_CONTIG_RETIRED",0x024C,"MVE"},
    {"MVE_LDST_NONCONTIG_RETIRED",0x0250,"MVE"},
    {"MVE_LD_NONCONTIG_RETIRED",0x0254,"MVE"},
    {"MVE_ST_NONCONTIG_RETIRED",0x0258,"MVE"},
    {"MVE_LDST_MULTI_RETIRED",0x025C,"MVE"},
    {"MVE_LD_MULTI_RETIRED",0x0260,"MVE"},
    {"MVE_ST_MULTI_RETIRED",0x0264,"MVE"},
    {"MVE_LDST_UNALIGNED_RETIRED",0x028C,"MVE"},
    {"MVE_LD_UNALIGNED_RETIRED",0x0290,"MVE"},
    {"MVE_ST_UNALIGNED_RETIRED",0x0294,"MVE"},
    {"MVE_LDST_UNALIGNED_NONCONTIG_RETIRED",0x0298,"MVE"},
    {"MVE_VREDUCE_RETIRED",0x02A0,"MVE"},
    {"MVE_VREDUCE_FP_RETIRED",0x02A4,"MVE"},
    {"MVE_VREDUCE_INT_RETIRED",0x02A8,"MVE"},
    {"MVE_PRED",0x02B8,"MVE"},
    {"MVE_STALL",0x02CC,"MVE"},
    {"MVE_STALL_RESOURCE",0x02CD,"MVE"},
    {"MVE_STALL_RESOURCE_MEM",0x02CE,"MVE"},
    {"MVE_STALL_RESOURCE_FP",0x02CF,"MVE"},
    {"MVE_STALL_RESOURCE_INT",0x02D0,"MVE"},
    {"MVE_STALL_BREAK",0x02D3,"MVE"},
    {"MVE_STALL_DEPENDENCY",0x02D4,"MVE"},

    // TCM / Trace / CTI
    {"ITCM_ACCESS",0x4007,"TCM / Trace / CTI"},
    {"DTCM_ACCESS",0x4008,"TCM / Trace / CTI"},
    {"TRCEXTOUT0",0x4010,"TCM / Trace / CTI"},
    {"TRCEXTOUT1",0x4011,"TCM / Trace / CTI"},
    {"TRCEXTOUT2",0x4012,"TCM / Trace / CTI"},
    {"TRCEXTOUT3",0x4013,"TCM / Trace / CTI"},
    {"CTI_TRIGOUT4",0x4018,"TCM / Trace / CTI"},
    {"CTI_TRIGOUT5",0x4019,"TCM / Trace / CTI"},
    {"CTI_TRIGOUT6",0x401A,"TCM / Trace / CTI"},
    {"CTI_TRIGOUT7",0x401B,"TCM / Trace / CTI"},

    // ECC
    {"ECC_ERR",0xC000,"ECC"},
    {"ECC_ERR_MBIT",0xC001,"ECC"},
    {"ECC_ERR_DCACHE",0xC010,"ECC"},
    {"ECC_ERR_ICACHE",0xC011,"ECC"},
    {"ECC_ERR_MBIT_DCACHE",0xC012,"ECC"},
    {"ECC_ERR_MBIT_ICACHE",0xC013,"ECC"},
    {"ECC_ERR_DTCM",0xC020,"ECC"},
    {"ECC_ERR_ITCM",0xC021,"ECC"},
    {"ECC_ERR_MBIT_DTCM",0xC022,"ECC"},
    {"ECC_ERR_MBIT_ITCM",0xC023,"ECC"},

    // Prefetcher
    {"PF_LINEFILL",0xC100,"Prefetcher"},
    {"PF_CANCEL",0xC101,"Prefetcher"},
    {"PF_DROP_LINEFILL",0xC102,"Prefetcher"},

    // No-Write-Allocate
    {"NWAMODE_ENTER",0xC200,"No-Write-Allocate"},
    {"NWAMODE",0xC201,"No-Write-Allocate"},

    // Bus Interfaces
    {"SAHB_ACCESS",0xC300,"Bus Interfaces"},
    {"PAHB_ACCESS",0xC301,"Bus Interfaces"},
    {"AXI_WRITE_ACCESS",0xC302,"Bus Interfaces"},
    {"AXI_READ_ACCESS",0xC303,"Bus Interfaces"},

    // DoS timeout
    {"DOSTIMEOUT_DOUBLE",0xC400,"DoS Timeout"},
    {"DOSTIMEOUT_TRIPLE",0xC401,"DoS Timeout"},

    // CDE
    {"CDE_INST_RETIRED",0xC402,"CDE"},
    {"CDE_CX1_INST_RETIRED",0xC404,"CDE"},
    {"CDE_CX2_INST_RETIRED",0xC406,"CDE"},
    {"CDE_CX3_INST_RETIRED",0xC408,"CDE"},
    {"CDE_VCX1_INST_RETIRED",0xC40A,"CDE"},
    {"CDE_VCX2_INST_RETIRED",0xC40C,"CDE"},
    {"CDE_VCX3_INST_RETIRED",0xC40E,"CDE"},
    {"CDE_VCX1_VEC_INST_RETIRED",0xC410,"CDE"},
    {"CDE_VCX2_VEC_INST_RETIRED",0xC412,"CDE"},
    {"CDE_VCX3_VEC_INST_RETIRED",0xC414,"CDE"},
    {"CDE_PRED",0xC416,"CDE"},
    {"CDE_STALL",0xC417,"CDE"},
    {"CDE_STALL_RESOURCE",0xC418,"CDE"},
    {"CDE_STALL_DEPENDENCY",0xC419,"CDE"},
    {"CDE_STALL_CUSTOM",0xC41A,"CDE"},
    {"CDE_STALL_OTHER",0xC41B,"CDE"},

    // Prefetcher
    {"PF_LF_LA_1",0xC41C,"Prefetcher"},
    {"PF_LF_LA_2",0xC41D,"Prefetcher"},
    {"PF_LF_LA_3",0xC41E,"Prefetcher"},
    {"PF_LF_LA_4",0xC41F,"Prefetcher"},
    {"PF_LF_LA_5",0xC420,"Prefetcher"},
    {"PF_LF_LA_6",0xC421,"Prefetcher"},
    {"PF_BUFFER_FULL",0xC422,"Prefetcher"},
    {"PF_BUFFER_MISS",0xC423,"Prefetcher"},
    {"PF_BUFFER_HIT",0xC424,"Prefetcher"},
};

QString OpenMVProfileView::selectEventForColumn(int section, const QPoint& globalPos)
{
    // Group events
    QHash<QString, QList<const Ev *> > grouped;
    QList<QString> groupOrder;

    for (const Ev &e : evs) {
        const QString g = QLatin1String(e.group);
        if (!grouped.contains(g)) groupOrder.append(g);
        grouped[g].append(&e);
    }

    // Build menu
    QMenu menu(this);
    QActionGroup *ag = new QActionGroup(&menu);
    ag->setExclusive(true);

    const QString currentName = m_settings->value(QString(QStringLiteral(LAST_PROFILE_DIALOG_EVENT "_%1")).arg(section).toUtf8()).toString();

    for (const QString &groupName : groupOrder) {
        QMenu *sub = menu.addMenu(groupName);
        for (const Ev *e : grouped[groupName]) {
            QAction* act = sub->addAction(QLatin1String(e->name));
            act->setData(e->id);
            act->setCheckable(true);
            ag->addAction(act);
            if (currentName == QLatin1String(e->name)) act->setChecked(true);
        }
    }

    QAction *picked = menu.exec(globalPos);
    if (!picked || !picked->isCheckable()) return QString();

    m_settings->setValue(QString(QStringLiteral(LAST_PROFILE_DIALOG_EVENT "_%1")).arg(section).toUtf8(), picked->text());
    emit setEventCounter(section, picked->data().toInt());
    return picked->text();
}

void OpenMVProfileView::restoreEventCountersAndHeaders()
{
    for (const QString &key : m_settings->allKeys())
    {
        if (!key.startsWith(QStringLiteral(LAST_PROFILE_DIALOG_EVENT "_"))) continue;

        bool ok = false;
        int col = key.mid(QStringLiteral(LAST_PROFILE_DIALOG_EVENT "_").size()).toInt(&ok);
        if (!ok) continue;

        const QString chosen = m_settings->value(key.toUtf8()).toString();
        if (chosen.isEmpty()) continue;

        int eventId = -1;
        for (const auto &e : evs) {
            if (QLatin1String(e.name) == chosen) {
                eventId = e.id;
                break;
            }
        }

        if (eventId != -1) {
            m_model->setHeaderDataValue(col + OpenMVProfileModel::ColumnCount, chosen);
            emit setEventCounter(col, eventId);
        }
    }
}

// We have to do this because Qt does not update the icons when switching between
// a non-high dpi screen and a high-dpi screen.
void OpenMVProfileView::paintEvent(QPaintEvent *event)
{
    qreal ratio = devicePixelRatioF();
    if (!qFuzzyCompare(ratio, m_devicePixelRatio))
    {
        m_devicePixelRatio = ratio;
        setStyleSheet(qFuzzyCompare(1.0, ratio) ? m_styleSheet : m_highDPIStyleSheet); // reload icons
    }

    QDialog::paintEvent(event);
}

} // namespace Internal
} // namespace OpenMV
