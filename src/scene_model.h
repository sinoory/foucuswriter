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

#ifndef SCENE_MODEL_H
#define SCENE_MODEL_H

#include <QAbstractItemModel>
#include <QList>
#include <QVariant>

class QTextBlock;
class QTextCursor;
class QTextEdit;

class OutlineItem
{
public:
    explicit OutlineItem(OutlineItem *parent = nullptr);
    ~OutlineItem();

    void appendChild(OutlineItem *child);

    OutlineItem *child(int row);
    int childCount() const;
    int row() const;
    const QList<OutlineItem*>& children() const { return m_childItems; }
    OutlineItem *parentItem();

    int level = 0;
    QString text;
    int block_number = -1;

private:
    QList<OutlineItem*> m_childItems;
    OutlineItem *m_parentItem;
};

struct CutNode
{
    QString text;
    int level;
};


class SceneModel : public QAbstractItemModel
{
	Q_OBJECT

public:
	SceneModel(QTextEdit* document, QObject* parent = 0);
	~SceneModel();

	QModelIndex findScene(const QTextCursor& cursor) const;
	void setUpdatesBlocked(bool blocked);

	bool hasCutNodes() const;

	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	Qt::ItemFlags flags(const QModelIndex& index) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
	QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& index) const override;
	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

public slots:
    void rebuildOutline();
	void setAutoUpdate(bool enabled);
	void cut(const QModelIndex& index);
	void pasteIn(const QModelIndex& index);
	void pasteAfter(const QModelIndex& index);
	void pasteBefore(const QModelIndex& index);

private slots:
	void scheduleRebuild();

private:
    void setupModelData();
    OutlineItem *getItem(const QModelIndex &index) const;
    QModelIndex findSceneRecursive(const QTextBlock& block, const QModelIndex& parent) const;
    OutlineItem* findNextItemInOutline(OutlineItem* item) const;
    void populateCutNodes(OutlineItem* item);

private:
    OutlineItem *m_rootItem;
	QTextEdit* m_document;
	bool m_updatesBlocked;
	bool m_autoUpdateEnabled;
    QString m_cutHtml;
    QList<CutNode> m_cutNodes;
};

#endif
