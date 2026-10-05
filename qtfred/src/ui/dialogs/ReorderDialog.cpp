#include "ui/dialogs/ReorderDialog.h"
#include "mission/commands/FredCommands.h"
#include "ui/Theme.h"
#include "ui/util/DialogUndo.h"
#include "ui_ReorderDialog.h"

#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>

#include <algorithm>

namespace fso::fred::dialogs {

ReorderDialog::ReorderDialog(FredView* parent, EditorViewport* viewport) :
	QDialog(parent),
	_fredView(parent),
	_viewport(viewport),
	ui(new Ui::ReorderDialog()),
	_model(new ReorderDialogModel(this, viewport))
{
	ui->setupUi(this);
	util::installMainStackUndoShortcuts(this, _fredView->mainUndoStack());

	using Type = ReorderDialogModel::Type;
	_tabs = {
		{Type::Ships, ui->shipList, ui->shipMoveTopBtn, ui->shipMoveUpBtn, ui->shipMoveDownBtn, ui->shipMoveBottomBtn},
		{Type::Wings, ui->wingList, ui->wingMoveTopBtn, ui->wingMoveUpBtn, ui->wingMoveDownBtn, ui->wingMoveBottomBtn},
		{Type::Props, ui->propList, ui->propMoveTopBtn, ui->propMoveUpBtn, ui->propMoveDownBtn, ui->propMoveBottomBtn},
		{Type::WaypointLists, ui->waypointList, ui->waypointMoveTopBtn, ui->waypointMoveUpBtn, ui->waypointMoveDownBtn, ui->waypointMoveBottomBtn},
		{Type::JumpNodes, ui->jumpNodeList, ui->jumpNodeMoveTopBtn, ui->jumpNodeMoveUpBtn, ui->jumpNodeMoveDownBtn, ui->jumpNodeMoveBottomBtn},
		{Type::CoordinatePoints, ui->coordinatePointList, ui->coordinatePointMoveTopBtn, ui->coordinatePointMoveUpBtn, ui->coordinatePointMoveDownBtn, ui->coordinatePointMoveBottomBtn},
	};

	for (const auto& tab : _tabs) {
		setupTab(tab);
	}

	// Undo/redo reorders the mission behind our back, so rebuild from the model
	// whenever the mission changes rather than only after our own move buttons.
	connect(_viewport->editor, &Editor::missionChanged, this, [this] {
		for (const auto& tab : _tabs) {
			rebuildList(tab);
			updateButtons(tab);
		}
	});
}

ReorderDialog::~ReorderDialog() = default;

void ReorderDialog::changeEvent(QEvent* e)
{
	if (e->type() == QEvent::ActivationChange && isActiveWindow())
		_fredView->undoGroup()->setActiveStack(_fredView->mainUndoStack());
	QDialog::changeEvent(e);
}

void ReorderDialog::setupTab(const Tab& tab)
{
	fso::fred::bindCustomIcon(tab.top, CustomIcon::MoveToTop);
	fso::fred::bindStandardIcon(tab.up, QStyle::SP_ArrowUp);
	fso::fred::bindStandardIcon(tab.down, QStyle::SP_ArrowDown);
	fso::fred::bindCustomIcon(tab.bottom, CustomIcon::MoveToBottom);

	using MoveKind = ReorderDialogModel::MoveKind;
	connect(tab.top, &QAbstractButton::clicked, this, [this, tab] { move(tab, MoveKind::Top); });
	connect(tab.up, &QAbstractButton::clicked, this, [this, tab] { move(tab, MoveKind::Up); });
	connect(tab.down, &QAbstractButton::clicked, this, [this, tab] { move(tab, MoveKind::Down); });
	connect(tab.bottom, &QAbstractButton::clicked, this, [this, tab] { move(tab, MoveKind::Bottom); });

	// Ctrl/Shift+click select several items, which the buttons then move together
	tab.list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	connect(tab.list, &QListWidget::itemSelectionChanged, this, [tab] { updateButtons(tab); });

	if (tab.type == ReorderDialogModel::Type::Ships) {
		tab.list->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(tab.list, &QWidget::customContextMenuRequested, this, &ReorderDialog::showShipContextMenu);
	}

	rebuildList(tab);
	// Select the first item so the move buttons start in a sensible state.
	if (tab.list->count() > 0)
		tab.list->setCurrentRow(0);
	updateButtons(tab);
}

void ReorderDialog::rebuildList(const Tab& tab)
{
	QSignalBlocker blocker(tab.list);

	const SCP_vector<int> prevRows = selectedRows(tab);
	const int prevCurrent = tab.list->currentRow();
	QSet<QString> prevNames;
	for (int row : prevRows)
		prevNames.insert(tab.list->item(row)->text());

	tab.list->clear();
	for (const auto& name : _model->getItemNames(tab.type)) {
		tab.list->addItem(QString::fromStdString(name));
	}

	const int count = tab.list->count();
	if (count == 0)
		return;

	// The selection follows the items, so it moves with them on undo and redo too (names are unique
	// within each type). If none of them is left, fall back to the previous rows, clamped.
	SCP_vector<int> rows;
	for (int row = 0; row < count; ++row) {
		if (prevNames.contains(tab.list->item(row)->text()))
			rows.push_back(row);
	}
	if (rows.empty()) {
		for (int row : prevRows)
			rows.push_back(std::min(row, count - 1));
		if (rows.empty() && prevCurrent >= 0)
			rows.push_back(std::min(prevCurrent, count - 1));
		std::sort(rows.begin(), rows.end());
		rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
	}
	selectRows(tab, rows);
	if (!rows.empty())
		tab.list->scrollToItem(tab.list->item(rows.front())); // only scrolls if it's out of view
}

SCP_vector<int> ReorderDialog::selectedRows(const Tab& tab)
{
	SCP_vector<int> rows;
	for (const auto& index : tab.list->selectionModel()->selectedRows())
		rows.push_back(index.row());
	std::sort(rows.begin(), rows.end());
	rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
	return rows;
}

void ReorderDialog::selectRows(const Tab& tab, const SCP_vector<int>& rows)
{
	tab.list->clearSelection();
	for (int row : rows) {
		if (auto* item = tab.list->item(row))
			item->setSelected(true);
	}
	if (!rows.empty()) {
		// move the current item without disturbing the selection just made
		tab.list->setCurrentRow(rows.front(), QItemSelectionModel::NoUpdate);
	}
}

void ReorderDialog::updateButtons(const Tab& tab)
{
	const SCP_vector<int> rows = selectedRows(tab);
	const int count = tab.list->count();
	const int n = static_cast<int>(rows.size());

	// a selection can move up unless it is already a block at the top, and likewise down
	bool canUp = false;
	bool canDown = false;
	for (int k = 0; k < n; ++k) {
		canUp = canUp || rows[k] != k;
		canDown = canDown || rows[k] != count - n + k;
	}

	tab.top->setEnabled(canUp);
	tab.up->setEnabled(canUp);
	tab.down->setEnabled(canDown);
	tab.bottom->setEnabled(canDown);
}

void ReorderDialog::move(const Tab& tab, ReorderDialogModel::MoveKind kind)
{
	const SCP_vector<int> rows = selectedRows(tab);
	if (rows.empty())
		return;

	SCP_vector<ReorderDialogModel::Step> steps;
	const SCP_vector<int> newRows = _model->moveItems(tab.type, rows, kind, steps);
	if (steps.empty())
		return;

	// The moves are already applied, so the command's first redo() is a no-op.
	_fredView->mainUndoStack()->push(new ReorderCommand(static_cast<int>(tab.type), std::move(steps), _viewport));

	// moveItems() fired missionChanged(), which rebuilt the lists; select where the items went
	{
		QSignalBlocker blocker(tab.list);
		selectRows(tab, newRows);
	}
	tab.list->scrollToItem(tab.list->item(kind == ReorderDialogModel::MoveKind::Down ||
		kind == ReorderDialogModel::MoveKind::Bottom ? newRows.back() : newRows.front()));
	updateButtons(tab);
}

void ReorderDialog::showShipContextMenu(const QPoint& pos)
{
	const auto it = std::find_if(_tabs.begin(), _tabs.end(),
		[](const Tab& t) { return t.type == ReorderDialogModel::Type::Ships; });
	if (it == _tabs.end())
		return;
	const Tab& tab = *it;
	const int row = tab.list->row(tab.list->itemAt(pos));
	const SCP_vector<int> wingRows = ReorderDialogModel::getSameWingShipRows(row);

	QMenu menu(this);
	auto* selectWing = menu.addAction(tr("Select Wing"));
	selectWing->setEnabled(!wingRows.empty());
	connect(selectWing, &QAction::triggered, this, [&tab, &wingRows] {
		selectRows(tab, wingRows); // itemSelectionChanged updates the buttons
	});
	menu.exec(tab.list->viewport()->mapToGlobal(pos));
}

} // namespace fso::fred::dialogs
