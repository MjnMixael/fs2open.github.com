#include "mission/dialogs/AsteroidEditorDialogModel.h"

#include <algorithm>

namespace fso::fred::dialogs {

AsteroidEditorDialogModel::AsteroidEditorDialogModel(QObject* parent, EditorViewport* viewport) :
	AbstractDialogModel(parent, viewport),
	_bypass_errors(false),
	_enable_asteroids(false),
	_enable_inner_bounds(false),
	_enable_enhanced_checking(false),
	_field_type(FT_ACTIVE),
	_debris_genre(DG_ASTEROID),
	_num_asteroids(1),
	_avg_speed(""),
	_min_x(""),
	_min_y(""),
	_min_z(""),
	_max_x(""),
	_max_y(""),
	_max_z(""),
	_inner_min_x(""),
	_inner_min_y(""),
	_inner_min_z(""),
	_inner_max_x(""),
	_inner_max_y(""),
	_inner_max_z("")
{
	initializeData();
}

bool AsteroidEditorDialogModel::apply()
{
	update_internal_field();
	if (!AsteroidEditorDialogModel::validate_data()) {
		return false;
	}
	Asteroid_field = _a_field;
	return true;
}

void AsteroidEditorDialogModel::reject()
{
	// Restore the global Asteroid_field to the snapshot taken in
	// initializeData(). This undoes any live preview produced by handle drags
	// or by typing into the bound spinboxes (which also push live now).
	Asteroid_field = _original_a_field;
}

void AsteroidEditorDialogModel::initializeData()
{
	_a_field = Asteroid_field; // copy the current asteroid field data
	_original_a_field = Asteroid_field; // snapshot for reject() to restore

	// Now initialize the model data from the asteroid field
	_enable_asteroids = (_a_field.num_initial_asteroids > 0);
	_enable_inner_bounds = _a_field.has_inner_bound;
	_enable_enhanced_checking = _a_field.enhanced_visibility_checks;

	_field_type = _a_field.field_type;
	_debris_genre = _a_field.debris_genre;
	
	_num_asteroids = _a_field.num_initial_asteroids;
	if (!_enable_asteroids) {
		_num_asteroids = 1; // fallback
	}

	CLAMP(_num_asteroids, 1, MAX_ASTEROIDS);

	_avg_speed = QString::number(static_cast<int>(vm_vec_mag(&_a_field.vel)));

	// Convert coords to strings
	_min_x = QString::number(_a_field.min_bound.xyz.x, 'f', 1);
	_min_y = QString::number(_a_field.min_bound.xyz.y, 'f', 1);
	_min_z = QString::number(_a_field.min_bound.xyz.z, 'f', 1);
	_max_x = QString::number(_a_field.max_bound.xyz.x, 'f', 1);
	_max_y = QString::number(_a_field.max_bound.xyz.y, 'f', 1);
	_max_z = QString::number(_a_field.max_bound.xyz.z, 'f', 1);
	_inner_min_x = QString::number(_a_field.inner_min_bound.xyz.x, 'f', 1);
	_inner_min_y = QString::number(_a_field.inner_min_bound.xyz.y, 'f', 1);
	_inner_min_z = QString::number(_a_field.inner_min_bound.xyz.z, 'f', 1);
	_inner_max_x = QString::number(_a_field.inner_max_bound.xyz.x, 'f', 1);
	_inner_max_y = QString::number(_a_field.inner_max_bound.xyz.y, 'f', 1);
	_inner_max_z = QString::number(_a_field.inner_max_bound.xyz.z, 'f', 1);

	// Copy the object lists
	_field_debris_type = _a_field.field_debris_type;
	_field_asteroid_type = _a_field.field_asteroid_type;
	_field_target_names = _a_field.target_names;

	// Initialize asteroid options
	const auto& list = get_list_valid_asteroid_subtypes();
	for (const auto& name : list) {
		asteroidOptions.push_back(name);
	}

	// Initialize debris options
	for (size_t i = 0; i < Asteroid_info.size(); ++i) {
		if (Asteroid_info[i].type == -1) {
			debrisOptions.emplace_back(std::make_pair(Asteroid_info[i].name, static_cast<int>(i)));
		}
	}
	_modified = false;
}

void AsteroidEditorDialogModel::update_internal_field()
{
	// if asteroids are not enabled, just clear the field and return
	if (!_enable_asteroids) {
		_a_field = {};
		return;
	}
	
	// Do some quick data conversion
	int num_asteroids = _enable_asteroids ? _num_asteroids : 0;
	CLAMP(num_asteroids, 0, MAX_ASTEROIDS);
	vec3d vel_vec = vmd_x_vector;
	vm_vec_scale(&vel_vec, static_cast<float>(_avg_speed.toInt()));
	
	// Now update the asteroid field with the current values
	_a_field.has_inner_bound = _enable_inner_bounds;
	_a_field.enhanced_visibility_checks = _enable_enhanced_checking;
	
	_a_field.field_type = _field_type;
	_a_field.debris_genre = _debris_genre;
	
	_a_field.num_initial_asteroids = num_asteroids;
	_a_field.vel = vel_vec;

	// save the box coords
	_a_field.min_bound.xyz.x = _min_x.toFloat();
	_a_field.min_bound.xyz.y = _min_y.toFloat();
	_a_field.min_bound.xyz.z = _min_z.toFloat();
	_a_field.max_bound.xyz.x = _max_x.toFloat();
	_a_field.max_bound.xyz.y = _max_y.toFloat();
	_a_field.max_bound.xyz.z = _max_z.toFloat();

	if (_enable_inner_bounds) {
		_a_field.inner_min_bound.xyz.x = _inner_min_x.toFloat();
		_a_field.inner_min_bound.xyz.y = _inner_min_y.toFloat();
		_a_field.inner_min_bound.xyz.z = _inner_min_z.toFloat();
		_a_field.inner_max_bound.xyz.x = _inner_max_x.toFloat();
		_a_field.inner_max_bound.xyz.y = _inner_max_y.toFloat();
		_a_field.inner_max_bound.xyz.z = _inner_max_z.toFloat();
	}

	// clear the lists
	_a_field.field_debris_type.clear();
	_a_field.field_asteroid_type.clear();
	_a_field.target_names.clear();

	// debris
	if ((_field_type == FT_PASSIVE) && (_debris_genre == DG_DEBRIS)) {
		_a_field.field_debris_type = _field_debris_type;
	}

	// asteroids
	if (_debris_genre == DG_ASTEROID) {
		_a_field.field_asteroid_type = _field_asteroid_type;

		// target ships
		if (_field_type == FT_ACTIVE) {
			_a_field.target_names = _field_target_names;
		}
	}
}

bool AsteroidEditorDialogModel::validate_data()
{
	if (!_enable_asteroids) {
		return true;
	}
	else {
		// be helpful to the FREDer; try to advise precisely what the problem is
		// more general checks 1st, followed by more specific ones
		_bypass_errors = false;

		// check outer x/y/z max is greater than min
		if (_a_field.max_bound.xyz.x < _a_field.min_bound.xyz.x) {
			showErrorDialogNoCancel( "Outer box 'X' min is greater than max\n");
			return false;
		}

		// check y
		if (_a_field.max_bound.xyz.y < _a_field.min_bound.xyz.y) {
			showErrorDialogNoCancel( "Outer box 'Y' min is greater than max\n");
			return false;
		}

		// check z
		if (_a_field.max_bound.xyz.z < _a_field.min_bound.xyz.z) {
			showErrorDialogNoCancel( "Outer box 'Z' min is greater than max\n");
			return false;
		}

		if (_a_field.has_inner_bound) {
			// check inner x/y/z max is greater than min
			if (_a_field.inner_max_bound.xyz.x < _a_field.inner_min_bound.xyz.x) {
				showErrorDialogNoCancel( "Inner box 'X' min is greater than inner max\n");
				return false;
			}

			if (_a_field.inner_max_bound.xyz.y < _a_field.inner_min_bound.xyz.y) {
				showErrorDialogNoCancel( "Inner box 'Y' min is greater than inner max\n");
				return false;
			}

			if (_a_field.inner_max_bound.xyz.z < _a_field.inner_min_bound.xyz.z) {
				showErrorDialogNoCancel( "Inner box 'Z' min is greater than inner max\n");
				return false;
			}

			// check outer x/y/z max is greater than inner x/y/z max
			if (_a_field.max_bound.xyz.x < _a_field.inner_max_bound.xyz.x) {
				showErrorDialogNoCancel( "Outer box 'X' max is less than inner max\n");
				return false;
			}

			if (_a_field.max_bound.xyz.y < _a_field.inner_max_bound.xyz.y) {
				showErrorDialogNoCancel( "Outer box 'Y' max is less than inner max\n");
				return false;
			}

			if (_a_field.max_bound.xyz.z < _a_field.inner_max_bound.xyz.z) {
				showErrorDialogNoCancel( "Outer box 'Z' max is less than inner max\n");
				return false;
			}

			// check outer x/y/z min is less than inner x/y/z min
			if (_a_field.min_bound.xyz.x > _a_field.inner_min_bound.xyz.x) {
				showErrorDialogNoCancel( "Inner box 'X' min is less than outer min\n");
				return false;
			}

			if (_a_field.min_bound.xyz.y > _a_field.inner_min_bound.xyz.y) {
				showErrorDialogNoCancel( "Inner box 'Y' min is less than outer min\n");
				return false;
			}

			if (_a_field.min_bound.xyz.z > _a_field.inner_min_bound.xyz.z) {
				showErrorDialogNoCancel( "Inner box 'Z' min is less than outer min\n");
				return false;
			}

			// split checks to give FREDers more specific feedback
			// check x thickness
			if (_a_field.inner_min_bound.xyz.x - _MIN_BOX_THICKNESS < _a_field.min_bound.xyz.x) {
				showErrorDialogNoCancel(
						"X axis minimum values must be at least " + \
						std::to_string(_MIN_BOX_THICKNESS) + " apart");
				return false;
			}
			if (_a_field.inner_max_bound.xyz.x + _MIN_BOX_THICKNESS > _a_field.max_bound.xyz.x) {
				showErrorDialogNoCancel(
						"X axis maximum values must be at least " + \
						std::to_string(_MIN_BOX_THICKNESS) + " apart");
				return false;
			}

			// check y thickness
			if (_a_field.inner_min_bound.xyz.y - _MIN_BOX_THICKNESS < _a_field.min_bound.xyz.y) {
				showErrorDialogNoCancel(
						"Y axis minimum values must be at least " + \
						std::to_string(_MIN_BOX_THICKNESS) + " apart");
				return false;
			}
			if (_a_field.inner_max_bound.xyz.y + _MIN_BOX_THICKNESS > _a_field.max_bound.xyz.y) {
				showErrorDialogNoCancel(
						"Y axis maximum values must be at least " + \
						std::to_string(_MIN_BOX_THICKNESS) + " apart");
				return false;
			}

			// check z thickness
			if (_a_field.inner_min_bound.xyz.z - _MIN_BOX_THICKNESS < _a_field.min_bound.xyz.z) {
				showErrorDialogNoCancel(
						"Z axis minimum values must be at least " + \
						std::to_string(_MIN_BOX_THICKNESS) + " apart");
				return false;
			}
			if (_a_field.inner_max_bound.xyz.z + _MIN_BOX_THICKNESS > _a_field.max_bound.xyz.z) {
				showErrorDialogNoCancel(
						"Z axis maximum values must be at least " + \
						std::to_string(_MIN_BOX_THICKNESS) + " apart");
				return false;
			}
		}

		// for a ship debris (i.e. passive) field, need at least one debris type is selected
		if (_a_field.field_type == FT_PASSIVE) {
			if (_a_field.debris_genre == DG_DEBRIS) {
				if (_a_field.field_debris_type.size() == 0) {
					showErrorDialogNoCancel("You must choose one or more types of ship debris\n");
					return false;
				}
			}
		}

		// check at least one asteroid subtype is selected
		if (_a_field.debris_genre == DG_ASTEROID) {
			if (_a_field.field_asteroid_type.size() == 0) {
				showErrorDialogNoCancel("You must choose one or more asteroid subtypes\n");
				return false;
			}
		}

	}

	return true;
}

void AsteroidEditorDialogModel::showErrorDialogNoCancel(const SCP_string& message)
{
	if (_bypass_errors) {
		return;
	}

	_bypass_errors = true;
	_viewport->dialogProvider->showButtonDialog(DialogType::Error,
												"Error",
												message,
												{ DialogButton::Ok });
}

void AsteroidEditorDialogModel::setFieldEnabled(bool enabled)
{
	modify(_enable_asteroids, enabled);
	// Live-push so the visualizer (which gates on num_initial_asteroids > 0)
	// shows or hides the box immediately as the user toggles the checkbox.
	// Reject restores the snapshot.
	Asteroid_field.num_initial_asteroids = enabled ? std::max(1, _num_asteroids) : 0;
	if (enabled) {
		pushLiveBound(BoundBox::Outer);
		if (_enable_inner_bounds) {
			pushLiveBound(BoundBox::Inner);
		}
	}
}

bool AsteroidEditorDialogModel::getFieldEnabled() const
{
	return _enable_asteroids;
}

void AsteroidEditorDialogModel::setInnerBoxEnabled(bool enabled)
{
	modify(_enable_inner_bounds, enabled);
	// Live-push so the inner wireframe (and the inner-box viewport handles)
	// appear/disappear as the user toggles the checkbox.
	Asteroid_field.has_inner_bound = enabled;
	if (enabled) {
		pushLiveBound(BoundBox::Inner);
	}
}

bool AsteroidEditorDialogModel::getInnerBoxEnabled() const
{
	return _enable_inner_bounds;
}

void AsteroidEditorDialogModel::setEnhancedEnabled(bool enabled)
{
	modify(_enable_enhanced_checking, enabled);
}

bool AsteroidEditorDialogModel::getEnhancedEnabled() const
{
	return _enable_enhanced_checking;
}

void AsteroidEditorDialogModel::setFieldType(field_type_t type)
{
	modify(_field_type, type);
}

field_type_t AsteroidEditorDialogModel::getFieldType()
{
	return _field_type;
}

void AsteroidEditorDialogModel::setDebrisGenre(debris_genre_t genre)
{
	modify(_debris_genre, genre);
}

debris_genre_t AsteroidEditorDialogModel::getDebrisGenre()
{
	return _debris_genre;
}

void AsteroidEditorDialogModel::setNumAsteroids(int num_asteroids)
{
	modify(_num_asteroids, num_asteroids);
}

int AsteroidEditorDialogModel::getNumAsteroids() const
{
	return _num_asteroids;
}

void AsteroidEditorDialogModel::setAvgSpeed(const QString& speed)
{
	modify(_avg_speed, speed);
}

QString& AsteroidEditorDialogModel::getAvgSpeed()
{
	return _avg_speed;
}

void AsteroidEditorDialogModel::setBoxText(const QString &text, _box_line_edits type)
{
	BoundBox box = (type >= _I_MIN_X) ? BoundBox::Inner : BoundBox::Outer;
	switch (type) {
		case _O_MIN_X: modify(_min_x, text); break;
		case _O_MIN_Y: modify(_min_y, text); break;
		case _O_MIN_Z: modify(_min_z, text); break;
		case _O_MAX_X: modify(_max_x, text); break;
		case _O_MAX_Y: modify(_max_y, text); break;
		case _O_MAX_Z: modify(_max_z, text); break;
		case _I_MIN_X: modify(_inner_min_x, text); break;
		case _I_MIN_Y: modify(_inner_min_y, text); break;
		case _I_MIN_Z: modify(_inner_min_z, text); break;
		case _I_MAX_X: modify(_inner_max_x, text); break;
		case _I_MAX_Y: modify(_inner_max_y, text); break;
		case _I_MAX_Z: modify(_inner_max_z, text); break;
		default:
			Error(LOCATION, "Get a coder! Unknown enum value found! %i", type);
			return;
	}

	// Push the new bound straight to Asteroid_field so the wireframe visualizer
	// reflects what the user is typing or dragging in real time. The working
	// copy strings remain the source of truth for apply()/validate_data().
	pushLiveBound(box);
}

void AsteroidEditorDialogModel::pushLiveBound(BoundBox box)
{
	if (box == BoundBox::Outer) {
		Asteroid_field.min_bound.xyz.x = _min_x.toFloat();
		Asteroid_field.min_bound.xyz.y = _min_y.toFloat();
		Asteroid_field.min_bound.xyz.z = _min_z.toFloat();
		Asteroid_field.max_bound.xyz.x = _max_x.toFloat();
		Asteroid_field.max_bound.xyz.y = _max_y.toFloat();
		Asteroid_field.max_bound.xyz.z = _max_z.toFloat();
	} else {
		Asteroid_field.inner_min_bound.xyz.x = _inner_min_x.toFloat();
		Asteroid_field.inner_min_bound.xyz.y = _inner_min_y.toFloat();
		Asteroid_field.inner_min_bound.xyz.z = _inner_min_z.toFloat();
		Asteroid_field.inner_max_bound.xyz.x = _inner_max_x.toFloat();
		Asteroid_field.inner_max_bound.xyz.y = _inner_max_y.toFloat();
		Asteroid_field.inner_max_bound.xyz.z = _inner_max_z.toFloat();
	}
}

void AsteroidEditorDialogModel::getBound(BoundBox box, vec3d* out_min, vec3d* out_max) const
{
	if (box == BoundBox::Outer) {
		out_min->xyz.x = _min_x.toFloat();
		out_min->xyz.y = _min_y.toFloat();
		out_min->xyz.z = _min_z.toFloat();
		out_max->xyz.x = _max_x.toFloat();
		out_max->xyz.y = _max_y.toFloat();
		out_max->xyz.z = _max_z.toFloat();
	} else {
		out_min->xyz.x = _inner_min_x.toFloat();
		out_min->xyz.y = _inner_min_y.toFloat();
		out_min->xyz.z = _inner_min_z.toFloat();
		out_max->xyz.x = _inner_max_x.toFloat();
		out_max->xyz.y = _inner_max_y.toFloat();
		out_max->xyz.z = _inner_max_z.toFloat();
	}
}

void AsteroidEditorDialogModel::nudgeBoundComponent(BoundBox box, BoundCorner corner, int axis_index, float delta_world)
{
	if (delta_world == 0.0f) {
		return;
	}
	vec3d mn, mx;
	getBound(box, &mn, &mx);

	float* target = nullptr;
	float other = 0.0f; // the opposing min/max on this axis, used for clamping
	if (corner == BoundCorner::Min) {
		switch (axis_index) {
		case 0: target = &mn.xyz.x; other = mx.xyz.x; break;
		case 1: target = &mn.xyz.y; other = mx.xyz.y; break;
		case 2: target = &mn.xyz.z; other = mx.xyz.z; break;
		default: return;
		}
	} else {
		switch (axis_index) {
		case 0: target = &mx.xyz.x; other = mn.xyz.x; break;
		case 1: target = &mx.xyz.y; other = mn.xyz.y; break;
		case 2: target = &mx.xyz.z; other = mn.xyz.z; break;
		default: return;
		}
	}

	float new_val = *target + delta_world;

	// Keep the box non-degenerate. Use _MIN_BOX_THICKNESS as the minimum gap
	// between min and max to avoid producing a zero- or negative-thickness
	// box on a fast drag; matches the validation threshold the dialog enforces
	// at apply time anyway.
	const float pad = static_cast<float>(_MIN_BOX_THICKNESS);
	if (corner == BoundCorner::Min) {
		new_val = std::min(new_val, other - pad);
	} else {
		new_val = std::max(new_val, other + pad);
	}

	if (new_val == *target) {
		return;
	}
	*target = new_val;

	// Re-stringify the moved component and write back through setBoxText so
	// the modify() / modelChanged() / spinbox-refresh path runs exactly as if
	// the user had typed the new value.
	QString s = QString::number(new_val, 'f', 1);
	int edit_index = -1;
	if (box == BoundBox::Outer) {
		if (corner == BoundCorner::Min) edit_index = _O_MIN_X + axis_index;
		else                            edit_index = _O_MAX_X + axis_index;
	} else {
		if (corner == BoundCorner::Min) edit_index = _I_MIN_X + axis_index;
		else                            edit_index = _I_MAX_X + axis_index;
	}
	setBoxText(s, static_cast<_box_line_edits>(edit_index));
}

void AsteroidEditorDialogModel::translateBound(BoundBox box, const vec3d& delta_world)
{
	// Translate is just six nudges, but applied atomically so we don't trip
	// the per-axis clamp when sliding the box wholesale.
	vec3d mn, mx;
	getBound(box, &mn, &mx);
	vm_vec_add2(&mn, &delta_world);
	vm_vec_add2(&mx, &delta_world);

	// Push each component via setBoxText so all the usual signals fire.
	int base_min, base_max;
	if (box == BoundBox::Outer) { base_min = _O_MIN_X; base_max = _O_MAX_X; }
	else                        { base_min = _I_MIN_X; base_max = _I_MAX_X; }

	setBoxText(QString::number(mn.xyz.x, 'f', 1), static_cast<_box_line_edits>(base_min + 0));
	setBoxText(QString::number(mn.xyz.y, 'f', 1), static_cast<_box_line_edits>(base_min + 1));
	setBoxText(QString::number(mn.xyz.z, 'f', 1), static_cast<_box_line_edits>(base_min + 2));
	setBoxText(QString::number(mx.xyz.x, 'f', 1), static_cast<_box_line_edits>(base_max + 0));
	setBoxText(QString::number(mx.xyz.y, 'f', 1), static_cast<_box_line_edits>(base_max + 1));
	setBoxText(QString::number(mx.xyz.z, 'f', 1), static_cast<_box_line_edits>(base_max + 2));
}

QString & AsteroidEditorDialogModel::getBoxText(_box_line_edits type)
{
	switch (type) {
		case _O_MIN_X: return _min_x;
		case _O_MIN_Y: return _min_y;
		case _O_MIN_Z: return _min_z;
		case _O_MAX_X: return _max_x;
		case _O_MAX_Y: return _max_y;
		case _O_MAX_Z: return _max_z;
		case _I_MIN_X: return _inner_min_x;
		case _I_MIN_Y: return _inner_min_y;
		case _I_MIN_Z: return _inner_min_z;
		case _I_MAX_X: return _inner_max_x;
		case _I_MAX_Y: return _inner_max_y;
		case _I_MAX_Z: return _inner_max_z;
		default:
			UNREACHABLE("Unknown asteroid coordinates enum value found (%i); Get a coder! ", type);
			return _min_x;
	}
}

void AsteroidEditorDialogModel::setAsteroidSelections(const QVector<bool>& selected)
{
	SCP_vector<SCP_string> selectedTypes;
	for (size_t i = 0; i < asteroidOptions.size(); ++i) {
		if (selected.at(static_cast<int>(i))) {
			selectedTypes.push_back(asteroidOptions[i]);
		}
	}

	modify(_field_asteroid_type, selectedTypes);
}

QVector<std::pair<QString, bool>> AsteroidEditorDialogModel::getAsteroidSelections() const
{
	QVector<std::pair<QString, bool>> options;
	for (const auto& name : asteroidOptions) {
		bool enabled = SCP_vector_contains(_field_asteroid_type, name);
		options.append({QString::fromStdString(name), enabled});
	}
	return options;
}

void AsteroidEditorDialogModel::setDebrisSelections(const QVector<bool>& selected)
{
	SCP_vector<int> selectedTypes;
	for (size_t i = 0; i < debrisOptions.size(); ++i) {
		if (selected.at(static_cast<int>(i))) {
			selectedTypes.push_back(debrisOptions[i].second);
		}
	}

	modify(_field_debris_type, selectedTypes);
}

QVector<std::pair<QString, bool>> AsteroidEditorDialogModel::getDebrisSelections() const
{
	QVector<std::pair<QString, bool>> options;
	for (const auto& setting : debrisOptions) {
		bool enabled = SCP_vector_contains(_field_debris_type, setting.second);
		options.append({QString::fromStdString(setting.first), enabled});
	}
	return options;
}

void AsteroidEditorDialogModel::setShipSelections(const QVector<bool>& selected)
{
	SCP_vector<SCP_string> selectedTypes;

	for (size_t i = 0; i < shipOptions.size(); ++i) {
		if (selected.at(static_cast<int>(i))) {
			selectedTypes.push_back(shipOptions[i]);
		}
	}

	modify(_field_target_names, selectedTypes);

	// Now we can clear the shipOptions vector since we're done with it
	shipOptions.clear();
}

QVector<std::pair<QString, bool>> AsteroidEditorDialogModel::getShipSelections()
{
	// Ships can be placed while the Asteroid field editor is open so we need to initialize this every time
	shipOptions.clear();
	for (auto& ship : Ships) {
		if (ship.objnum >= 0) {
			SCP_string name = ship.ship_name;
			shipOptions.push_back(name);
		}
	}

	QVector<std::pair<QString, bool>> options;
	for (const auto& name : shipOptions) {
		bool enabled = SCP_vector_contains(_field_target_names, name);
		options.append({QString::fromStdString(name), enabled});
	}
	return options;
}

} // namespace fso::fred::dialogs