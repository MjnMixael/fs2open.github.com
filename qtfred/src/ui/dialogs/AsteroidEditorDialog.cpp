#include "ui/dialogs/AsteroidEditorDialog.h"
#include "ui/dialogs/General/CheckBoxListDialog.h"
#include "ui/util/SignalBlockers.h"

#include <algorithm>

#include "ui_AsteroidEditorDialog.h"
#include <mission/util.h>
#include <mission/commands/FredCommands.h>
#include <ui/util/DialogUndo.h>

#include <utility>

namespace fso::fred::dialogs {

AsteroidEditorDialog::AsteroidEditorDialog(FredView *parent, EditorViewport* viewport) :
	QDialog(parent),
	_viewport(viewport),
	_editor(viewport->editor),
	_fredView(parent),
	ui(new Ui::AsteroidEditorDialog()),
	_model(new AsteroidEditorDialogModel(this, viewport))
{
	this->setFocus();
	ui->setupUi(this);

	_dialogStack = new QUndoStack(this);
	_fredView->undoGroup()->addStack(_dialogStack);
	util::setupDialogUndo(this, _fredView->undoGroup(), _dialogStack, tr("Asteroid Field"));

	// set our internal values, update the UI
	initializeUi();
	updateUi();

	// Whenever the model is modified (by spinbox edit, checkbox toggle, or by
	// a handle drag in the viewport that called back into the model), refresh
	// the dialog widgets and the handle positions. SignalBlockers in
	// updateUi() prevents the refresh from re-firing the slots.
	// updateUi() above already registered the initial handle group via
	// rebuildHandles(); future modelChanged emissions will fire updateUi
	// (which re-runs rebuildHandles) to keep the handles in sync.
	connect(_model.get(), &AbstractDialogModel::modelChanged, this, &AsteroidEditorDialog::updateUi);

	// setup validators for text input
	_box_validator.setNotation(QDoubleValidator::StandardNotation);
	_box_validator.setDecimals(1);

	ui->lineEdit_obox_minX->setValidator(&_box_validator);
	ui->lineEdit_obox_minY->setValidator(&_box_validator);
	ui->lineEdit_obox_minZ->setValidator(&_box_validator);
	ui->lineEdit_obox_maxX->setValidator(&_box_validator);
	ui->lineEdit_obox_maxY->setValidator(&_box_validator);
	ui->lineEdit_obox_maxZ->setValidator(&_box_validator);
	ui->lineEdit_ibox_minX->setValidator(&_box_validator);
	ui->lineEdit_ibox_minY->setValidator(&_box_validator);
	ui->lineEdit_ibox_minZ->setValidator(&_box_validator);
	ui->lineEdit_ibox_maxX->setValidator(&_box_validator);
	ui->lineEdit_ibox_maxY->setValidator(&_box_validator);
	ui->lineEdit_ibox_maxZ->setValidator(&_box_validator);

	ui->lineEditAvgSpeed->setValidator(&_speed_validator);
}

AsteroidEditorDialog::~AsteroidEditorDialog()
{
	if (_viewport != nullptr && _handle_group.valid()) {
		_viewport->unregisterHandleGroup(_handle_group);
	}
}

void AsteroidEditorDialog::accept()
{
	QByteArray stateBefore = _model->captureState();
	if (_model->apply()) {
		QByteArray stateAfter = _model->captureState();
		_model->setParent(nullptr);
		_fredView->mainUndoStack()->push(
			new ApplyDialogCommand(std::move(_model), stateBefore, stateAfter,
			                       _viewport->editor, tr("Edit Asteroid Field")));
		_dialogStack->clear();
		_fredView->undoGroup()->setActiveStack(_fredView->mainUndoStack());
		QDialog::accept();
	}
}

void AsteroidEditorDialog::reject()
{
	if (!_model) {
		_dialogStack->clear();
		_fredView->undoGroup()->setActiveStack(_fredView->mainUndoStack());
		QDialog::reject();
		return;
	}
	if (rejectOrCloseHandler(this, _model.get(), _viewport)) {
		_dialogStack->clear();
		_fredView->undoGroup()->setActiveStack(_fredView->mainUndoStack());
		QDialog::reject();
	}
}

void AsteroidEditorDialog::closeEvent(QCloseEvent* e)
{
	reject();
	// reject() hides the dialog when it actually closes. Let that close
	// proceed (so a dialog created with WA_DeleteOnClose is destroyed),
	// and only veto it when reject() decided to keep the dialog open (e.g.
	// the user cancelled the unsaved-changes prompt).
	if (isVisible()) {
		e->ignore();
	} else {
		e->accept();
	}
}


void AsteroidEditorDialog::initializeUi()
{
	util::SignalBlockers blockers(this); // block signals while we set up the UI
	
	// Checkboxes
	ui->enabled->setChecked(_model->getFieldEnabled());
	ui->innerBoxEnabled->setChecked(_model->getInnerBoxEnabled());
	ui->enhancedFieldEnabled->setChecked(_model->getEnhancedEnabled());

	// Radio buttons for field type
	ui->radioButtonActiveField->setChecked(_model->getFieldType() == FT_ACTIVE);
	ui->radioButtonPassiveField->setChecked(_model->getFieldType() == FT_PASSIVE);

	// Radio buttons for debris genre
	ui->radioButtonAsteroid->setChecked(_model->getDebrisGenre() == DG_ASTEROID);
	ui->radioButtonDebris->setChecked(_model->getDebrisGenre() == DG_DEBRIS);

	// Spin box
	ui->spinBoxNumber->setValue(_model->getNumAsteroids());

	// Average speed
	ui->lineEditAvgSpeed->setText(_model->getAvgSpeed());

	// Outer box
	ui->lineEdit_obox_minX->setText(_model->getBoxText(AsteroidEditorDialogModel::_box_line_edits::_O_MIN_X));
	ui->lineEdit_obox_minY->setText(_model->getBoxText(AsteroidEditorDialogModel::_box_line_edits::_O_MIN_Y));
	ui->lineEdit_obox_minZ->setText(_model->getBoxText(AsteroidEditorDialogModel::_box_line_edits::_O_MIN_Z));
	ui->lineEdit_obox_maxX->setText(_model->getBoxText(AsteroidEditorDialogModel::_box_line_edits::_O_MAX_X));
	ui->lineEdit_obox_maxY->setText(_model->getBoxText(AsteroidEditorDialogModel::_box_line_edits::_O_MAX_Y));
	ui->lineEdit_obox_maxZ->setText(_model->getBoxText(AsteroidEditorDialogModel::_box_line_edits::_O_MAX_Z));

	// Inner box
	ui->lineEdit_ibox_minX->setText(_model->getBoxText(AsteroidEditorDialogModel::_box_line_edits::_I_MIN_X));
	ui->lineEdit_ibox_minY->setText(_model->getBoxText(AsteroidEditorDialogModel::_box_line_edits::_I_MIN_Y));
	ui->lineEdit_ibox_minZ->setText(_model->getBoxText(AsteroidEditorDialogModel::_box_line_edits::_I_MIN_Z));
	ui->lineEdit_ibox_maxX->setText(_model->getBoxText(AsteroidEditorDialogModel::_box_line_edits::_I_MAX_X));
	ui->lineEdit_ibox_maxY->setText(_model->getBoxText(AsteroidEditorDialogModel::_box_line_edits::_I_MAX_Y));
	ui->lineEdit_ibox_maxZ->setText(_model->getBoxText(AsteroidEditorDialogModel::_box_line_edits::_I_MAX_Z));

	// Housekeeping
	ui->spinBoxNumber->setRange(1, MAX_ASTEROIDS);
}

void AsteroidEditorDialog::updateUi()
{
	util::SignalBlockers blockers(this); // block signals while we update the UI

	// Re-push every text/spinbox value from the model. This is what makes
	// handle-drag updates (and any other modelChanged source) visible in the
	// dialog. SignalBlockers nests cleanly with the one inside initializeUi.
	initializeUi();

	bool overall_enabled = _model->getFieldEnabled();
	bool asteroids_enabled = overall_enabled && _model->getDebrisGenre() == DG_ASTEROID;
	bool debris_enabled = overall_enabled && _model->getDebrisGenre() == DG_DEBRIS;
	bool inner_box_enabled = _model->getInnerBoxEnabled();
	bool field_is_active = (_model->getFieldType() == FT_ACTIVE);

	// Checkboxes
	ui->innerBoxEnabled->setEnabled(overall_enabled);
	ui->enhancedFieldEnabled->setEnabled(overall_enabled);

	// Radio buttons for field type
	ui->radioButtonActiveField->setEnabled(overall_enabled);
	ui->radioButtonPassiveField->setEnabled(overall_enabled);

	// Radio buttons for debris genre
	ui->radioButtonAsteroid->setEnabled(overall_enabled);
	ui->radioButtonDebris->setEnabled(overall_enabled && !field_is_active);

	// Spin box
	ui->spinBoxNumber->setEnabled(overall_enabled);

	// Average speed
	ui->lineEditAvgSpeed->setEnabled(overall_enabled);

	// Outer box
	ui->lineEdit_obox_minX->setEnabled(overall_enabled);
	ui->lineEdit_obox_minY->setEnabled(overall_enabled);
	ui->lineEdit_obox_minZ->setEnabled(overall_enabled);
	ui->lineEdit_obox_maxX->setEnabled(overall_enabled);
	ui->lineEdit_obox_maxY->setEnabled(overall_enabled);
	ui->lineEdit_obox_maxZ->setEnabled(overall_enabled);

	// Inner box
	ui->lineEdit_ibox_minX->setEnabled(overall_enabled && inner_box_enabled);
	ui->lineEdit_ibox_minY->setEnabled(overall_enabled && inner_box_enabled);
	ui->lineEdit_ibox_minZ->setEnabled(overall_enabled && inner_box_enabled);
	ui->lineEdit_ibox_maxX->setEnabled(overall_enabled && inner_box_enabled);
	ui->lineEdit_ibox_maxY->setEnabled(overall_enabled && inner_box_enabled);
	ui->lineEdit_ibox_maxZ->setEnabled(overall_enabled && inner_box_enabled);

	// Push buttons for object types
	ui->asteroidSelectButton->setEnabled(overall_enabled && asteroids_enabled);
	ui->debrisSelectButton->setEnabled(overall_enabled && debris_enabled && !field_is_active);

	// Push buttons for ship targets
	ui->shipSelectButton->setEnabled(overall_enabled && field_is_active);

	// Update the radio buttons as these do depend on the field type
	ui->radioButtonAsteroid->setChecked(_model->getDebrisGenre() == DG_ASTEROID);
	ui->radioButtonDebris->setChecked(_model->getDebrisGenre() == DG_DEBRIS);

	// Refresh handle positions after the spinbox values have been re-pushed.
	rebuildHandles();
}

void AsteroidEditorDialog::on_okAndCancelButtons_accepted()
{
	accept();
}

void AsteroidEditorDialog::on_okAndCancelButtons_rejected()
{
	reject();
}

void AsteroidEditorDialog::on_enabled_toggled(bool enabled)
{
	const bool before = _model->getFieldEnabled();
	if (before == enabled) {
		return;
	}

	_model->setFieldEnabled(enabled);
	updateUi();

	auto* cmd = new FieldEditCommand<bool>(FieldId::Ast_FieldEnabled, nullptr, tr("Toggle Asteroid Field"), true);
	cmd->addEntry(before, enabled, [this](const bool& v) {
		_model->setFieldEnabled(v);
		QSignalBlocker blocker(ui->enabled);
		ui->enabled->setChecked(v);
		updateUi();
	});
	_dialogStack->push(cmd);
}

void AsteroidEditorDialog::on_innerBoxEnabled_toggled(bool enabled)
{
	const bool before = _model->getInnerBoxEnabled();
	if (before == enabled) {
		return;
	}

	_model->setInnerBoxEnabled(enabled);
	updateUi();

	auto* cmd = new FieldEditCommand<bool>(FieldId::Ast_InnerEnabled, nullptr, tr("Toggle Inner Box"), true);
	cmd->addEntry(before, enabled, [this](const bool& v) {
		_model->setInnerBoxEnabled(v);
		QSignalBlocker blocker(ui->innerBoxEnabled);
		ui->innerBoxEnabled->setChecked(v);
		updateUi();
	});
	_dialogStack->push(cmd);
}

void AsteroidEditorDialog::on_enhancedFieldEnabled_toggled(bool enabled)
{
	const bool before = _model->getEnhancedEnabled();
	if (before == enabled) {
		return;
	}

	_model->setEnhancedEnabled(enabled);

	auto* cmd = new FieldEditCommand<bool>(FieldId::Ast_Enhanced, nullptr, tr("Toggle Enhanced Field"), true);
	cmd->addEntry(before, enabled, [this](const bool& v) {
		_model->setEnhancedEnabled(v);
		QSignalBlocker blocker(ui->enhancedFieldEnabled);
		ui->enhancedFieldEnabled->setChecked(v);
	});
	_dialogStack->push(cmd);
}

// Field type and debris genre change together (an active field forces the
// asteroid genre), so the radio commands track the (type, genre) pair.
void AsteroidEditorDialog::changeFieldType(field_type_t type, debris_genre_t genre)
{
	const std::pair<int, int> before{_model->getFieldType(), _model->getDebrisGenre()};
	const std::pair<int, int> after{type, genre};
	if (before == after) {
		return;
	}

	_model->setFieldType(type);
	_model->setDebrisGenre(genre);
	updateUi();

	auto* cmd = new FieldEditCommand<std::pair<int, int>>(FieldId::Ast_FieldType, nullptr, tr("Change Field Type"), true);
	cmd->addEntry(before, after, [this](const std::pair<int, int>& v) {
		_model->setFieldType(static_cast<field_type_t>(v.first));
		_model->setDebrisGenre(static_cast<debris_genre_t>(v.second));
		QSignalBlocker blockActive(ui->radioButtonActiveField);
		QSignalBlocker blockPassive(ui->radioButtonPassiveField);
		ui->radioButtonActiveField->setChecked(v.first == FT_ACTIVE);
		ui->radioButtonPassiveField->setChecked(v.first == FT_PASSIVE);
		updateUi();
	});
	_dialogStack->push(cmd);
}

void AsteroidEditorDialog::on_radioButtonActiveField_toggled(bool checked)
{
	if (checked) {
		changeFieldType(FT_ACTIVE, DG_ASTEROID); // only allow asteroids in active fields
	}
}

void AsteroidEditorDialog::on_radioButtonPassiveField_toggled(bool checked)
{
	if (checked) {
		changeFieldType(FT_PASSIVE, _model->getDebrisGenre());
	}
}

void AsteroidEditorDialog::on_radioButtonAsteroid_toggled(bool checked)
{
	if (checked && _model->getDebrisGenre() != DG_ASTEROID) {
		_model->setDebrisGenre(DG_ASTEROID);
		updateUi();

		auto* cmd = new FieldEditCommand<int>(FieldId::Ast_DebrisGenre, nullptr, tr("Change Debris Genre"), true);
		cmd->addEntry(DG_DEBRIS, DG_ASTEROID, [this](const int& v) {
			_model->setDebrisGenre(static_cast<debris_genre_t>(v));
			updateUi(); // re-checks the genre radios
		});
		_dialogStack->push(cmd);
	}
}

void AsteroidEditorDialog::on_radioButtonDebris_toggled(bool checked)
{
	if (checked && _model->getDebrisGenre() != DG_DEBRIS) {
		_model->setDebrisGenre(DG_DEBRIS);
		updateUi();

		auto* cmd = new FieldEditCommand<int>(FieldId::Ast_DebrisGenre, nullptr, tr("Change Debris Genre"), true);
		cmd->addEntry(DG_ASTEROID, DG_DEBRIS, [this](const int& v) {
			_model->setDebrisGenre(static_cast<debris_genre_t>(v));
			updateUi(); // re-checks the genre radios
		});
		_dialogStack->push(cmd);
	}
}

void AsteroidEditorDialog::on_spinBoxNumber_valueChanged(int num_asteroids)
{
	const int before = _model->getNumAsteroids();
	if (before == num_asteroids) {
		return;
	}

	_model->setNumAsteroids(num_asteroids);

	auto* cmd = new FieldEditCommand<int>(FieldId::Ast_NumAsteroids, nullptr, tr("Change Asteroid Count"), true);
	cmd->addEntry(before, num_asteroids, [this](const int& v) {
		_model->setNumAsteroids(v);
		QSignalBlocker blocker(ui->spinBoxNumber);
		ui->spinBoxNumber->setValue(v);
	});
	_dialogStack->push(cmd);
}

void AsteroidEditorDialog::on_lineEditAvgSpeed_textEdited(const QString& text)
{
	const QString before = _model->getAvgSpeed();
	if (before == text) {
		return;
	}

	_model->setAvgSpeed(text);

	auto* cmd = new FieldEditCommand<QString>(FieldId::Ast_AvgSpeed, nullptr, tr("Change Average Speed"), true);
	cmd->addEntry(before, text, [this](const QString& v) {
		_model->setAvgSpeed(v);
		// textEdited does not fire for programmatic setText
		ui->lineEditAvgSpeed->setText(v);
	});
	_dialogStack->push(cmd);
}

void AsteroidEditorDialog::changeBoxText(QLineEdit* edit, AsteroidEditorDialogModel::_box_line_edits type, const QString& text)
{
	const QString before = _model->getBoxText(type);
	if (before == text) {
		return;
	}

	_model->setBoxText(text, type);

	auto* cmd = new FieldEditCommand<QString>(FieldId::Ast_Box + type, nullptr, tr("Change Field Bounds"), true);
	cmd->addEntry(before, text, [this, edit, type](const QString& v) {
		_model->setBoxText(v, type);
		// textEdited does not fire for programmatic setText
		edit->setText(v);
	});
	_dialogStack->push(cmd);
}

void AsteroidEditorDialog::on_lineEdit_obox_minX_textEdited(const QString& text)
{
	changeBoxText(ui->lineEdit_obox_minX, AsteroidEditorDialogModel::_O_MIN_X, text);
}

void AsteroidEditorDialog::on_lineEdit_obox_minY_textEdited(const QString& text)
{
	changeBoxText(ui->lineEdit_obox_minY, AsteroidEditorDialogModel::_O_MIN_Y, text);
}

void AsteroidEditorDialog::on_lineEdit_obox_minZ_textEdited(const QString& text)
{
	changeBoxText(ui->lineEdit_obox_minZ, AsteroidEditorDialogModel::_O_MIN_Z, text);
}

void AsteroidEditorDialog::on_lineEdit_obox_maxX_textEdited(const QString& text)
{
	changeBoxText(ui->lineEdit_obox_maxX, AsteroidEditorDialogModel::_O_MAX_X, text);
}

void AsteroidEditorDialog::on_lineEdit_obox_maxY_textEdited(const QString& text)
{
	changeBoxText(ui->lineEdit_obox_maxY, AsteroidEditorDialogModel::_O_MAX_Y, text);
}

void AsteroidEditorDialog::on_lineEdit_obox_maxZ_textEdited(const QString& text)
{
	changeBoxText(ui->lineEdit_obox_maxZ, AsteroidEditorDialogModel::_O_MAX_Z, text);
}

void AsteroidEditorDialog::on_lineEdit_ibox_minX_textEdited(const QString& text)
{
	changeBoxText(ui->lineEdit_ibox_minX, AsteroidEditorDialogModel::_I_MIN_X, text);
}

void AsteroidEditorDialog::on_lineEdit_ibox_minY_textEdited(const QString& text)
{
	changeBoxText(ui->lineEdit_ibox_minY, AsteroidEditorDialogModel::_I_MIN_Y, text);
}

void AsteroidEditorDialog::on_lineEdit_ibox_minZ_textEdited(const QString& text)
{
	changeBoxText(ui->lineEdit_ibox_minZ, AsteroidEditorDialogModel::_I_MIN_Z, text);
}

void AsteroidEditorDialog::on_lineEdit_ibox_maxX_textEdited(const QString& text)
{
	changeBoxText(ui->lineEdit_ibox_maxX, AsteroidEditorDialogModel::_I_MAX_X, text);
}

void AsteroidEditorDialog::on_lineEdit_ibox_maxY_textEdited(const QString& text)
{
	changeBoxText(ui->lineEdit_ibox_maxY, AsteroidEditorDialogModel::_I_MAX_Y, text);
}

void AsteroidEditorDialog::on_lineEdit_ibox_maxZ_textEdited(const QString& text)
{
	changeBoxText(ui->lineEdit_ibox_maxZ, AsteroidEditorDialogModel::_I_MAX_Z, text);
}

static QVector<bool> checkedStates(const QVector<std::pair<QString, bool>>& options)
{
	QVector<bool> out;
	out.reserve(options.size());
	for (const auto& option : options) {
		out.push_back(option.second);
	}
	return out;
}

void AsteroidEditorDialog::on_asteroidSelectButton_clicked()
{
	CheckBoxListDialog dlg(this);
	dlg.setCaption("Select Asteroid Types");
	dlg.setOptions(_model->getAsteroidSelections());

	if (dlg.exec() != QDialog::Accepted) {
		return;
	}

	const QVector<bool> before = checkedStates(_model->getAsteroidSelections());
	const QVector<bool> after  = dlg.getCheckedStates();
	if (before == after) {
		return;
	}

	_model->setAsteroidSelections(after);

	auto* cmd = new FieldEditCommand<QVector<bool>>(FieldId::Ast_AsteroidSel, nullptr, tr("Select Asteroid Types"), true);
	cmd->setNoMerge(); // each subdialog visit is a discrete action
	cmd->addEntry(before, after, [this](const QVector<bool>& v) { _model->setAsteroidSelections(v); });
	_dialogStack->push(cmd);
}

void AsteroidEditorDialog::on_debrisSelectButton_clicked()
{
	CheckBoxListDialog dlg(this);
	dlg.setCaption("Select Debris Types");
	dlg.setOptions(_model->getDebrisSelections());

	if (dlg.exec() != QDialog::Accepted) {
		return;
	}

	const QVector<bool> before = checkedStates(_model->getDebrisSelections());
	const QVector<bool> after  = dlg.getCheckedStates();
	if (before == after) {
		return;
	}

	_model->setDebrisSelections(after);

	auto* cmd = new FieldEditCommand<QVector<bool>>(FieldId::Ast_DebrisSel, nullptr, tr("Select Debris Types"), true);
	cmd->setNoMerge();
	cmd->addEntry(before, after, [this](const QVector<bool>& v) { _model->setDebrisSelections(v); });
	_dialogStack->push(cmd);
}

void AsteroidEditorDialog::on_shipSelectButton_clicked()
{
	CheckBoxListDialog dlg(this);
	dlg.setCaption("Select Ship Debris Types");
	dlg.setOptions(_model->getShipSelections());
	if (dlg.exec() != QDialog::Accepted) {
		return;
	}

	// Undo state is the resolved target-name list, not the checkbox bitmap:
	// the bitmap only has meaning against the ship list captured when the
	// subdialog opened (setShipSelections consumes and clears that list).
	const SCP_vector<SCP_string> before = _model->getShipTargetNames();
	_model->setShipSelections(dlg.getCheckedStates());
	const SCP_vector<SCP_string> after = _model->getShipTargetNames();
	if (before == after) {
		return;
	}

	auto* cmd = new FieldEditCommand<SCP_vector<SCP_string>>(
		FieldId::Ast_ShipSel, nullptr, tr("Select Ship Debris Types"), true);
	cmd->setNoMerge();
	cmd->addEntry(before, after, [this](const SCP_vector<SCP_string>& v) { _model->setShipTargetNames(v); });
	_dialogStack->push(cmd);
}

namespace {

// Build a closure that, given an axis (0/1/2) and which face (Min/Max), reads
// the toolbar constraint state and returns true iff motion is allowed on that
// axis. Used by face handles so an axis-locked-out face renders grayed out
// and refuses to be picked.
std::function<bool()> makeAxisAllowedFn(fso::fred::EditorViewport* viewport, int axis_index) {
	return [viewport, axis_index]() {
		// Constraint vector: a component of 1.0 means motion along that axis
		// is allowed. (See FredView::on_actionConstrainX_triggered and
		// friends.)
		const vec3d& c = viewport->Constraint;
		switch (axis_index) {
		case 0: return c.xyz.x != 0.0f;
		case 1: return c.xyz.y != 0.0f;
		case 2: return c.xyz.z != 0.0f;
		default: return true;
		}
	};
}

// Tag for the on_drag closure: which model setter to call. Captured by value
// in the lambdas below so the dialog's lifetime doesn't matter — the model
// outlives the handles via the registry until the dialog destructor.
struct AsteroidBoundTarget {
	AsteroidEditorDialogModel* model;
	AsteroidEditorDialogModel::BoundBox box;
};

void appendFaceHandles(std::vector<fso::fred::ViewportHandle>& out,
                       fso::fred::EditorViewport* viewport,
                       AsteroidBoundTarget target,
                       const vec3d& mn, const vec3d& mx,
                       int color_r, int color_g, int color_b)
{
	using fso::fred::ViewportHandle;
	const vec3d center{ {{ (mn.xyz.x + mx.xyz.x) * 0.5f,
	                       (mn.xyz.y + mx.xyz.y) * 0.5f,
	                       (mn.xyz.z + mx.xyz.z) * 0.5f }} };

	// Six face handles. Each lives at the center of its face and moves only
	// along the face's normal axis. The +/- sign indicates whether the face
	// is the min or max side on that axis.
	struct FaceSpec {
		int axis_index;
		bool is_max;
		vec3d pos;
		vec3d normal;
	};
	const FaceSpec faces[6] = {
		{ 0, false, {{{ mn.xyz.x, center.xyz.y, center.xyz.z }}}, {{{ -1.0f, 0.0f, 0.0f }}} },
		{ 0, true,  {{{ mx.xyz.x, center.xyz.y, center.xyz.z }}}, {{{  1.0f, 0.0f, 0.0f }}} },
		{ 1, false, {{{ center.xyz.x, mn.xyz.y, center.xyz.z }}}, {{{ 0.0f, -1.0f, 0.0f }}} },
		{ 1, true,  {{{ center.xyz.x, mx.xyz.y, center.xyz.z }}}, {{{ 0.0f,  1.0f, 0.0f }}} },
		{ 2, false, {{{ center.xyz.x, center.xyz.y, mn.xyz.z }}}, {{{ 0.0f, 0.0f, -1.0f }}} },
		{ 2, true,  {{{ center.xyz.x, center.xyz.y, mx.xyz.z }}}, {{{ 0.0f, 0.0f,  1.0f }}} },
	};

	for (const auto& f : faces) {
		ViewportHandle h;
		h.kind = ViewportHandle::Kind::Face;
		h.world_pos = f.pos;
		h.axis = f.normal;
		h.color_r = color_r;
		h.color_g = color_g;
		h.color_b = color_b;
		h.is_enabled = makeAxisAllowedFn(viewport, f.axis_index);
		const auto box = target.box;
		auto* model = target.model;
		const int axis = f.axis_index;
		const auto corner = f.is_max ? AsteroidEditorDialogModel::BoundCorner::Max
		                             : AsteroidEditorDialogModel::BoundCorner::Min;
		h.on_drag = [model, box, corner, axis](const vec3d& delta) {
			// Project delta onto the face normal (already done by EditorViewport
			// for Face handles, so delta is colinear with the normal — just
			// take the signed magnitude on this axis).
			float component = 0.0f;
			switch (axis) {
			case 0: component = delta.xyz.x; break;
			case 1: component = delta.xyz.y; break;
			case 2: component = delta.xyz.z; break;
			}
			model->nudgeBoundComponent(box, corner, axis, component);
		};
		out.push_back(std::move(h));
	}
}

void appendCornerHandles(std::vector<fso::fred::ViewportHandle>& out,
                         AsteroidBoundTarget target,
                         const vec3d& mn, const vec3d& mx,
                         int color_r, int color_g, int color_b)
{
	using fso::fred::ViewportHandle;
	// Eight corners. Drag callback resizes the three adjacent faces by the
	// drag delta, with clamping done per-component by nudgeBoundComponent.
	for (int xi = 0; xi < 2; ++xi) {
		for (int yi = 0; yi < 2; ++yi) {
			for (int zi = 0; zi < 2; ++zi) {
				ViewportHandle h;
				h.kind = ViewportHandle::Kind::Corner;
				h.world_pos = vec3d{ {{ xi ? mx.xyz.x : mn.xyz.x,
				                         yi ? mx.xyz.y : mn.xyz.y,
				                         zi ? mx.xyz.z : mn.xyz.z }} };
				// Pack the three +/-1 indicators into the axis vector for the
				// renderer's reference (unused by drag math here).
				h.axis = vec3d{ {{ xi ? 1.0f : -1.0f, yi ? 1.0f : -1.0f, zi ? 1.0f : -1.0f }} };
				h.color_r = color_r;
				h.color_g = color_g;
				h.color_b = color_b;
				auto* model = target.model;
				const auto box = target.box;
				const auto cornerX = xi ? AsteroidEditorDialogModel::BoundCorner::Max : AsteroidEditorDialogModel::BoundCorner::Min;
				const auto cornerY = yi ? AsteroidEditorDialogModel::BoundCorner::Max : AsteroidEditorDialogModel::BoundCorner::Min;
				const auto cornerZ = zi ? AsteroidEditorDialogModel::BoundCorner::Max : AsteroidEditorDialogModel::BoundCorner::Min;
				h.on_drag = [model, box, cornerX, cornerY, cornerZ](const vec3d& delta) {
					model->nudgeBoundComponent(box, cornerX, 0, delta.xyz.x);
					model->nudgeBoundComponent(box, cornerY, 1, delta.xyz.y);
					model->nudgeBoundComponent(box, cornerZ, 2, delta.xyz.z);
				};
				out.push_back(std::move(h));
			}
		}
	}
}

void appendCenterHandle(std::vector<fso::fred::ViewportHandle>& out,
                        AsteroidBoundTarget target,
                        const vec3d& mn, const vec3d& mx,
                        int color_r, int color_g, int color_b)
{
	using fso::fred::ViewportHandle;
	ViewportHandle h;
	h.kind = ViewportHandle::Kind::Center;
	h.world_pos = vec3d{ {{ (mn.xyz.x + mx.xyz.x) * 0.5f,
	                         (mn.xyz.y + mx.xyz.y) * 0.5f,
	                         (mn.xyz.z + mx.xyz.z) * 0.5f }} };
	h.color_r = color_r;
	h.color_g = color_g;
	h.color_b = color_b;
	auto* model = target.model;
	const auto box = target.box;
	h.on_drag = [model, box](const vec3d& delta) {
		model->translateBound(box, delta);
	};
	out.push_back(std::move(h));
}

} // anonymous namespace

void AsteroidEditorDialog::rebuildHandles()
{
	if (_viewport == nullptr) {
		return;
	}

	std::vector<ViewportHandle> handles;

	if (_model->getFieldEnabled()) {
		// Outer box: orange (matches the wireframe).
		vec3d mn, mx;
		_model->getBound(AsteroidEditorDialogModel::BoundBox::Outer, &mn, &mx);
		AsteroidBoundTarget outer{ _model.get(), AsteroidEditorDialogModel::BoundBox::Outer };
		appendFaceHandles(handles, _viewport, outer, mn, mx, 255, 160, 64);
		appendCornerHandles(handles, outer, mn, mx, 255, 200, 64);
		appendCenterHandle(handles, outer, mn, mx, 255, 220, 96);

		// Inner box: green (matches its wireframe), only if enabled.
		if (_model->getInnerBoxEnabled()) {
			vec3d imn, imx;
			_model->getBound(AsteroidEditorDialogModel::BoundBox::Inner, &imn, &imx);
			AsteroidBoundTarget inner{ _model.get(), AsteroidEditorDialogModel::BoundBox::Inner };
			appendFaceHandles(handles, _viewport, inner, imn, imx, 64, 220, 120);
			appendCornerHandles(handles, inner, imn, imx, 96, 240, 140);
			appendCenterHandle(handles, inner, imn, imx, 128, 255, 160);
		}
	}

	if (_handle_group.valid()) {
		_viewport->updateHandleGroup(_handle_group, std::move(handles));
	} else {
		_handle_group = _viewport->registerHandleGroup(std::move(handles));
	}
}

} // namespace fso::fred::dialogs
