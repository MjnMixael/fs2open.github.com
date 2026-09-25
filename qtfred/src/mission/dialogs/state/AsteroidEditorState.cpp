// captureState() and restoreState() for AsteroidEditorDialogModel.
// Reads/writes Asteroid_field directly so undo/redo bypasses the model's
// working copy.

#include <mission/dialogs/AsteroidEditorDialogModel.h>
#include <mission/EditorViewport.h>

#include <asteroid/asteroid.h>

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QString>

namespace fso::fred::dialogs {

static void serializeVec3d(QDataStream& ds, const vec3d& v)
{
	ds << v.xyz.x << v.xyz.y << v.xyz.z;
}

static void deserializeVec3d(QDataStream& ds, vec3d& v)
{
	ds >> v.xyz.x >> v.xyz.y >> v.xyz.z;
}

QByteArray AsteroidEditorDialogModel::captureGlobalState()
{
	QByteArray data;
	QDataStream ds(&data, QIODevice::WriteOnly);

	const asteroid_field& f = Asteroid_field;

	ds << static_cast<qint32>(f.num_initial_asteroids);
	ds << static_cast<qint32>(f.field_type);
	ds << static_cast<qint32>(f.debris_genre);
	ds << static_cast<qint8>(f.has_inner_bound ? 1 : 0);
	ds << static_cast<qint8>(f.enhanced_visibility_checks ? 1 : 0);
	ds << f.bound_rad;
	ds << f.speed;

	serializeVec3d(ds, f.vel);
	serializeVec3d(ds, f.min_bound);
	serializeVec3d(ds, f.max_bound);
	serializeVec3d(ds, f.inner_min_bound);
	serializeVec3d(ds, f.inner_max_bound);

	// field_debris_type: vector<int>
	ds << static_cast<qint32>(static_cast<int>(f.field_debris_type.size()));
	for (int idx : f.field_debris_type)
		ds << static_cast<qint32>(idx);

	// field_asteroid_type: vector<SCP_string>
	ds << static_cast<qint32>(static_cast<int>(f.field_asteroid_type.size()));
	for (const auto& s : f.field_asteroid_type)
		ds << QString(s.c_str());

	// target_names: vector<SCP_string>
	ds << static_cast<qint32>(static_cast<int>(f.target_names.size()));
	for (const auto& s : f.target_names)
		ds << QString(s.c_str());

	return data;
}

void AsteroidEditorDialogModel::restoreGlobalState(const QByteArray& state)
{
	QDataStream ds(state);

	asteroid_field& f = Asteroid_field;

	qint32 i32;
	qint8  i8;
	float  fval;

	ds >> i32; f.num_initial_asteroids = i32;
	ds >> i32; f.field_type   = static_cast<field_type_t>(i32);
	ds >> i32; f.debris_genre = static_cast<debris_genre_t>(i32);
	ds >> i8;  f.has_inner_bound              = (i8 != 0);
	ds >> i8;  f.enhanced_visibility_checks   = (i8 != 0);
	ds >> fval; f.bound_rad = fval;
	ds >> fval; f.speed     = fval;

	deserializeVec3d(ds, f.vel);
	deserializeVec3d(ds, f.min_bound);
	deserializeVec3d(ds, f.max_bound);
	deserializeVec3d(ds, f.inner_min_bound);
	deserializeVec3d(ds, f.inner_max_bound);

	// field_debris_type
	ds >> i32;
	f.field_debris_type.clear();
	f.field_debris_type.reserve(static_cast<size_t>(i32));
	for (int j = 0; j < i32; j++) {
		qint32 idx;
		ds >> idx;
		f.field_debris_type.push_back(static_cast<int>(idx));
	}

	// field_asteroid_type
	ds >> i32;
	f.field_asteroid_type.clear();
	f.field_asteroid_type.reserve(static_cast<size_t>(i32));
	for (int j = 0; j < i32; j++) {
		QString s;
		ds >> s;
		f.field_asteroid_type.emplace_back(s.toUtf8().constData());
	}

	// target_names
	ds >> i32;
	f.target_names.clear();
	f.target_names.reserve(static_cast<size_t>(i32));
	for (int j = 0; j < i32; j++) {
		QString s;
		ds >> s;
		f.target_names.emplace_back(s.toUtf8().constData());
	}
}

QByteArray AsteroidEditorDialogModel::captureGizmoState()
{
	QByteArray data;
	QDataStream ds(&data, QIODevice::WriteOnly);
	serializeVec3d(ds, Asteroid_field.min_bound);
	serializeVec3d(ds, Asteroid_field.max_bound);
	serializeVec3d(ds, Asteroid_field.inner_min_bound);
	serializeVec3d(ds, Asteroid_field.inner_max_bound);
	return data;
}

void AsteroidEditorDialogModel::restoreGizmoState(const QByteArray& state)
{
	QDataStream ds(state);
	deserializeVec3d(ds, Asteroid_field.min_bound);
	deserializeVec3d(ds, Asteroid_field.max_bound);
	deserializeVec3d(ds, Asteroid_field.inner_min_bound);
	deserializeVec3d(ds, Asteroid_field.inner_max_bound);
}

QByteArray AsteroidEditorDialogModel::captureOriginalState() const
{
	// The globals hold this dialog's live preview, so swap the open-time
	// snapshot in just long enough to serialize it.
	const asteroid_field live = Asteroid_field;
	Asteroid_field = _original_a_field;
	QByteArray data = captureGlobalState();
	Asteroid_field = live;
	return data;
}

// The AbstractDialogModel overrides just delegate: the snapshot is of the
// mission globals, not of this model's working copy, so an undo command can
// restore it with no dialog alive.
QByteArray AsteroidEditorDialogModel::captureState() const
{
	return captureGlobalState();
}

void AsteroidEditorDialogModel::restoreState(const QByteArray& state)
{
	restoreGlobalState(state);

	// This model belongs to a closed dialog's undo command. If an asteroid
	// dialog is open now, its working copy is stale, so reload it.
	if (_viewport != nullptr) {
		if (auto* open = _viewport->asteroidEditModel()) {
			open->reloadFromGlobals();
		}
	}
}

} // namespace fso::fred::dialogs
