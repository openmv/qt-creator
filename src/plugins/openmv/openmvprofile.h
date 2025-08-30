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

#ifndef OPENMVPROFILE_H
#define OPENMVPROFILE_H

#include <QtCore>
#include <QtWidgets>

#include <openmv/openmvpluginio.h>
#include <utils/qtcsettings.h>
#include <utils/pathchooser.h>

namespace OpenMV {
namespace Internal {

class OpenMVProfileModel : public QAbstractItemModel
{
    Q_OBJECT

public:

    explicit OpenMVProfileModel(QObject *parent = nullptr) : QAbstractItemModel(parent) {}
    ~OpenMVProfileModel() override { clearNodes(); }

    enum Column {
        Address = 0,
        Calls,
        MinTicks,
        MaxTicks,
        TotalTicks,
        AvgTicks,
        AvgCycles,
        Percentage,
        ColumnCount
    };

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override { Q_UNUSED(parent); return ColumnCount + eventSize_; }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    void setRecords(const QList<profile_record_t> &records, bool tree);
    void clear();
    void setSymbolMap(const QHash<qulonglong, QString>  m, const QMap<qulonglong, qulonglong> &s) { symMap_ = m; symMapSizes_ = s; }
    void setHeaderData(int section, const QString &name) { headerData_[section] = name; }

private:

    struct Node {
        profile_record_t rec;
        Node *parent = nullptr;
        QList<Node *> children;
        int rootRow = -1;
        int rowInParent() const { return parent ? parent->children.indexOf(const_cast<Node *>(this)) : rootRow; }
    };

    QList<profile_record_t> records_;
    QList<Node *> roots_;
    QList<Node *> allNodes_;
    QHash<uint32_t, Node *> byAddress_;
    QHash<qulonglong, QString> symMap_;
    QMap<qulonglong, qulonglong> symMapSizes_;
    int lastSortColumn_ = -1;
    Qt::SortOrder lastSortOrder_ = Qt::AscendingOrder;
    qulonglong totalTicks_ = 0;
    int eventSize_ = 0;
    QMap<int, QString> headerData_;

    void updateRecords(const QList<profile_record_t> &newRecords, bool tree);
    void clearNodes();
    QModelIndex indexForNode(Node *n, int column = 0) const;
    void rebuildTree(bool tree);
    qulonglong functionStartFor(qulonglong pc) const;
};

class OpenMVProfileView : public QDialog
{
    Q_OBJECT

public:

    explicit OpenMVProfileView(Utils::QtcSettings *settings, QWidget *parent = Q_NULLPTR);
    ~OpenMVProfileView();

public slots:

    void setRecords(const QList<profile_record_t> &records);

signals:

    void setProfileMode(int mode);
    void setEventCounter(int event_num, int event_type);
    void profileReset();

protected:

    void paintEvent(QPaintEvent *event) override;

private:

    QString selectEventForColumn(int section, const QPoint& globalPos);
    void restoreEventCountersAndHeaders();

    OpenMVProfileModel *m_model;
    Utils::QtcSettings *m_settings;
    QToolButton *m_runButton;
    QRadioButton *m_treeMode;
    Utils::PathChooser *m_pathChooser;
    QLineEdit *m_filterEdit;
    QRadioButton *m_exclusiveMode;
    QTreeView *m_treeView;
    QLabel *m_statusLabel;
    uint64_t m_columnCount;
    QHash<qulonglong, QString> m_funcNames;
    QMap<qulonglong, qulonglong> m_funcSizes;
    QString m_styleSheet, m_highDPIStyleSheet;
    qreal m_devicePixelRatio;
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVPROFILE_H
