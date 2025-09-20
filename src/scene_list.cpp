/***********************************************************************
 *
 * Copyright (C) 2012, 2014, 2018, 2019, 2024 Graeme Gott <graeme@gottcode.org>
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

#include "scene_list.h"

#include "action_manager.h"
#include "document.h"
#include "scene_model.h"

#include <QAction>
#include <QApplication>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QTreeView>
#include <QMimeData>
#include <QMouseEvent>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStack>
#include <QTextBlock>
#include <QTextEdit>
#include <QToolButton>

#include <algorithm>
#include <cmath>

//-----------------------------------------------------------------------------

SceneList::SceneList(QWidget* parent) :
	QFrame(parent),
	m_document(0),
    m_isInteractingWithView(false),
	m_resizing(false)
{
	m_width = qBound(0, QSettings().value("SceneList/Width", (int)std::lround(3.5 * logicalDpiX())).toInt(), maximumWidth());

	// Configure sidebar
	setFrameStyle(QFrame::Panel | QFrame::Raised);
	setAutoFillBackground(true);
	setPalette(QApplication::palette());

	// Create button to show scenes
	m_show_button = new QToolButton(this);
	m_show_button->setAutoRaise(true);
	m_show_button->setArrowType(Qt::RightArrow);
	m_show_button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::MinimumExpanding);
	connect(m_show_button, &QToolButton::clicked, this, &SceneList::showScenes);

	// Create button to hide scenes
	m_hide_button = new QToolButton(this);
	m_hide_button->setAutoRaise(true);
	m_hide_button->setArrowType(Qt::LeftArrow);
	m_hide_button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::MinimumExpanding);
	connect(m_hide_button, &QToolButton::clicked, this, &SceneList::hideScenes);

	// Create action for toggling scenes
	m_toggle_action = new QAction(tr("Toggle Outline"), this);
	m_toggle_action->setShortcut(tr("Shift+F4"));
	connect(m_toggle_action, &QAction::changed, this, &SceneList::updateShortcuts);
	connect(m_toggle_action, &QAction::triggered, this, &SceneList::toggleScenes);
	ActionManager::instance()->addAction("ToggleScenes", m_toggle_action);
	updateShortcuts();
	parent->addAction(m_toggle_action);

	QAction* refresh_action = new QAction(tr("Refresh Outline"), this);
	refresh_action->setShortcut(tr("F5"));
	connect(refresh_action, &QAction::triggered, this, &SceneList::refreshOutline);
	addAction(refresh_action);

	// Create scene view
	m_filter_model = new QSortFilterProxyModel(this);
	m_filter_model->setFilterCaseSensitivity(Qt::CaseInsensitive);
	m_filter_model->setRecursiveFilteringEnabled(true);
	connect(m_filter_model, &QSortFilterProxyModel::modelAboutToBeReset, this, &SceneList::saveExpandedState);
	connect(m_filter_model, &QSortFilterProxyModel::modelReset, this, &SceneList::restoreExpandedState);

	m_scenes = new QTreeView(this);
	m_scenes->setAlternatingRowColors(true);
	m_scenes->setDragDropMode(QAbstractItemView::NoDragDrop);
	m_scenes->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scenes->setSelectionMode(QAbstractItemView::SingleSelection);
	m_scenes->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	m_scenes->setModel(m_filter_model);
	m_scenes->setExpandsOnDoubleClick(false);
	connect(m_scenes, &QTreeView::doubleClicked, this, &SceneList::toggleExpansion);
    m_scenes->setHeaderHidden(true);
	m_scenes->show();
	setFocusProxy(m_scenes);
	setFocusPolicy(Qt::StrongFocus);

	m_scenes->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_scenes, &QTreeView::customContextMenuRequested, this, &SceneList::onCustomContextMenu);

	m_contextMenu = new QMenu(this);
	m_cutAction = m_contextMenu->addAction(tr("Cut"));
	connect(m_cutAction, &QAction::triggered, this, &SceneList::cutSelectedScene);

	// Create filter widget
	m_filter = new QLineEdit(this);
	m_filter->setPlaceholderText(tr("Filter"));
	connect(m_filter, &QLineEdit::textChanged, this, &SceneList::setFilter);

	// Create widget for resizing
	m_resizer = new QFrame(this);
	m_resizer->setCursor(Qt::SizeHorCursor);
	m_resizer->setFrameStyle(QFrame::VLine | QFrame::Sunken);
	m_resizer->setToolTip(tr("Resize outline"));

	// Lay out widgets
	QGridLayout* layout = new QGridLayout(this);
	layout->setColumnStretch(1, 1);
	layout->setRowStretch(0, 1);
	layout->addWidget(m_show_button, 0, 0, 2, 1);
	layout->addWidget(m_hide_button, 0, 1, 2, 1);
	layout->addWidget(m_scenes, 0, 2);
	layout->addWidget(m_filter, 1, 2);
	layout->addWidget(m_resizer, 0, 3, 2, 1);

	// Start collapsed
	hideScenes();
}

//-----------------------------------------------------------------------------

SceneList::~SceneList()
{
	QSettings().setValue("SceneList/Width", m_width);
}

//-----------------------------------------------------------------------------

bool SceneList::scenesVisible() const
{
	return m_scenes->isVisible();
}

//-----------------------------------------------------------------------------

void SceneList::setDocument(Document* document)
{
	if (m_document) {
		disconnect(m_document->text(), &QTextEdit::cursorPositionChanged, this, &SceneList::selectCurrentScene);
	}
	m_document = 0;

	m_scenes->clearSelection();
	m_filter->clear();
	m_filter_model->setSourceModel(document->sceneModel());

	m_document = document;
	if (m_document && scenesVisible()) {
		m_document->sceneModel()->setUpdatesBlocked(false);
		connect(m_document->text(), &QTextEdit::cursorPositionChanged, this, &SceneList::selectCurrentScene);
		selectCurrentScene();
	}
}

//-----------------------------------------------------------------------------

void SceneList::hideScenes()
{
	if (m_document) {
		m_document->sceneModel()->setAutoUpdate(true);
		disconnect(m_scenes->selectionModel(), &QItemSelectionModel::currentChanged, this, &SceneList::sceneSelected);
		m_document->sceneModel()->setUpdatesBlocked(true);
		disconnect(m_document->text(), &QTextEdit::cursorPositionChanged, this, &SceneList::selectCurrentScene);
	}

	m_show_button->show();

	m_hide_button->hide();
	m_scenes->hide();
	m_filter->hide();
	m_resizer->hide();

	setMinimumWidth(0);
	setMaximumWidth(minimumSizeHint().width());

	m_filter->clear();

	hide();

	if (m_document) {
		m_document->text()->setFocus();
	}
}

//-----------------------------------------------------------------------------

void SceneList::showScenes()
{
	show();

	m_hide_button->show();
	m_scenes->show();
	m_filter->show();
	m_resizer->show();

	m_show_button->hide();

	setMinimumWidth(std::lround(1.5 * logicalDpiX()));
	setMaximumWidth(m_width);

	if (m_document) {
		m_document->sceneModel()->setAutoUpdate(false);
		m_document->sceneModel()->rebuildOutline();
		m_document->sceneModel()->setUpdatesBlocked(false);
		connect(m_document->text(), &QTextEdit::cursorPositionChanged, this, &SceneList::selectCurrentScene);
		selectCurrentScene();
		connect(m_scenes->selectionModel(), &QItemSelectionModel::currentChanged, this, &SceneList::sceneSelected);
	}

	m_scenes->setFocus();
}

//-----------------------------------------------------------------------------

void SceneList::mouseMoveEvent(QMouseEvent* event)
{
	if (m_resizing) {
		int delta = event->pos().x() - m_mouse_current.x();
		m_mouse_current = event->pos();

		m_width += delta;
		m_width = std::max(minimumWidth(), m_width);
		setMaximumWidth(m_width);

		event->accept();
	} else {
		QFrame::mouseMoveEvent(event);
	}
}

//-----------------------------------------------------------------------------

void SceneList::mousePressEvent(QMouseEvent* event)
{
	if (scenesVisible() &&
			(event->button() == Qt::LeftButton) &&
			(event->pos().x() >= m_resizer->mapToParent(m_resizer->rect().topLeft()).x())) {
		m_width = width();
		m_mouse_current = event->pos();
		m_resizing = true;

		event->accept();
	} else {
		QFrame::mousePressEvent(event);
	}
}

//-----------------------------------------------------------------------------

void SceneList::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton) {
		m_resizing = false;
	}
	QFrame::mouseReleaseEvent(event);
}

//-----------------------------------------------------------------------------

void SceneList::resizeEvent(QResizeEvent* event)
{
	m_scenes->scrollTo(m_scenes->currentIndex());
	QFrame::resizeEvent(event);
}

//-----------------------------------------------------------------------------

void SceneList::sceneSelected(const QModelIndex& index)
{
	if (!m_document || !scenesVisible()) {
		return;
	}

	if (index.isValid()) {
        m_isInteractingWithView = true;

		int block_number = m_filter_model->mapToSource(index).data(Qt::UserRole).toInt();
		QTextBlock block = m_document->text()->document()->findBlockByNumber(block_number);
		QTextCursor cursor = m_document->text()->textCursor();
		cursor.setPosition(block.position());
		m_document->text()->setTextCursor(cursor);
		m_document->centerCursor(true);

        m_isInteractingWithView = false;
	}
}

//-----------------------------------------------------------------------------

void SceneList::selectCurrentScene()
{
    if (m_isInteractingWithView) {
        return;
    }

	if (!m_document || !scenesVisible()) {
		return;
	}

	QModelIndex index = m_document->sceneModel()->findScene(m_document->text()->textCursor());
	if (index.isValid()) {
		index = m_filter_model->mapFromSource(index);

		m_scenes->selectionModel()->blockSignals(true);
		m_scenes->setCurrentIndex(index);
		m_scenes->selectionModel()->blockSignals(false);

		m_scenes->scrollTo(index);
	}
}
//-----------------------------------------------------------------------------

void SceneList::setFilter(const QString& filter)
{
	m_filter_model->setFilterFixedString(filter);
}

//-----------------------------------------------------------------------------

void SceneList::toggleScenes()
{
	if (scenesVisible()) {
		hideScenes();
	} else {
		showScenes();
	}
}

//-----------------------------------------------------------------------------

void SceneList::refreshOutline()
{
	if (m_document) {
		m_document->sceneModel()->rebuildOutline();
	}
}

//-----------------------------------------------------------------------------

void SceneList::updateShortcuts()
{
	QKeySequence shortcut = ActionManager::instance()->action("ToggleScenes")->shortcut();
	m_toggle_action->setShortcut(shortcut);
	m_show_button->setToolTip(tr("Show outline (%1)").arg(shortcut.toString(QKeySequence::NativeText)));
	m_hide_button->setToolTip(tr("Hide outline (%1)").arg(shortcut.toString(QKeySequence::NativeText)));
}

void SceneList::saveExpandedState()
{
    m_expandedBlockNumbers.clear();
    QModelIndex parent;
    QStack<QModelIndex> parents;
    parents.push(parent);

    while (!parents.isEmpty()) {
        parent = parents.pop();
        int rowCount = m_filter_model->rowCount(parent);
        for (int i = 0; i < rowCount; ++i) {
            QModelIndex index = m_filter_model->index(i, 0, parent);
            if (m_scenes->isExpanded(index)) {
                QModelIndex sourceIndex = m_filter_model->mapToSource(index);
                m_expandedBlockNumbers.insert(sourceIndex.data(Qt::UserRole).toInt());
            }
            if (m_filter_model->hasChildren(index)) {
                parents.push(index);
            }
        }
    }
}

void SceneList::restoreExpandedState()
{
    QModelIndex parent;
    QStack<QModelIndex> parents;
    parents.push(parent);

    while (!parents.isEmpty()) {
        parent = parents.pop();
        int rowCount = m_filter_model->rowCount(parent);
        for (int i = 0; i < rowCount; ++i) {
            QModelIndex index = m_filter_model->index(i, 0, parent);
            QModelIndex sourceIndex = m_filter_model->mapToSource(index);
            if (m_expandedBlockNumbers.contains(sourceIndex.data(Qt::UserRole).toInt())) {
                m_scenes->expand(index);
            }
            if (m_filter_model->hasChildren(index)) {
                parents.push(index);
            }
        }
    }
}

//-----------------------------------------------------------------------------

void SceneList::toggleExpansion(const QModelIndex& index)
{
	if (index.isValid()) {
		m_scenes->setExpanded(index, !m_scenes->isExpanded(index));
	}
}

//-----------------------------------------------------------------------------

void SceneList::onCustomContextMenu(const QPoint& point)
{
	QModelIndex index = m_scenes->indexAt(point);
	if (index.isValid()) {
		m_contextMenu->exec(m_scenes->viewport()->mapToGlobal(point));
	}
}

//-----------------------------------------------------------------------------

void SceneList::cutSelectedScene()
{
	QModelIndex index = m_scenes->currentIndex();
	if (!index.isValid()) {
		return;
	}

	if (m_document) {
		SceneModel* model = m_document->sceneModel();
		model->cut(m_filter_model->mapToSource(index));
	}
}
