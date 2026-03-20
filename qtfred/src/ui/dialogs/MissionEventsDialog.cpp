#include "MissionEventsDialog.h"
#include "ui_MissionEventsDialog.h"
#include "ui/util/SignalBlockers.h"
#include "ui/dialogs/General/ImagePickerDialog.h"

#include "mission/util.h"

#include <sound/audiostr.h>
#include <localization/localize.h>

#include <QInputDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QDebug>
#include <QKeyEvent>
#include <mission/missionmessage.h>
#include <mission/missionparse.h>
#include <parse/parselo.h>
#include <parse/sexp.h>

void parse_event(mission* pm);

namespace fso::fred::dialogs {

MissionEventsDialog::MissionEventsDialog(QWidget* parent, EditorViewport* viewport) :
	QDialog(parent),
	SexpTreeEditorInterface({ TreeFlags::LabeledRoot, TreeFlags::RootDeletable, TreeFlags::RootEditable, TreeFlags::AnnotationsAllowed }),
	  ui(new Ui::MissionEventsDialog()), _viewport(viewport)
{
	ui->setupUi(this);

	ui->eventTree->initializeEditor(viewport->editor, this);
	ui->eventTree->clear_tree();
	ui->eventTree->_model.post_load();

	// Construct the model with a direct reference to the shared sexp tree model
	_model = std::make_unique<MissionEventsDialogModel>(this, _viewport, ui->eventTree->_model);

	// Connect model signals to widget operations
	connect(_model.get(), &MissionEventsDialogModel::treeCleared, this, [this]() {
		ui->eventTree->clear();
	});

	connect(_model.get(), &MissionEventsDialogModel::subtreeAdded, this,
		[this](const SCP_string& name, NodeImage image, int formula) {
			auto h = ui->eventTree->insert(name.c_str(), image);
			h->setData(0, sexp_tree_view::FormulaDataRole, formula);
			ui->eventTree->add_sub_tree(formula, h);
		});

	connect(_model.get(), &MissionEventsDialogModel::defaultRootBuilt, this,
		[this](const SCP_string& name, int after_root_formula, int new_formula) {
			// Find the item to insert after (if any)
			QTreeWidgetItem* afterItem = nullptr;
			if (after_root_formula >= 0) {
				const int n = ui->eventTree->topLevelItemCount();
				for (int i = 0; i < n; ++i) {
					auto* it = ui->eventTree->topLevelItem(i);
					if (it && it->data(0, sexp_tree_view::FormulaDataRole).toInt() == after_root_formula) {
						afterItem = it;
						break;
					}
				}
			}

			// Insert the root item
			auto* root = ui->eventTree->insert(name.c_str(), NodeImage::ROOT, nullptr, afterItem);
			root->setData(0, sexp_tree_view::FormulaDataRole, new_formula);

			// Build the visual subtree from the model's tree_nodes
			ui->eventTree->add_sub_tree(new_formula, root);

			ui->eventTree->clearSelection();
			root->setSelected(true);
		});

	connect(_model.get(), &MissionEventsDialogModel::rootSelected, this, [this](int formula) {
		const int n = ui->eventTree->topLevelItemCount();
		for (int i = 0; i < n; ++i) {
			auto* it = ui->eventTree->topLevelItem(i);
			if (it && it->data(0, sexp_tree_view::FormulaDataRole).toInt() == formula) {
				ui->eventTree->setCurrentItem(it);
				break;
			}
		}
	});

	connect(_model.get(), &MissionEventsDialogModel::eventDeleteRequested, this, [this]() {
		// Walk to root before deleting
		auto item = ui->eventTree->currentItem();
		while (item && item->parent() != nullptr) {
			item = item->parent();
		}
		if (item) {
			ui->eventTree->setCurrentItem(item);
			ui->eventTree->deleteCurrentItem();
		}
	});

	connect(_model.get(), &MissionEventsDialogModel::topLevelIndexRequested, this,
		[this](int formula, int desired_index) {
			const int n = ui->eventTree->topLevelItemCount();
			for (int i = 0; i < n; ++i) {
				auto* it = ui->eventTree->topLevelItem(i);
				if (it && it->data(0, sexp_tree_view::FormulaDataRole).toInt() == formula) {
					int cur = ui->eventTree->indexOfTopLevelItem(it);
					if (cur != desired_index) {
						ui->eventTree->takeTopLevelItem(cur);
						ui->eventTree->insertTopLevelItem(desired_index, it);
					}
					break;
				}
			}
		});

	connect(_model.get(), &MissionEventsDialogModel::annotationApplied, this,
		[this](int node_index, const SCP_string& note, int r, int g, int b, bool has_color) {
			if (node_index < 0 || node_index >= static_cast<int>(ui->eventTree->_model.tree_nodes.size()))
				return;
			auto* it = tree_item_handle(ui->eventTree->_model.tree_nodes[node_index]);
			if (!it)
				return;
			const QString q = QString::fromStdString(note);
			it->setData(0, sexp_tree_view::NoteRole, q);
			it->setToolTip(0, q);
			it->setData(0, sexp_tree_view::BgColorRole, QColor(r, g, b));
			it->setBackground(0, has_color ? QBrush(QColor(r, g, b)) : QBrush());
			sexp_tree_view::applyVisuals(it);
		});

	initMessageWidgets();

	initEventWidgets();
}

MissionEventsDialog::~MissionEventsDialog() = default;

void MissionEventsDialog::initEventWidgets() {
	initEventTeams();

	ui->miniHelpBox->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	ui->helpBox->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

	// connect the sexp tree stuff
	connect(ui->eventTree, &sexp_tree_view::modified, this, [this]() { _model->setModified(); });
	connect(ui->eventTree, &sexp_tree_view::rootNodeDeleted, this, &MissionEventsDialog::rootNodeDeleted);
	connect(ui->eventTree, &sexp_tree_view::rootNodeRenamed, this, &MissionEventsDialog::rootNodeRenamed);
	connect(ui->eventTree, &sexp_tree_view::rootNodeFormulaChanged, this, &MissionEventsDialog::rootNodeFormulaChanged);
	connect(ui->eventTree, &sexp_tree_view::miniHelpChanged, this, [this](const QString& help) { ui->miniHelpBox->setText(help); });
	connect(ui->eventTree, &sexp_tree_view::helpChanged, this, [this](const QString& help) { ui->helpBox->setPlainText(help); });
	connect(ui->eventTree, &sexp_tree_view::selectedRootChanged, this, [this](int formula) { MissionEventsDialog::rootNodeSelectedByFormula(formula); });

	connect(ui->eventTree, &sexp_tree_view::nodeAnnotationChanged, this, [this](void* h, const QString& note) {
		// Translate QTreeWidgetItem* to node index
		int node_index = ui->eventTree->get_node(static_cast<QTreeWidgetItem*>(h));
		if (node_index >= 0) {
			SCP_string text = note.toUtf8().constData();
			_model->setNodeAnnotation(node_index, text);
		}
	});

	connect(ui->eventTree, &sexp_tree_view::nodeBgColorChanged, this, [this](void* h, const QColor& c) {
		int node_index = ui->eventTree->get_node(static_cast<QTreeWidgetItem*>(h));
		if (node_index >= 0) {
			_model->setNodeBgColor(node_index, c.red(), c.green(), c.blue(), c.isValid());
		}
	});

	connect(ui->eventTree, &sexp_tree_view::rootOrderChanged, this, [this] {
		SCP_vector<int> order;
		order.reserve(ui->eventTree->topLevelItemCount());
		for (int i = 0; i < ui->eventTree->topLevelItemCount(); ++i) {
			auto* it = ui->eventTree->topLevelItem(i);
			order.push_back(it->data(0, sexp_tree_view::FormulaDataRole).toInt());
		}
		_model->reorderByRootFormulaOrder(order);
		m_last_message_node = -1;
	});

	_model->setCurrentlySelectedEvent(-1);

	updateEventUi();
}

int MissionEventsDialog::getRootReturnType() const
{
	return OPR_NULL;
}

void MissionEventsDialog::accept()
{
	if (ui->advancedEditorButton->isChecked() && !applyAdvancedEditorText()) {
		return;
	}

	// If apply() returns true, close the dialog
	if (_model->apply()) {
		QDialog::accept();
	}
	// else: validation failed, don't close
}

void MissionEventsDialog::reject()
{
	// Asks the user if they want to save changes, if any
	// If they do, it runs _model->apply() and returns the success value
	// If they don't, it runs _model->reject() and returns true
	if (rejectOrCloseHandler(this, _model.get(), _viewport)) {
		QDialog::reject(); // actually close
	}
	// else: do nothing, don't close
}

SCP_vector<SCP_string> MissionEventsDialog::getMessages()
{
	SCP_vector<SCP_string> out;
	const auto& msgs = _model->getMessageList();
	out.reserve(msgs.size());
	for (const auto& m : msgs) {
		out.emplace_back(m.name);
	}
	return out;
}

bool MissionEventsDialog::hasDefaultMessageParamter()
{
	return !_model->getMessageList().empty();
}

void MissionEventsDialog::closeEvent(QCloseEvent* e)
{
	reject();
	e->ignore(); // Don't let the base class close the window
}

void MissionEventsDialog::initMessageWidgets() {
	initHeadCombo();
	initWaveFilenames();
	initPersonas();
	initMessageTeams();

	initMessageList();

	ui->messageName->setMaxLength(NAME_LENGTH - 1);

	if (auto* le = ui->aniCombo->lineEdit()) {
		connect(le, &QLineEdit::editingFinished, this, &MissionEventsDialog::on_aniCombo_editingFinished);
	}

	if (auto* le = ui->waveCombo->lineEdit()) {
		connect(le, &QLineEdit::editingFinished, this, &MissionEventsDialog::on_waveCombo_editingFinished);
	}

	updateMessageUi();
}

void MissionEventsDialog::rootNodeDeleted(int node) {
	_model->deleteRootNode(node);
}

void MissionEventsDialog::rootNodeRenamed(int node) {
	QTreeWidgetItem* item = nullptr;
	for (int i = 0; i < ui->eventTree->topLevelItemCount(); ++i) {
		auto* it = ui->eventTree->topLevelItem(i);
		if (it && it->data(0, sexp_tree_view::FormulaDataRole).toInt() == node) {
			item = it;
			break;
		}
	}
	if (!item)
		return;

	SCP_string newText = item->text(0).toUtf8().constData();

	_model->renameRootNode(node, newText);
}

void MissionEventsDialog::rootNodeFormulaChanged(int old, int node) {
	_model->changeRootNodeFormula(old, node);
}

void MissionEventsDialog::rootNodeSelectedByFormula(int formula) {
	_model->setCurrentlySelectedEventByFormula(formula);
	if (ui->advancedEditorButton->isChecked()) {
		enterAdvancedEditorMode();
	}
	updateEventUi();
}

void MissionEventsDialog::on_advancedEditorButton_toggled(bool checked)
{
	if (checked) {
		enterAdvancedEditorMode();
		return;
	}

	leaveAdvancedEditorMode();
}

void MissionEventsDialog::enterAdvancedEditorMode()
{
	if (!_model->eventIsValid()) {
		ui->advancedSexpText->clear();
		ui->eventTree->setVisible(false);
		ui->advancedSexpText->setVisible(true);
		return;
	}

	SCP_string serialized_sexp;
	const auto formula = _model->getFormula();
	const auto sexp_root = ui->eventTree->_model.save_tree(formula);
	convert_sexp_to_string(serialized_sexp, sexp_root, SEXP_SAVE_MODE);
	free_sexp2(sexp_root);

	const auto cur_event_index = _model->getCurrentlySelectedEvent();
	const auto& event = _model->getEventList()[cur_event_index];

	SCP_string event_text = "$Formula: " + serialized_sexp + "\n";
	if (!event.name.empty()) {
		event_text += "+Name: " + event.name + "\n";
	}

	event_text += "+Repeat Count: " + std::to_string(event.repeat_count) + "\n";
	if (event.flags & MEF_USING_TRIGGER_COUNT) {
		event_text += "+Trigger Count: " + std::to_string(event.trigger_count) + "\n";
	}

	event_text += "+Interval: " + std::to_string(event.interval) + "\n";
	if (event.score != 0) {
		event_text += "+Score: " + std::to_string(event.score) + "\n";
	}
	if (event.chain_delay >= 0) {
		event_text += "+Chained: " + std::to_string(event.chain_delay) + "\n";
	}
	if (!event.objective_text.empty()) {
		event_text += "+Objective: " + event.objective_text + "\n";
	}
	if (!event.objective_key_text.empty()) {
		event_text += "+Objective key: " + event.objective_key_text + "\n";
	}
	if (event.team >= 0) {
		event_text += "+Team: " + std::to_string(event.team) + "\n";
	}

	if (event.mission_log_flags != 0) {
		event_text += "+Event Log Flags: (";
		for (int i = 0; i < MAX_MISSION_EVENT_LOG_FLAGS; ++i) {
			const auto bit = 1 << i;
			if (event.mission_log_flags & bit) {
				event_text += " \"";
				event_text += Mission_event_log_flags[i];
				event_text += "\"";
			}
		}
		event_text += " )\n";
	}

	ui->advancedSexpText->setPlainText(QString::fromStdString(event_text));
	ui->eventTree->setVisible(false);
	ui->advancedSexpText->setVisible(true);
}

bool MissionEventsDialog::applyAdvancedEditorText()
{
	if (!_model->eventIsValid()) {
		return true;
	}

	SCP_string wrapped_text = "#Events\n";
	wrapped_text += ui->advancedSexpText->toPlainText().trimmed().toStdString();
	wrapped_text += "\n#Goals\n";

	SCP_vector<char> parse_buffer(wrapped_text.begin(), wrapped_text.end());
	parse_buffer.push_back('\0');

	const auto old_event_count = Mission_events.size();

	mission_event parsed_event;
	SCP_string parse_error;
	pause_parse();
	reset_parse(parse_buffer.data());
	try {
		required_string("#Events");
		parse_event(nullptr);
		required_string("#Goals");
	} catch (const parse::ParseException& e) {
		parse_error = e.what();
	}
	unpause_parse();

	if (!parse_error.empty() || Mission_events.size() <= old_event_count) {
		while (Mission_events.size() > old_event_count) {
			if (Mission_events.back().formula >= 0) {
				free_sexp2(Mission_events.back().formula);
			}
			Mission_events.pop_back();
		}
		if (parse_error.empty()) {
			parse_error = "Could not parse event data.";
		}
		QMessageBox::warning(this, tr("Invalid Event Data"), QString::fromStdString(parse_error));
		return false;
	}

	parsed_event = Mission_events.back();
	Mission_events.pop_back();

	if (parsed_event.formula < 0) {
		QMessageBox::warning(this, tr("Invalid Event Data"), tr("Event formula failed to parse."));
		return false;
	}

	const auto old_formula = _model->getFormula();
	const auto new_formula = ui->eventTree->_model.load_sub_tree(parsed_event.formula, true, "true");
	free_sexp2(parsed_event.formula);

	QTreeWidgetItem* root_item = nullptr;
	for (int i = 0; i < ui->eventTree->topLevelItemCount(); ++i) {
		auto* it = ui->eventTree->topLevelItem(i);
		if (it && it->data(0, sexp_tree_view::FormulaDataRole).toInt() == old_formula) {
			root_item = it;
			break;
		}
	}

	if (root_item == nullptr) {
		QMessageBox::warning(this, tr("SEXP Update Failed"), tr("Could not locate the current event root node."));
		return false;
	}

	while (root_item->childCount() > 0) {
		delete root_item->takeChild(0);
	}

	root_item->setData(0, sexp_tree_view::FormulaDataRole, new_formula);
	ui->eventTree->add_sub_tree(new_formula, root_item);
	ui->eventTree->setCurrentItem(root_item);

	const auto cur_event_idx = _model->getCurrentlySelectedEvent();
	if (parsed_event.name != _model->getEventList()[cur_event_idx].name) {
		root_item->setText(0, QString::fromStdString(parsed_event.name));
		_model->renameEvent(cur_event_idx, parsed_event.name);
	}

	_model->changeRootNodeFormula(old_formula, new_formula);
	_model->setCurrentlySelectedEventByFormula(new_formula);

	_model->setRepeatCount(parsed_event.repeat_count);
	if (parsed_event.flags & MEF_USING_TRIGGER_COUNT) {
		_model->setTriggerCount(parsed_event.trigger_count);
	} else {
		_model->setTriggerCount(1);
	}
	_model->setIntervalTime(parsed_event.interval);
	_model->setEventScore(parsed_event.score);
	_model->setChained(parsed_event.chain_delay >= 0);
	if (parsed_event.chain_delay >= 0) {
		_model->setChainDelay(parsed_event.chain_delay);
	}
	_model->setEventDirectiveText(parsed_event.objective_text);
	_model->setEventDirectiveKeyText(parsed_event.objective_key_text);
	_model->setEventTeam(parsed_event.team);
	_model->setLogTrue((parsed_event.mission_log_flags & MLF_SEXP_TRUE) != 0);
	_model->setLogFalse((parsed_event.mission_log_flags & MLF_SEXP_FALSE) != 0);
	_model->setLogLogPrevious((parsed_event.mission_log_flags & MLF_STATE_CHANGE) != 0);
	_model->setLogAlwaysFalse((parsed_event.mission_log_flags & MLF_SEXP_KNOWN_FALSE) != 0);
	_model->setLogFirstRepeat((parsed_event.mission_log_flags & MLF_FIRST_REPEAT_ONLY) != 0);
	_model->setLogLastRepeat((parsed_event.mission_log_flags & MLF_LAST_REPEAT_ONLY) != 0);
	_model->setLogFirstTrigger((parsed_event.mission_log_flags & MLF_FIRST_TRIGGER_ONLY) != 0);
	_model->setLogLastTrigger((parsed_event.mission_log_flags & MLF_LAST_TRIGGER_ONLY) != 0);

	updateEventUi();
	_model->setModified();
	return true;
}

void MissionEventsDialog::leaveAdvancedEditorMode()
{
	if (!applyAdvancedEditorText()) {
		QSignalBlocker blocker(ui->advancedEditorButton);
		ui->advancedEditorButton->setChecked(true);
		return;
	}

	ui->advancedSexpText->setVisible(false);
	ui->eventTree->setVisible(true);
}

void MissionEventsDialog::initMessageList() {
	rebuildMessageList();

	_model->setCurrentlySelectedMessage(_model->getMessageList().empty() ? -1 : 0);
}

void MissionEventsDialog::rebuildMessageList() {
	// Block signals so that the current item index isn't overwritten by this
	QSignalBlocker blocker(ui->messageList);

	const int curRow = _model->getCurrentlySelectedMessage();

	ui->messageList->clear();
	for (auto& msg : _model->getMessageList()) {
		auto item = new QListWidgetItem(msg.name, ui->messageList);
		ui->messageList->addItem(item);
	}

	if (curRow >= 0 && curRow < ui->messageList->count()) {
		ui->messageList->setCurrentRow(curRow);
	}
}

void MissionEventsDialog::updateEventUi() {
	util::SignalBlockers blockers(this);

	updateEventMoveButtons();

	if (!_model->eventIsValid()) {
		ui->repeatCountBox->setValue(1);
		ui->triggerCountBox->setValue(1);
		ui->intervalTimeBox->setValue(1);
		ui->chainDelayBox->setValue(0);
		ui->teamCombo->setCurrentIndex(0); // was MAX_TVT_TEAMS for none?
		ui->editDirectiveText->setText("");
		ui->editDirectiveKeypressText->setText("");

		ui->repeatCountBox->setEnabled(false);
		ui->triggerCountBox->setEnabled(false);
		ui->intervalTimeBox->setEnabled(false);
		ui->chainDelayBox->setEnabled(false);
		ui->teamCombo->setEnabled(false);
		ui->editDirectiveText->setEnabled(false);
		ui->editDirectiveKeypressText->setEnabled(false);
		return;
	}

	ui->teamCombo->setCurrentIndex(ui->teamCombo->findData(_model->getEventTeam()));

	ui->repeatCountBox->setValue(_model->getRepeatCount());
	ui->triggerCountBox->setValue(_model->getTriggerCount());
	ui->intervalTimeBox->setValue(_model->getIntervalTime());
	ui->scoreBox->setValue(_model->getEventScore());
	if (_model->getChained()) {
		ui->chainedCheckBox->setChecked(true);
		ui->chainDelayBox->setValue(_model->getChainDelay());
		ui->chainDelayBox->setEnabled(true);
	} else {
		ui->chainedCheckBox->setChecked(false);
		ui->chainDelayBox->setValue(0);
		ui->chainDelayBox->setEnabled(false);
	}

	ui->editDirectiveText->setText(QString::fromStdString(_model->getEventDirectiveText()));
	ui->editDirectiveKeypressText->setText(QString::fromStdString(_model->getEventDirectiveKeyText()));

	ui->repeatCountBox->setEnabled(true);
	ui->triggerCountBox->setEnabled(true);

	if ((_model->getRepeatCount() > 1) || (_model->getRepeatCount() < 0) ||
		(_model->getTriggerCount() > 1) || (_model->getTriggerCount() < 0)) {
		ui->intervalTimeBox->setEnabled(true);
	} else {
		ui->intervalTimeBox->setValue(_model->getIntervalTime());
		ui->intervalTimeBox->setEnabled(false);
	}

	ui->scoreBox->setEnabled(true);
	ui->chainedCheckBox->setEnabled(true);
	ui->editDirectiveText->setEnabled(true);
	ui->editDirectiveKeypressText->setEnabled(true);
	ui->teamCombo->setEnabled(_model->getMissionIsMultiTeam());

	// handle event log flags
	ui->checkLogTrue->setChecked(_model->getLogTrue());
	ui->checkLogFalse->setChecked(_model->getLogFalse());
	ui->checkLogPrevious->setChecked(_model->getLogLogPrevious());
	ui->checkLogAlwaysFalse->setChecked(_model->getLogAlwaysFalse());
	ui->checkLogFirstRepeat->setChecked(_model->getLogFirstRepeat());
	ui->checkLogLastRepeat->setChecked(_model->getLogLastRepeat());
	ui->checkLogFirstTrigger->setChecked(_model->getLogFirstTrigger());
	ui->checkLogLastTrigger->setChecked(_model->getLogLastTrigger());
}

void MissionEventsDialog::updateEventMoveButtons()
{
	auto* cur = ui->eventTree->currentItem();

	const bool isRoot = (cur && !cur->parent());
	const int count = ui->eventTree->topLevelItemCount();

	bool canUp = false, canDown = false;

	if (isRoot && count > 1) {
		const int idx = ui->eventTree->indexOfTopLevelItem(cur);
		canUp = (idx > 0);
		canDown = (idx >= 0 && idx < count - 1);
	}

	ui->eventUpBtn->setEnabled(canUp);
	ui->eventDownBtn->setEnabled(canDown);
}

void MissionEventsDialog::initHeadCombo() {
	auto list = _model->getHeadAniList();

	ui->aniCombo->clear();

	for (auto& head : list) {
		ui->aniCombo->addItem(QString().fromStdString(head));
	}
}

void MissionEventsDialog::initWaveFilenames() {
	auto list = _model->getWaveList();

	ui->waveCombo->clear();

	for (auto& wave : list) {
		ui->waveCombo->addItem(QString().fromStdString(wave));
	}
}

void MissionEventsDialog::initPersonas() {
	auto list = _model->getPersonaList();

	ui->personaCombo->clear();

	for (auto&& [name, id] : _model->getPersonaList()) {
		ui->personaCombo->addItem(QString::fromStdString(name), id);
	}
}

void MissionEventsDialog::initMessageTeams() {
	auto list = _model->getTeamList();

	ui->messageTeamCombo->clear();

	for (const auto& team : list) {
		ui->messageTeamCombo->addItem(QString::fromStdString(team.first), team.second);
	}

}

void MissionEventsDialog::initEventTeams()
{
	auto list = _model->getTeamList();

	ui->teamCombo->clear();

	for (const auto& team : list) {
		ui->teamCombo->addItem(QString::fromStdString(team.first), team.second);
	}
}

void MissionEventsDialog::updateMessageUi()
{
	bool enable = true;

	if (!_model->messageIsValid()) {
		enable = false;

		ui->messageName->setText("");
		ui->messageContent->setPlainText("");
		ui->aniCombo->setEditText("");
		ui->personaCombo->setCurrentIndex(-1);
		ui->waveCombo->setEditText("");
		ui->messageTeamCombo->setCurrentIndex(-1);
		ui->btnMsgNote->setText("Add Note");
	} else {
		ui->messageName->setText(QString().fromStdString(_model->getMessageName()));
		ui->messageContent->setPlainText(QString().fromStdString(_model->getMessageText()));
		ui->aniCombo->setEditText(QString().fromStdString(_model->getMessageAni()));
		ui->personaCombo->setCurrentIndex(ui->personaCombo->findData(_model->getMessagePersona()));
		ui->waveCombo->setEditText(QString().fromStdString(_model->getMessageWave()));
		ui->messageTeamCombo->setCurrentIndex(ui->messageTeamCombo->findData(_model->getMessageTeam()));
		if (_model->getMessageNote().empty()) {
			ui->btnMsgNote->setText("Add Note");
		} else {
			ui->btnMsgNote->setText("Edit Note");
		}
	}

	ui->messageName->setEnabled(enable);
	ui->messageContent->setEnabled(enable);
	ui->aniCombo->setEnabled(enable);
	ui->btnAniBrowse->setEnabled(enable);
	ui->btnBrowseWave->setEnabled(enable);
	ui->btnWavePlay->setEnabled(enable);
	ui->waveCombo->setEnabled(enable);
	ui->btnDeleteMsg->setEnabled(enable);
	ui->personaCombo->setEnabled(enable);
	ui->messageTeamCombo->setEnabled(enable && _model->getMissionIsMultiTeam());
	ui->btnMsgNote->setEnabled(enable);

	updateMessageMoveButtons();
}

void MissionEventsDialog::updateMessageMoveButtons()
{
	const int count = ui->messageList->count();
	const int row = ui->messageList->currentItem() ? ui->messageList->row(ui->messageList->currentItem()) : -1;

	const bool hasSel = (row >= 0);
	const bool canUp = hasSel && row > 0;
	const bool canDown = hasSel && row < count - 1;

	ui->msgUpBtn->setEnabled(canUp);
	ui->msgDownBtn->setEnabled(canDown);
}

SCP_vector<int> MissionEventsDialog::read_root_formula_order(sexp_tree_view* tree)
{
	SCP_vector<int> order;
	order.reserve(tree->topLevelItemCount());
	for (int i = 0; i < tree->topLevelItemCount(); ++i) {
		auto* it = tree->topLevelItem(i);
		order.push_back(it->data(0, sexp_tree_view::FormulaDataRole).toInt());
	}
	return order;
}

void MissionEventsDialog::updateEventBitmap() {
	auto chained = _model->getChained();
	auto hasObjectiveText = !_model->getEventDirectiveText().empty();

	NodeImage bitmap;
	if (chained) {
		if (!hasObjectiveText) {
			bitmap = NodeImage::CHAIN;
		} else {
			bitmap = NodeImage::CHAIN_DIRECTIVE;
		}
	} else {
		if (!hasObjectiveText) {
			bitmap = NodeImage::ROOT;
		} else {
			bitmap = NodeImage::ROOT_DIRECTIVE;
		}
	}
	for (int i = 0; i < ui->eventTree->topLevelItemCount(); ++i) {
		auto item = ui->eventTree->topLevelItem(i);

		if (item->data(0, sexp_tree_view::FormulaDataRole).toInt() == _model->getFormula()) {
			item->setIcon(0, sexp_tree_view::convertNodeImageToIcon(bitmap));
			return;
		}
	}
}

void MissionEventsDialog::on_okAndCancelButtons_accepted()
{
	accept();
}

void MissionEventsDialog::on_okAndCancelButtons_rejected()
{
	reject();
}

void MissionEventsDialog::on_btnNewEvent_clicked()
{
	_model->createEvent();

	updateEventUi();
}

void MissionEventsDialog::on_btnInsertEvent_clicked()
{
	_model->insertEvent();

	updateEventUi();
}

void MissionEventsDialog::on_btnDeleteEvent_clicked()
{
	_model->deleteEvent();

	updateEventUi();
}

void MissionEventsDialog::on_eventUpBtn_clicked()
{
	auto* cur = ui->eventTree->currentItem();
	if (!cur || cur->parent())
		return; // roots only
	const int idx = ui->eventTree->indexOfTopLevelItem(cur);
	if (idx <= 0)
		return; // already at top

	QTreeWidgetItem* dest = ui->eventTree->topLevelItem(idx - 1);
	ui->eventTree->move_root(cur, dest, /*insert_before=*/true); // visual move + modified()

	// Ensure it stays selected and visible
	ui->eventTree->setCurrentItem(cur);
	ui->eventTree->scrollToItem(cur);
	updateEventMoveButtons();
}

void MissionEventsDialog::on_eventDownBtn_clicked()
{
	auto* cur = ui->eventTree->currentItem();
	if (!cur || cur->parent())
		return; // roots only
	const int idx = ui->eventTree->indexOfTopLevelItem(cur);
	const int last = ui->eventTree->topLevelItemCount() - 1;
	if (idx < 0 || idx >= last)
		return; // already at bottom

	QTreeWidgetItem* dest = ui->eventTree->topLevelItem(idx + 1);
	ui->eventTree->move_root(cur, dest, /*insert_before=*/false); // visual move + modified()

	ui->eventTree->setCurrentItem(cur);
	ui->eventTree->scrollToItem(cur);
	updateEventMoveButtons();
}

void MissionEventsDialog::on_repeatCountBox_valueChanged(int value)
{
	_model->setRepeatCount(value);
	updateEventUi();
}

void MissionEventsDialog::on_triggerCountBox_valueChanged(int value)
{
	_model->setTriggerCount(value);
	updateEventUi();
}

void MissionEventsDialog::on_intervalTimeBox_valueChanged(int value)
{
	_model->setIntervalTime(value);
}

void MissionEventsDialog::on_chainedCheckBox_stateChanged(int state)
{
	_model->setChained(state == Qt::Checked);
	updateEventBitmap();
	updateEventUi();
}

void MissionEventsDialog::on_chainedDelayBox_valueChanged(int value)
{
	_model->setChainDelay(value);
}

void MissionEventsDialog::on_scoreBox_valueChanged(int value)
{
	_model->setEventScore(value);
}

void MissionEventsDialog::on_teamCombo_currentIndexChanged(int index)
{
	_model->setEventTeam(ui->teamCombo->itemData(index).toInt());
}

void MissionEventsDialog::on_editDirectiveText_textChanged(const QString& text)
{
	SCP_string dir = text.toUtf8().constData();
	_model->setEventDirectiveText(dir);
	updateEventBitmap();
}

void MissionEventsDialog::on_editDirectiveKeypressText_textChanged(const QString& text)
{
	SCP_string dir = text.toUtf8().constData();
	_model->setEventDirectiveKeyText(dir);
}

void MissionEventsDialog::on_checkLogTrue_stateChanged(int state)
{
	_model->setLogTrue(state == Qt::Checked);
}

void MissionEventsDialog::on_checkLogFalse_stateChanged(int state)
{
	_model->setLogFalse(state == Qt::Checked);
}

void MissionEventsDialog::on_checkLogPrevious_stateChanged(int state)
{
	_model->setLogLogPrevious(state == Qt::Checked);
}

void MissionEventsDialog::on_checkLogAlwaysFalse_stateChanged(int state)
{
	_model->setLogAlwaysFalse(state == Qt::Checked);
}

void MissionEventsDialog::on_checkLogFirstRepeat_stateChanged(int state)
{
	_model->setLogFirstRepeat(state == Qt::Checked);
}

void MissionEventsDialog::on_checkLogLastRepeat_stateChanged(int state)
{
	_model->setLogLastRepeat(state == Qt::Checked);
}

void MissionEventsDialog::on_checkLogFirstTrigger_stateChanged(int state)
{
	_model->setLogFirstTrigger(state == Qt::Checked);
}

void MissionEventsDialog::on_checkLogLastTrigger_stateChanged(int state)
{
	_model->setLogLastTrigger(state == Qt::Checked);
}

void MissionEventsDialog::on_messageList_currentRowChanged(int row)
{
	_model->setCurrentlySelectedMessage(row);
	updateMessageUi();
}

void MissionEventsDialog::on_messageList_itemDoubleClicked(QListWidgetItem* item)
{
	if (!item || !ui->eventTree)
		return;

	const QString name = item->text();
	if (name != m_last_message_name) {
		m_last_message_name = name;
		m_last_message_node = -1; // reset cycle when switching message
	}

	int nodes[MAX_SEARCH_MESSAGE_DEPTH];
	const int num = ui->eventTree->_model.find_text(name.toUtf8().constData(), nodes, MAX_SEARCH_MESSAGE_DEPTH);
	if (num <= 0) {
		QMessageBox::information(this, tr("Error"), tr("No events using message '%1'").arg(name));
		return;
	}

	// cycle to next
	int next = nodes[0];
	if (m_last_message_node != -1) {
		int pos = -1;
		for (int i = 0; i < num; ++i) {
			if (nodes[i] == m_last_message_node) {
				pos = i;
				break;
			}
		}
		next = (pos == -1 || pos == num - 1) ? nodes[0] : nodes[pos + 1];
	}

	m_last_message_node = next;
	ui->eventTree->hilite_item(next);
}

void MissionEventsDialog::on_btnNewMsg_clicked()
{
	_model->createMessage();

	rebuildMessageList();
	updateMessageUi();
}

void MissionEventsDialog::on_btnInsertMsg_clicked()
{
	_model->insertMessage();

	// Refresh list UI (replace with your actual refresh)
	rebuildMessageList();

	// Keep selection/visibility in sync
	const int sel = _model->getCurrentlySelectedMessage(); // or expose accessor
	if (auto* w = ui->messageList) {                            // your list widget id
		w->setCurrentRow(sel);
		if (auto* it = w->item(sel))
			w->scrollToItem(it);
	}
	updateMessageUi();
}

void MissionEventsDialog::on_btnDeleteMsg_clicked()
{
	_model->deleteMessage();

	rebuildMessageList();
	updateMessageUi();
}

void MissionEventsDialog::on_msgUpBtn_clicked()
{
	_model->moveMessageUp();
	rebuildMessageList();
	const int sel = _model->getCurrentlySelectedMessage();
	if (auto* w = ui->messageList) {
		w->setCurrentRow(sel);
		if (auto* it = w->item(sel))
			w->scrollToItem(it);
	}
	updateMessageUi();
}

void MissionEventsDialog::on_msgDownBtn_clicked()
{
	_model->moveMessageDown();
	rebuildMessageList();
	const int sel = _model->getCurrentlySelectedMessage();
	if (auto* w = ui->messageList) {
		w->setCurrentRow(sel);
		if (auto* it = w->item(sel))
			w->scrollToItem(it);
	}
	updateMessageUi();
}

void MissionEventsDialog::on_messageName_textChanged(const QString& text)
{
	SCP_string name = text.toUtf8().constData();
	_model->setMessageName(name);

	rebuildMessageList();
}

void MissionEventsDialog::on_messageContent_textChanged()
{
	SCP_string content = ui->messageContent->toPlainText().toUtf8().constData();
	_model->setMessageText(content);
}

void MissionEventsDialog::on_btnMsgNote_clicked()
{
	if (!_model->messageIsValid())
		return;

	QDialog dlg(this);
	dlg.setWindowTitle(tr("Message Note"));
	auto* layout = new QVBoxLayout(&dlg);
	auto* label = new QLabel(tr("Enter a note for this message:"), &dlg);
	auto* edit = new QTextEdit(&dlg);
	edit->setPlainText(QString::fromUtf8(_model->getMessageNote().c_str()));
	edit->setMinimumSize(700, 500); // big!
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);

	layout->addWidget(label);
	layout->addWidget(edit, 1);
	layout->addWidget(buttons);

	QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

	if (dlg.exec() != QDialog::Accepted)
		return;

	SCP_string note = edit->toPlainText().toUtf8().constData();
	_model->setMessageNote(note);

	// Update the button text
	if (note.empty()) {
		ui->btnMsgNote->setText("Add Note");
	} else {
		ui->btnMsgNote->setText("Edit Note");
	}
}

void MissionEventsDialog::on_aniCombo_editingFinished()
{
	SCP_string name = ui->aniCombo->currentText().toUtf8().constData();
	_model->setMessageAni(name);

	initHeadCombo();
	ui->aniCombo->setCurrentText(QString::fromStdString(name));
}

void MissionEventsDialog::on_aniCombo_selectedIndexChanged(int index)
{
	SCP_string name = ui->aniCombo->itemText(index).toUtf8().constData();
	_model->setMessageAni(name);
}

void MissionEventsDialog::on_btnAniBrowse_clicked()
{
	// TODO Build gallery from the model's known head ANIs
	const QString filters =
		"FSO Images (*.ani *.eff *.png);;All files (*.*)";
	const QString file = QFileDialog::getOpenFileName(this, tr("Select Head Animation"), QString(), filters);
	if (file.isEmpty())
		return;
	_model->setMessageAni(file.toUtf8().constData());
}

void MissionEventsDialog::on_waveCombo_editingFinished()
{
	SCP_string name = ui->waveCombo->currentText().toUtf8().constData();
	_model->setMessageWave(name);

	initWaveFilenames();
	ui->waveCombo->setCurrentText(QString::fromStdString(name));
}

void MissionEventsDialog::on_waveCombo_selectedIndexChanged(int index)
{
	SCP_string name = ui->waveCombo->itemText(index).toUtf8().constData();
	_model->setMessageWave(name);
}

void MissionEventsDialog::on_btnBrowseWave_clicked()
{
	if (!_model->messageIsValid()) {
		return;
	}

	int z;
	if (The_mission.game_type & MISSION_TYPE_TRAINING) {
		z = cfile_push_chdir(CF_TYPE_VOICE_TRAINING);
	} else {
		z = cfile_push_chdir(CF_TYPE_VOICE_SPECIAL);
	}
	auto interface_path = QDir::currentPath();
	if (!z) {
		cfile_pop_dir();
	}

	auto name = QFileDialog::getOpenFileName(this,
		tr("Select message animation"),
		interface_path,
		"Voice Files (*.ogg *.wav);;Ogg Vorbis Files (*.ogg);;Wave Files (*.wav);;All Files (*)");

	if (name.isEmpty()) {
		// Nothing was selected
		return;
	}

	QFileInfo info(name);

	SCP_string file_name = info.fileName().toUtf8().constData();

	_model->setMessageWave(file_name);

	initWaveFilenames();
	ui->waveCombo->setCurrentText(QString::fromStdString(file_name));
}

void MissionEventsDialog::on_btnWavePlay_clicked()
{
	_model->playMessageWave();
}

void MissionEventsDialog::on_personaCombo_currentIndexChanged(int index)
{
	_model->setMessagePersona(ui->personaCombo->itemData(index).toInt());
}

void MissionEventsDialog::on_btnUpdateStuff_clicked()
{
	auto result = _viewport->dialogProvider->showButtonDialog(
		DialogType::Question,
		"Update Message Stuff",
		"This will update the message animation and persona to match the current mission settings. "
		   "Are you sure you want to do this?",
		{DialogButton::Yes, DialogButton::No});

	if (result != DialogButton::Yes) {
		_model->autoSelectPersona();
		updateMessageUi();
	}
}

void MissionEventsDialog::on_messageTeamCombo_currentIndexChanged(int index)
{
	_model->setMessageTeam(ui->messageTeamCombo->itemData(index).toInt());
}

} // namespace fso::fred::dialogs
