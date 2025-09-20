/***********************************************************************
 *
 * Copyright (C) 2012, 2024 Graeme Gott <graeme@gottcode.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***********************************************************************/

#include "scene_model.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocumentFragment>
#include <QTimer>

//-----------------------------------------------------------------------------
// OutlineItem
//-----------------------------------------------------------------------------

OutlineItem::OutlineItem(OutlineItem *parent)
    : m_parentItem(parent)
{
}

OutlineItem::~OutlineItem()
{
    qDeleteAll(m_childItems);
}

void OutlineItem::appendChild(OutlineItem *item)
{
    m_childItems.append(item);
}

OutlineItem *OutlineItem::child(int row)
{
    if (row < 0 || row >= m_childItems.size())
        return nullptr;
    return m_childItems.at(row);
}

int OutlineItem::childCount() const
{
    return m_childItems.count();
}

int OutlineItem::row() const
{
    if (m_parentItem)
        return m_parentItem->m_childItems.indexOf(const_cast<OutlineItem*>(this));

    return 0;
}

OutlineItem *OutlineItem::parentItem()
{
    return m_parentItem;
}

//-----------------------------------------------------------------------------
// SceneModel
//-----------------------------------------------------------------------------

SceneModel::SceneModel(QTextEdit* document, QObject* parent) :
	QAbstractItemModel(parent),
	m_document(document),
    m_updatesBlocked(false),
	m_autoUpdateEnabled(true)
{
    m_rootItem = new OutlineItem(nullptr);
	connect(m_document->document(), &QTextDocument::contentsChanged, this, &SceneModel::scheduleRebuild);
    rebuildOutline();
}

SceneModel::~SceneModel()
{
    delete m_rootItem;
}

int SceneModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QVariant SceneModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if (role != Qt::DisplayRole && role != Qt::UserRole)
        return QVariant();

    OutlineItem *item = static_cast<OutlineItem*>(index.internalPointer());

    if (role == Qt::UserRole) {
        return item->block_number;
    }

    return item->text;
}

Qt::ItemFlags SceneModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return QAbstractItemModel::flags(index);
}

QVariant SceneModel::headerData(int section, Qt::Orientation orientation,
                               int role) const
{
    Q_UNUSED(section);
    Q_UNUSED(orientation);
    Q_UNUSED(role);
    return QVariant();
}

QModelIndex SceneModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    OutlineItem *parentItem;

    if (!parent.isValid())
        parentItem = m_rootItem;
    else
        parentItem = static_cast<OutlineItem*>(parent.internalPointer());

    OutlineItem *childItem = parentItem->child(row);
    if (childItem)
        return createIndex(row, column, childItem);
    return QModelIndex();
}

QModelIndex SceneModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return QModelIndex();

    OutlineItem *childItem = static_cast<OutlineItem*>(index.internalPointer());
    OutlineItem *parentItem = childItem->parentItem();

    if (parentItem == m_rootItem)
        return QModelIndex();

    return createIndex(parentItem->row(), 0, parentItem);
}

int SceneModel::rowCount(const QModelIndex &parent) const
{
    OutlineItem *parentItem;
    if (parent.column() > 0)
        return 0;

    if (!parent.isValid())
        parentItem = m_rootItem;
    else
        parentItem = static_cast<OutlineItem*>(parent.internalPointer());

    return parentItem->childCount();
}

void SceneModel::setUpdatesBlocked(bool blocked)
{
    m_updatesBlocked = blocked;
    if (!m_updatesBlocked) {
        rebuildOutline();
    }
}

void SceneModel::setAutoUpdate(bool enabled)
{
	m_autoUpdateEnabled = enabled;
}

void SceneModel::scheduleRebuild()
{
    if (!m_updatesBlocked && m_autoUpdateEnabled) {
        // Using a single shot timer to avoid rebuilding the model on every single character change
        // which can be expensive for large documents.
        QTimer::singleShot(100, this, &SceneModel::rebuildOutline);
    }
}

void SceneModel::rebuildOutline()
{
    if (m_updatesBlocked) return;

    beginResetModel();
    setupModelData();
    endResetModel();
}

void SceneModel::setupModelData()
{
    delete m_rootItem;
    m_rootItem = new OutlineItem(nullptr);

    QList<OutlineItem*> parents;
    parents << m_rootItem;

    for (QTextBlock block = m_document->document()->begin(); block.isValid(); block = block.next()) {
        int headingLevel = block.blockFormat().property(QTextFormat::UserProperty).toInt();

        if (headingLevel > 0 && !block.text().isEmpty()) {
            if (headingLevel > parents.last()->level) {
                // Child of the current item. Nothing to do with `parents` list yet.
            } else {
                // Sibling or uncle. Pop until we find the parent.
                while (headingLevel <= parents.last()->level && parents.size() > 1) {
                    parents.pop_back();
                }
            }
            
            OutlineItem* parentItem = parents.last();
            OutlineItem *item = new OutlineItem(parentItem);
            item->level = headingLevel;
            item->text = block.text();
            item->block_number = block.blockNumber();
            parentItem->appendChild(item);

            parents.push_back(item);
        }
    }
}

QModelIndex SceneModel::findScene(const QTextCursor& cursor) const
{
    QTextBlock currentBlock = cursor.block();
    while(currentBlock.isValid()) {
        int headingLevel = currentBlock.blockFormat().property(QTextFormat::UserProperty).toInt();
        if (headingLevel > 0 && !currentBlock.text().isEmpty()) {
            return findSceneRecursive(currentBlock, QModelIndex());
        }
        currentBlock = currentBlock.previous();
    }
    return QModelIndex();
}

QModelIndex SceneModel::findSceneRecursive(const QTextBlock& block, const QModelIndex& parent) const
{
    for (int i = 0; i < rowCount(parent); ++i) {
        QModelIndex index = this->index(i, 0, parent);
        OutlineItem* item = getItem(index);
        if (item && item->block_number == block.blockNumber()) {
            return index;
        }
        if (rowCount(index) > 0) {
            QModelIndex found = findSceneRecursive(block, index);
            if (found.isValid()) {
                return found;
            }
        }
    }
    return QModelIndex();
}

OutlineItem* SceneModel::getItem(const QModelIndex &index) const
{
    if (index.isValid()) {
        OutlineItem *item = static_cast<OutlineItem*>(index.internalPointer());
        if (item) {
            return item;
        }
    }
    return m_rootItem;
}

OutlineItem* SceneModel::findNextItemInOutline(OutlineItem* item) const
{
    if (!item) return nullptr;

    OutlineItem* current = item;
    OutlineItem* parent = current->parentItem();

    while (parent) {
        int nextRow = current->row() + 1;
        if (nextRow < parent->childCount()) {
            return parent->child(nextRow); // Found next sibling
        }
        // No next sibling, move up the tree
        current = parent;
        parent = current->parentItem();
    }

    return nullptr; // No next item found in the entire tree
}

#include <QDebug>

void SceneModel::cut(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }

    OutlineItem* item = getItem(index);
    if (!item || item == m_rootItem) {
        return;
    }

    // 1. Find text range
    QTextBlock startBlock = m_document->document()->findBlockByNumber(item->block_number);
    if (!startBlock.isValid()) {
        return;
    }

    int startPos = startBlock.position();
    int endPos = -1;

    OutlineItem* nextItem = findNextItemInOutline(item);
    if (nextItem) {
        QTextBlock nextBlock = m_document->document()->findBlockByNumber(nextItem->block_number);
        if (nextBlock.isValid()) {
            endPos = nextBlock.position();
        }
    }

    if (endPos == -1) {
        // No next item, so cut to the end of the document
        endPos = m_document->document()->characterCount() - 1;
    }

    qDebug() << "Cutting from" << startPos << "to" << endPos;

    // 2. Store data for pasting
    m_cutNodes.clear();
    m_cutHtml.clear();
    populateCutNodes(item);

    QTextCursor cursor(m_document->document());
    cursor.setPosition(startPos);
    cursor.setPosition(endPos, QTextCursor::KeepAnchor);
    m_cutHtml = cursor.selection().toHtml();

    // 3. Remove text (this will trigger rebuild)
    cursor.removeSelectedText();
}

void SceneModel::populateCutNodes(OutlineItem* item)
{
    if (!item) return;
    m_cutNodes.append({item->text, item->level});
    foreach (OutlineItem* child, item->children()) {
        populateCutNodes(child);
    }
}
