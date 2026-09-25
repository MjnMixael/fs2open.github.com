#pragma once

#include "mission/dialogs/AbstractDialogModel.h"

#include "asteroid/asteroid.h"

#include <QString>
#include <QVector>
#include <utility>

namespace fso::fred::dialogs {

class AsteroidEditorDialogModel: public AbstractDialogModel {
Q_OBJECT

public:
	AsteroidEditorDialogModel(QObject* parent, EditorViewport* viewport);

	enum _box_line_edits {
		_O_MIN_X = 0,
		_O_MIN_Y,
		_O_MIN_Z,
		_O_MAX_X,
		_O_MAX_Y,
		_O_MAX_Z,
		_I_MIN_X,
		_I_MIN_Y,
		_I_MIN_Z,
		_I_MAX_X,
		_I_MAX_Y,
		_I_MAX_Z,
	};

	// overrides
	bool apply() override;
	void reject() override;

	QByteArray captureState() const override;
	void restoreState(const QByteArray& state) override;

	// Snapshot/restore of the mission globals this dialog edits. Static so an
	// undo command can restore them with no dialog instance alive.
	static QByteArray captureGlobalState();
	static void restoreGlobalState(const QByteArray& state);
	// The globals as they were when the dialog opened, before any live preview.
	// This is the "before" of the dialog's OK.
	QByteArray captureOriginalState() const;

	// The viewport gizmos only ever change the box bounds. These snapshot just
	// those, so undoing a drag can't clobber the dialog's other live previews.
	static QByteArray captureGizmoState();
	static void restoreGizmoState(const QByteArray& state);
	// Re-read the box bounds from Asteroid_field after a gizmo edited them.
	// includeBaseline also moves the Cancel baseline, for edits that happened
	// outside this dialog (a main-stack undo).
	void syncGizmoFromGlobals(bool includeBaseline);

	// toggles
	void setFieldEnabled(bool enabled);
	bool getFieldEnabled() const;

	void setInnerBoxEnabled(bool enabled);
	bool getInnerBoxEnabled() const;

	void setEnhancedEnabled(bool enabled);
	bool getEnhancedEnabled() const;

	// field types
	void setFieldType(field_type_t type);
	field_type_t getFieldType();

	void setDebrisGenre(debris_genre_t genre);
	debris_genre_t getDebrisGenre();

	// basic values
	void setNumAsteroids(int num_asteroids);
	int  getNumAsteroids() const;

	void setAvgSpeed(const QString& speed);
	QString& getAvgSpeed();

	// box values
	void setBoxText(const QString& text, _box_line_edits type);
	QString& getBoxText(_box_line_edits type);

	// object selections
	QVector<std::pair<QString, bool>> getAsteroidSelections() const;
	void setAsteroidSelections(const QVector<bool>& selected);

	QVector<std::pair<QString, bool>> getDebrisSelections() const;
	void setDebrisSelections(const QVector<bool>& selected);

	QVector<std::pair<QString, bool>> getShipSelections();
	void setShipSelections(const QVector<bool>& selected);
	// Resolved target-name list — the undoable form of the ship selection.
	// The checkbox bitmap is only meaningful against the ship list captured
	// when the subdialog opened, which can change while the editor is open.
	const SCP_vector<SCP_string>& getShipTargetNames() const { return _field_target_names; }
	void setShipTargetNames(const SCP_vector<SCP_string>& names);

signals:
	// A viewport gizmo edit finished while this dialog is open. The dialog
	// records it on its own undo stack, so OK keeps it and Cancel drops it.
	void gizmoEditCommitted(const QByteArray& before, const QByteArray& after, const QString& text);

private:
	enum class BoundBox { Outer, Inner };

	// Re-read the whole working copy after an undo rewrote the globals behind
	// this dialog's back.
	void reloadFromGlobals();

	void initializeData();
	void update_internal_field();
	bool validate_data();
	void showErrorDialogNoCancel(const SCP_string& message);

	// Push the working-copy bounds for the given box (or one component of them)
	// into the global Asteroid_field, so the visualizer and the viewport gizmos
	// preview the edit live.
	void pushLiveBound(BoundBox box);
	void pushLiveBoundComponent(_box_line_edits type);

	// boilerplate
	bool _bypass_errors;
	const int _MIN_BOX_THICKNESS = 400;

	// working copy of the asteroid field
	asteroid_field _a_field;

	// Snapshot taken in initializeData(), restored by reject() so cancelling
	// the dialog undoes any live preview the user produced by dragging
	// handles or typing into the spinboxes.
	asteroid_field _original_a_field;

	// toggles
	bool  _enable_asteroids;
	bool  _enable_inner_bounds;
	bool  _enable_enhanced_checking;

	// field types
	field_type_t _field_type;     // active or passive
	debris_genre_t _debris_genre; // debris or asteroid

	// basic values
	int   _num_asteroids;
	QString _avg_speed;

	// box values
	QString _min_x;
	QString _min_y;
	QString _min_z;
	QString _max_x;
	QString _max_y;
	QString _max_z;
	QString _inner_min_x;
	QString _inner_min_y;
	QString _inner_min_z;
	QString _inner_max_x;
	QString _inner_max_y;
	QString _inner_max_z;

	// object selections
	SCP_vector<SCP_string>     _field_asteroid_type; // asteroid types
	SCP_vector<int>            _field_debris_type;   // debris types
	SCP_vector<SCP_string>     _field_target_names;  // target ships

	// Helper vectors for the checkbox dialog
	SCP_vector<SCP_string>                 asteroidOptions; // asteroid options for the checkbox dialog
	SCP_vector<std::pair<SCP_string, int>> debrisOptions;   // debris options for the checkbox dialog.. for this one we include the index in the pair so we can use it to map
	SCP_vector<SCP_string>                 shipOptions;     // ship options for the checkbox dialog
};

} // namespace fso::fred::dialogs
