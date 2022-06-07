/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt Design Tooling
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

#include "checkablefiletreeitem.h"

using namespace Utils;

namespace QmlProjectManager {

CheckableFileTreeItem::CheckableFileTreeItem(const FilePath &filePath)
    :QStandardItem(filePath.toString())
{
    Qt::ItemFlags itemFlags = flags();
    if (!isDir())
        itemFlags |= Qt::ItemIsUserCheckable;
    itemFlags &= ~(Qt::ItemIsEditable | Qt::ItemIsSelectable);
    setFlags(itemFlags);
}

const FilePath CheckableFileTreeItem::toFilePath() const
{
    return FilePath::fromString(text());
}

bool CheckableFileTreeItem::isFile() const
{
    return FilePath::fromString(text()).isFile();
}

bool CheckableFileTreeItem::isDir() const
{
    return FilePath::fromString(text()).isDir();
}

void CheckableFileTreeItem::setChecked(bool checked)
{
    this->checked = checked;
}

bool CheckableFileTreeItem::isChecked() const
{
    return this->checked;
}

} //QmlProjectManager