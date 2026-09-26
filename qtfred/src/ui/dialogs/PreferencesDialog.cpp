#include "PreferencesDialog.h"
#include "ui_PreferencesDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QEvent>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>

#include "ui/util/SignalBlockers.h"
#include "ui/widgets/MissionTextHighlighter.h"
#include "ui/widgets/sexp_tree_view.h"

namespace fso::fred::dialogs {
namespace {

int themeModeToIndex(ThemeMode mode)
{
	switch (mode) {
	case ThemeMode::Light:
		return 1;
	case ThemeMode::Dark:
		return 2;
	case ThemeMode::System:
		break;
	}
	return 0;
}

ThemeMode themeModeFromIndex(int index)
{
	switch (index) {
	case 1:
		return ThemeMode::Light;
	case 2:
		return ThemeMode::Dark;
	default:
		return ThemeMode::System;
	}
}

class ControlKeySequenceEdit : public QKeySequenceEdit {
public:
	explicit ControlKeySequenceEdit(const QKeySequence& sequence, QWidget* parent)
		: QKeySequenceEdit(sequence, parent) {}

protected:
	void keyPressEvent(QKeyEvent* event) override {
		if (event->isAutoRepeat()) {
			event->accept();
			return;
		}

		const auto key = event->key();
		if (key == Qt::Key_unknown) {
			event->accept();
			return;
		}

		// Ignore modifier-only presses until a non-modifier key is pressed
		if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta) {
			event->accept();
			return;
		}

		const auto mods = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier | Qt::KeypadModifier);
		setKeySequence(QKeySequence(static_cast<int>(key | mods)));
		event->accept();
	}
};

} // namespace

PreferencesDialog::PreferencesDialog(FredView* parent, EditorViewport* viewport)
	: QDialog(parent)
	, ui(new Ui::PreferencesDialog())
	, _model(new PreferencesDialogModel(this, viewport))
	, _fredView(parent)
	, _viewport(viewport)
{
	ui->setupUi(this);

	initializeUi();
	updateUi();

	connect(_model.get(), &PreferencesDialogModel::modelChanged, this, &PreferencesDialog::updateUi);
	connect(_model.get(), &PreferencesDialogModel::modelChanged, this, &PreferencesDialog::applyChanges);
}

PreferencesDialog::~PreferencesDialog() = default;

void PreferencesDialog::applyChanges() {
	_model->apply();
	_fredView->setIconSize(QSize(_model->getToolbarIconSize(), _model->getToolbarIconSize()));
	_fredView->restartAutosaveTimer();
	_viewport->needsUpdate();
}

void PreferencesDialog::initializeUi() {
	// Build the controls key-binding form dynamically from the registered bindings
	auto* form = new QFormLayout(ui->controlsFormWidget);
	auto& bindings = ControlBindings::instance();
	for (const auto& def : bindings.definitions()) {
		auto* edit = new ControlKeySequenceEdit(_model->getControlKey(def.action), ui->controlsFormWidget);
		_controlEditors.emplace(def.action, edit);
		form->addRow(def.label + ':', edit);
		connect(edit, &QKeySequenceEdit::keySequenceChanged, this, [this, action = def.action](const QKeySequence& seq) {
			_model->setControlKey(action, seq);
		});
	}

	buildSyntaxColorsUi();
}

namespace {
// Sample event for the Syntax Colors preview; touches every color role.
const char* const SyntaxPreviewText =
	"#Events\t\t; a comment\n"
	"$Formula: ( when\n"
	"   ( is-destroyed-delay 0 \"Alpha 1\" )\n"
	"   ( modify-variable @Score ( + @Score 10 ) )\n"
	"   ( not-an-operator )\n"
	")\n"
	"+Name: Example\n"
	";;FSO 21.0.0;; +Event Log Flags: ( \"true\" )\n";
} // namespace

void PreferencesDialog::buildSyntaxColorsUi()
{
	auto* group = ui->syntaxColorsGroup;
	auto* layout = ui->syntaxColorsLayout;

	_syntaxThemeLabel = new QLabel(group);
	layout->addWidget(_syntaxThemeLabel);

	// Two columns of rows: name, color swatch, bold, italic, reset.
	auto* roleGrid = new QGridLayout;
	roleGrid->setHorizontalSpacing(4);
	constexpr int perColumn = (SyntaxRoleCount + 1) / 2;
	constexpr int columnWidth = 6; // five widgets plus a gap column
	for (int r = 0; r < SyntaxRoleCount; ++r) {
		const auto role = static_cast<SyntaxRole>(r);
		const int row = r % perColumn;
		const int col = (r / perColumn) * columnWidth;
		auto& w = _syntaxRows[r];

		roleGrid->addWidget(new QLabel(SyntaxColorScheme::roleLabel(role), group), row, col);

		w.color = new QToolButton(group);
		w.color->setToolTip(tr("Choose a color"));
		roleGrid->addWidget(w.color, row, col + 1);

		w.bold = new QToolButton(group);
		w.bold->setText(QStringLiteral("B"));
		w.bold->setCheckable(true);
		w.bold->setToolTip(tr("Bold"));
		QFont boldFont = w.bold->font();
		boldFont.setBold(true);
		w.bold->setFont(boldFont);
		roleGrid->addWidget(w.bold, row, col + 2);

		w.italic = new QToolButton(group);
		w.italic->setText(QStringLiteral("I"));
		w.italic->setCheckable(true);
		w.italic->setToolTip(tr("Italic"));
		QFont italicFont = w.italic->font();
		italicFont.setItalic(true);
		w.italic->setFont(italicFont);
		roleGrid->addWidget(w.italic, row, col + 3);

		w.reset = new QToolButton(group);
		w.reset->setText(tr("Reset"));
		w.reset->setToolTip(tr("Use the theme's default"));
		roleGrid->addWidget(w.reset, row, col + 4);

		// Every edit applies to the theme that's showing right now.
		connect(w.color, &QToolButton::clicked, this, [this, role]() {
			const bool dark = SyntaxColorScheme::paletteIsDark();
			SyntaxStyle style = _model->getSyntaxStyle(role, dark);
			const QColor picked = QColorDialog::getColor(style.color, this, SyntaxColorScheme::roleLabel(role));
			if (!picked.isValid())
				return;
			style.color = picked;
			_model->setSyntaxStyle(role, dark, style);
		});
		connect(w.bold, &QToolButton::toggled, this, [this, role](bool on) {
			const bool dark = SyntaxColorScheme::paletteIsDark();
			SyntaxStyle style = _model->getSyntaxStyle(role, dark);
			style.bold = on;
			_model->setSyntaxStyle(role, dark, style);
		});
		connect(w.italic, &QToolButton::toggled, this, [this, role](bool on) {
			const bool dark = SyntaxColorScheme::paletteIsDark();
			SyntaxStyle style = _model->getSyntaxStyle(role, dark);
			style.italic = on;
			_model->setSyntaxStyle(role, dark, style);
		});
		connect(w.reset, &QToolButton::clicked, this, [this, role]() {
			_model->resetSyntaxStyle(role, SyntaxColorScheme::paletteIsDark());
		});
	}
	roleGrid->setColumnMinimumWidth(columnWidth - 1, 16);
	roleGrid->setColumnStretch(columnWidth * 2 - 1, 1);
	layout->addLayout(roleGrid);

	auto* optionsRow = new QHBoxLayout;
	_rainbowParensCheck = new QCheckBox(tr("Rainbow parentheses"), group);
	_rainbowParensCheck->setToolTip(tr("Color each nesting level of parentheses differently."));
	connect(_rainbowParensCheck, &QCheckBox::toggled, this, [this](bool on) { _model->setRainbowParens(on); });
	auto* resetAll = new QPushButton(tr("Reset All Colors"), group);
	resetAll->setToolTip(tr("Return every color for this theme to its default"));
	connect(resetAll, &QPushButton::clicked, this,
		[this]() { _model->resetAllSyntaxStyles(SyntaxColorScheme::paletteIsDark()); });
	optionsRow->addWidget(_rainbowParensCheck);
	optionsRow->addStretch(1);
	optionsRow->addWidget(resetAll);
	layout->addLayout(optionsRow);

	// Live preview. Preferences apply as they change, so the highlighter here
	// (like the one in an open Events editor) restyles right away.
	auto* preview = new QPlainTextEdit(group);
	preview->setReadOnly(true);
	preview->setLineWrapMode(QPlainTextEdit::NoWrap);
	preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	preview->setPlainText(QString::fromLatin1(SyntaxPreviewText));
	const int lines = static_cast<int>(QString::fromLatin1(SyntaxPreviewText).count(QLatin1Char('\n'))) + 1;
	preview->setFixedHeight(preview->fontMetrics().lineSpacing() * lines + 2 * preview->frameWidth() + 8);
	new fso::fred::MissionTextHighlighter(preview);
	layout->addWidget(preview);
}

void PreferencesDialog::updateSyntaxColorsUi()
{
	if (_syntaxThemeLabel == nullptr)
		return;
	util::SignalBlockers blockers(this);

	const bool dark = SyntaxColorScheme::paletteIsDark();
	_syntaxThemeLabel->setText(dark ? tr("Colors for the dark theme:") : tr("Colors for the light theme:"));
	for (int r = 0; r < SyntaxRoleCount; ++r) {
		const auto role = static_cast<SyntaxRole>(r);
		const SyntaxStyle style = _model->getSyntaxStyle(role, dark);
		auto& w = _syntaxRows[r];
		QPixmap swatch(16, 16);
		swatch.fill(style.color);
		w.color->setIcon(QIcon(swatch));
		w.bold->setChecked(style.bold);
		w.italic->setChecked(style.italic);
		w.reset->setEnabled(_model->isSyntaxStyleCustom(role, dark));
	}
	_rainbowParensCheck->setChecked(_model->getRainbowParens());
}

void PreferencesDialog::changeEvent(QEvent* event)
{
	// A theme change (from this dialog or the OS) swaps which color set is shown.
	if (event->type() == QEvent::PaletteChange)
		updateSyntaxColorsUi();
	QDialog::changeEvent(event);
}

void PreferencesDialog::updateUi() {
	util::SignalBlockers blockers(this);

	updateSyntaxColorsUi();

	// General
	ui->offerAutosaveRecovery->setChecked(_model->getOfferAutosaveRecovery());
	ui->autosaveIntervalSeconds->setValue(_model->getAutosaveIntervalSeconds());
	ui->sexpNumberEveryN->setValue(_model->getSexpNumberEveryN());
	ui->createBakOnSave->setChecked(_model->getCreateBakOnSave());
	ui->undoStackDepth->setValue(_model->getUndoStackDepth());
	ui->moveShipsWhenUndocking->setChecked(_model->getMoveShipsWhenUndocking());
	ui->alwaysSaveDisplayNames->setChecked(_model->getAlwaysSaveDisplayNames());
	ui->checkPotentialIssues->setChecked(_model->getCheckPotentialIssues());
	ui->applyAutoCorrections->setChecked(_model->getApplyAutoCorrections());
	ui->themeCombo->setCurrentIndex(themeModeToIndex(_model->getThemeMode()));
	ui->dataMenuStyleCombo->setCurrentIndex(static_cast<int>(_model->getDataMenuStyle()));

	const int iconSize = _model->getToolbarIconSize();
	ui->toolbarIconSizeCombo->setCurrentIndex(iconSize <= 16 ? 0 : iconSize >= 32 ? 2 : 1);
	ui->outlineLodCombo->setCurrentIndex(_model->getOutlineLod());
	ui->labelFontScaleSpin->setValue(_model->getLabelFontScale());
	ui->showSexpHelpMissionEvents->setChecked(_model->getShowSexpHelpMissionEvents());
	ui->showSexpHelpMissionGoals->setChecked(_model->getShowSexpHelpMissionGoals());
	ui->showSexpHelpMissionCutscenes->setChecked(_model->getShowSexpHelpMissionCutscenes());
	ui->showSexpHelpShipEditor->setChecked(_model->getShowSexpHelpShipEditor());
	ui->showSexpHelpWingEditor->setChecked(_model->getShowSexpHelpWingEditor());
	ui->showSexpHelpPropEditor->setChecked(_model->getShowSexpHelpPropEditor());

	// Grid plane selection and enabled state
	const auto plane = _model->getGridPlane();
	ui->xyPlaneRadio->setChecked(plane == GridPlane::XY);
	ui->xzPlaneRadio->setChecked(plane == GridPlane::XZ);
	ui->yzPlaneRadio->setChecked(plane == GridPlane::YZ);

	// Only the axis perpendicular to the selected plane is editable
	ui->gridCenterX->setEnabled(plane == GridPlane::YZ);
	ui->gridCenterY->setEnabled(plane == GridPlane::XZ);
	ui->gridCenterZ->setEnabled(plane == GridPlane::XY);

	ui->gridCenterX->setValue(_model->getGridCenterX());
	ui->gridCenterY->setValue(_model->getGridCenterY());
	ui->gridCenterZ->setValue(_model->getGridCenterZ());

	// Controls
	ui->invertOrbitX->setChecked(_model->getInvertOrbitX());
	ui->invertOrbitY->setChecked(_model->getInvertOrbitY());
	for (const auto& entry : _controlEditors) {
		entry.second->setKeySequence(_model->getControlKey(entry.first));
	}
}

void PreferencesDialog::on_offerAutosaveRecovery_toggled(bool checked) {
	_model->setOfferAutosaveRecovery(checked);
}

void PreferencesDialog::on_autosaveIntervalSeconds_valueChanged(int value) {
	_model->setAutosaveIntervalSeconds(value);
}

void PreferencesDialog::on_sexpNumberEveryN_valueChanged(int value) {
	_model->setSexpNumberEveryN(value);
	// setSexpNumberEveryN -> modelChanged -> applyChanges -> apply() has already committed the
	// new value to the viewport by now, so re-icon any open trees to renumber them live.
	sexp_tree_view::refreshAllInstances();
}

void PreferencesDialog::on_createBakOnSave_toggled(bool checked) {
	_model->setCreateBakOnSave(checked);
}

void PreferencesDialog::on_undoStackDepth_valueChanged(int value) {
	_model->setUndoStackDepth(value);
}

void PreferencesDialog::on_moveShipsWhenUndocking_toggled(bool checked) {
	_model->setMoveShipsWhenUndocking(checked);
}

void PreferencesDialog::on_alwaysSaveDisplayNames_toggled(bool checked) {
	_model->setAlwaysSaveDisplayNames(checked);
}

void PreferencesDialog::on_checkPotentialIssues_toggled(bool checked) {
	_model->setCheckPotentialIssues(checked);
}

void PreferencesDialog::on_applyAutoCorrections_toggled(bool checked) {
	_model->setApplyAutoCorrections(checked);
}

void PreferencesDialog::on_toolbarIconSizeCombo_currentIndexChanged(int index) {
	static constexpr int sizes[] = { 16, 24, 32 };
	_model->setToolbarIconSize(sizes[index]);
}

void PreferencesDialog::on_outlineLodCombo_currentIndexChanged(int index) {
	_model->setOutlineLod(index);
}

void PreferencesDialog::on_labelFontScaleSpin_valueChanged(double value) {
	_model->setLabelFontScale(value);
}

void PreferencesDialog::on_themeCombo_currentIndexChanged(int index) {
	_model->setThemeMode(themeModeFromIndex(index));
}

void PreferencesDialog::on_dataMenuStyleCombo_currentIndexChanged(int index) {
	_model->setDataMenuStyle(static_cast<DataMenuStyle>(index));
}

void PreferencesDialog::on_showSexpHelpMissionEvents_toggled(bool checked) {
	_model->setShowSexpHelpMissionEvents(checked);
}
void PreferencesDialog::on_showSexpHelpMissionGoals_toggled(bool checked) {
	_model->setShowSexpHelpMissionGoals(checked);
}
void PreferencesDialog::on_showSexpHelpMissionCutscenes_toggled(bool checked) {
	_model->setShowSexpHelpMissionCutscenes(checked);
}
void PreferencesDialog::on_showSexpHelpShipEditor_toggled(bool checked) {
	_model->setShowSexpHelpShipEditor(checked);
}
void PreferencesDialog::on_showSexpHelpWingEditor_toggled(bool checked) {
	_model->setShowSexpHelpWingEditor(checked);
}
void PreferencesDialog::on_showSexpHelpPropEditor_toggled(bool checked) {
	_model->setShowSexpHelpPropEditor(checked);
}

void PreferencesDialog::on_xyPlaneRadio_toggled(bool checked) {
	if (checked) {
		_model->setGridPlane(GridPlane::XY);
	}
}

void PreferencesDialog::on_xzPlaneRadio_toggled(bool checked) {
	if (checked) {
		_model->setGridPlane(GridPlane::XZ);
	}
}

void PreferencesDialog::on_yzPlaneRadio_toggled(bool checked) {
	if (checked) {
		_model->setGridPlane(GridPlane::YZ);
	}
}

void PreferencesDialog::on_gridCenterX_valueChanged(int value) {
	_model->setGridCenterX(value);
}

void PreferencesDialog::on_gridCenterY_valueChanged(int value) {
	_model->setGridCenterY(value);
}

void PreferencesDialog::on_gridCenterZ_valueChanged(int value) {
	_model->setGridCenterZ(value);
}

void PreferencesDialog::on_resetGridButton_clicked() {
	_model->resetGrid();
}

void PreferencesDialog::on_invertOrbitX_toggled(bool checked) {
	_model->setInvertOrbitX(checked);
}

void PreferencesDialog::on_invertOrbitY_toggled(bool checked) {
	_model->setInvertOrbitY(checked);
}

void PreferencesDialog::on_resetDefaultsButton_clicked() {
	_model->resetControlDefaults();
}

} // namespace fso::fred::dialogs
