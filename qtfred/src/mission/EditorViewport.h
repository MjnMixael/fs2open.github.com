#pragma once


#include "CameraController.h"
#include "FredRenderer.h"
#include "Editor.h"
#include "IDialogProvider.h"
#include "ui/ThemeMode.h"
#include "ViewportHandle.h"

#include <object/object.h>

namespace fso::fred {

// Defined in Editor.h. Forward-declared here because Editor.h and
// EditorViewport.h include each other: when a TU enters Editor.h first, this
// header is pulled in before Editor.h's full enum definition. Only used as a
// return type in a declaration below, so a forward declaration suffices.
enum class EnvironmentObject;

struct Marking_box {
	int x1 = 0;
	int y1 = 0;
	int x2 = 0;
	int y2 = 0;
};

enum class CreateKind {
	Ship,
	Prop,
	Other,
};

enum class OtherKind {
	Waypoint,
	JumpNode,
};

struct ViewSettings {
	bool Universal_heading = false;
	bool Show_stars = true;
	bool Show_horizon = false;
	bool Show_grid = true;
	bool Show_distances = true;
	bool Show_coordinates = false;
	bool Show_outlines = false;
	bool Draw_outlines_on_selected_ships = true;
	bool Draw_outline_at_warpin_position = false;
	bool Show_grid_positions = true;
	bool Show_dock_points = false;
	bool Show_bay_paths = false;
	bool Show_starts = true;
	bool Show_ships = true;
	SCP_vector<bool> Show_iff;
	bool Show_ship_info = true;
	bool Show_ship_models = true;
	bool Show_paths_fred = false;
	bool Lighting_on = false;
	bool FullDetail = false;
	bool Show_waypoints = true;
	bool Show_props = true;
	bool Show_jump_nodes = true;
	bool Show_compass = true;
	bool Highlight_selectable_subsys = false;
	int Outline_lod = 1;

	ViewSettings();
};

class EditorViewport {
	std::unique_ptr<FredRenderer> _renderer; //!< Internal, owned pointer

	int Last_cursor_over = -1;

	void process_system_keys();
	void level_object(matrix* orient);

	void initialSetup();

	SCP_vector<SCP_string> _layerNames;
	SCP_vector<bool> _layerVisibility;
	std::unordered_map<int, size_t> _objectLayers;

	size_t getLayerIndex(const SCP_string& name) const;
	size_t getObjectLayerIndex(int objectIndex) const;
	bool isLayerVisible(size_t layerIndex) const;
	void syncMissionLayerNames() const;
	void setObjectLayerByIndex(int objectIndex, size_t layerIndex);

 public:
	class ViewportControlLock {
	  public:
		explicit ViewportControlLock(EditorViewport* viewport);
		~ViewportControlLock();

		ViewportControlLock(const ViewportControlLock&) = delete;
		ViewportControlLock& operator=(const ViewportControlLock&) = delete;

		ViewportControlLock(ViewportControlLock&& other) noexcept;
		ViewportControlLock& operator=(ViewportControlLock&& other) noexcept;

	  private:
		EditorViewport* _viewport = nullptr;
	};

	static const char* DefaultLayerName;

	enum {
		DUP_DRAG_OF_WING = 2,
		// Ctrl+Shift+drag.  Same as a normal Ctrl+drag duplicate, except marked
		// waypoints insert a copy into their source path rather than starting a
		// new path.  Non-waypoint object types behave like a plain duplicate.
		DUP_DRAG_INSERT = 3,
	};

	EditorViewport(Editor* in_editor, std::unique_ptr<FredRenderer>&& in_renderer);

	void needsUpdate();
	bool areControlsLocked() const;
	[[nodiscard]] ViewportControlLock acquireControlLock();

	void reset();

	void select_objects(const Marking_box& box);

	void game_do_frame(const int cur_object_index);

	vec3d orbitCameraGetPivot();

	int object_check_collision(object* objp, vec3d* p0, vec3d* p1, vec3d* hitpos);

	int select_object(int cx, int cy);

	// Viewport handle (non-object selectable marker) API. Dialogs register a
	// group of handles while open; the picking pre-pass below runs before
	// select_object() so a handle click never falls through into normal mission
	// object selection.
	HandleGroupId registerHandleGroup(std::vector<ViewportHandle> handles);
	void updateHandleGroup(HandleGroupId id, std::vector<ViewportHandle> handles);
	void unregisterHandleGroup(HandleGroupId id);
	const std::vector<std::vector<ViewportHandle>>& getHandleGroups() const { return _handle_groups; }

	// Returns {group_index, handle_index} or {-1, -1} if nothing within pick
	// radius. group_index is the slot in _handle_groups, NOT the generation id.
	struct HandlePick { int group_index = -1; int handle_index = -1; };
	HandlePick pick_handle(int cx, int cy) const;

	// Begin/continue/end a handle drag. begin_handle_drag records the anchor
	// point on the constraint plane; drag_handle delivers per-tick deltas via
	// the handle's on_drag callback. Returns false if the active handle was
	// invalidated (e.g. its group was unregistered mid-drag).
	bool begin_handle_drag(HandlePick pick, int cx, int cy);
	bool drag_handle(int cx, int cy);
	void end_handle_drag();
	// Genuine mouse-release end: fires the active handle's on_release (if any)
	// before clearing the drag. Distinct from end_handle_drag(), which is the
	// cancel path (Escape / right-click) and skips on_release.
	void commit_handle_drag();
	bool has_active_handle_drag() const { return _active_handle.group_index >= 0; }

	// Hovered-handle tracking for the in-scene hover balloon. RenderWidget sets
	// this on mouse-move; FredRenderer reads it to draw the infobox.
	void setHoveredHandle(HandlePick pick) { _hovered_handle = pick; }
	HandlePick getHoveredHandle() const { return _hovered_handle; }

	// Viewport-owned volumetric nebula gizmo. Unlike the asteroid handles (owned
	// by their dialog), this one is always present whenever the mission has an
	// enabled volumetric with a hull, so the nebula can be dragged with no
	// dialog open. refreshVolumetricHandle() rebuilds it from The_mission when
	// its state actually changes (cheap no-op otherwise); it is called each
	// frame from the renderer. Dragging is a direct edit that marks the mission
	// modified; the editor dialog is modal, so it cannot overlap a drag.
	void refreshVolumetricHandle();

	// Viewport-owned asteroid-field gizmos (outer box, and inner box when
	// enabled): 6 face + 8 corner + 1 center handle per box, rebuilt from
	// Asteroid_field. Same always-on / direct-edit / modal-dialog model as the
	// volumetric handle. Called each frame from the renderer (dirty-checked).
	void refreshAsteroidHandles();

	// Which environment entity (if any) a picked handle belongs to. The
	// viewport-owned volumetric and asteroid gizmos map to one; anything else
	// returns None. Used by the widget to drive environment selection.
	EnvironmentObject handleEnvironment(HandlePick pick) const;

	// The specific handle the transform-toolbar spinboxes act on (for the
	// asteroid field, which has many handles). Set on a viewport handle click;
	// cleared to fall back to the field's outer-box center.
	void setSelectedHandle(HandlePick pick) { _selected_handle = pick; }
	void clearSelectedHandle() { _selected_handle = HandlePick{}; }

	// Read/write the currently targeted asteroid handle for the spinboxes.
	// asteroidSpinboxTarget fills the handle's world position and its editable
	// axis bitmask, defaulting to the outer-box center; returns false if there
	// is no asteroid field. applyAsteroidSpinbox moves that handle so its
	// position becomes new_pos (delta routed through the handle's on_drag, so
	// clamping and mission-modified marking happen there).
	bool asteroidSpinboxTarget(vec3d* out_pos, int* out_movable_axes) const;
	void applyAsteroidSpinbox(const vec3d& new_pos);

	SCP_vector<SCP_string> getLayerNames() const;
	bool addLayer(const SCP_string& name, SCP_string* errorMessage = nullptr);
	bool deleteLayer(const SCP_string& name, SCP_string* errorMessage = nullptr);
	bool setLayerVisibility(const SCP_string& name, bool visible, SCP_string* errorMessage = nullptr);
	bool getLayerVisibility(const SCP_string& name, bool* visible, SCP_string* errorMessage = nullptr) const;
	void showAllLayers();
	int getHiddenLayerCount() const;
	void reloadLayersFromMission();

	SCP_string getObjectLayerName(int objectIndex) const;
	bool moveObjectToLayer(int objectIndex, const SCP_string& layerName, SCP_string* errorMessage = nullptr);
	void moveMarkedObjectsToLayer(const SCP_string& layerName, SCP_string* errorMessage = nullptr);

	// Sync the _objectLayers map for a freshly-created object by reading its
	// per-object fred_layer string (or the parent waypoint list's, for waypoints).
	// Call this after creating a new ship/prop/jump-node/waypoint outside of
	// the normal mission-load path (e.g. when duplicating objects).
	void registerObjectInLayer(int objectIndex);

	bool isObjectVisibleInLayer(const object* objp) const;


	// viewpoint -> attach camera to current ship.
	// cur_obj -> ship viewed.
	void level_controlled();
	void verticalize_controlled();

	void drag_rotate_save_backup();

	int create_object_on_grid(int x, int y, int waypoint_instance);
	int create_object_on_grid(int x, int y, int waypoint_instance, CreateKind kind);

	int	create_object(vec3d *pos, int waypoint_instance = -1, CreateKind kind = CreateKind::Ship);

	vec3d getCreatePosition(int x, int y, float fallbackDist);
	int createShipAtScreenPos(int x, int y, int modelIndex);
	int createPropAtScreenPos(int x, int y, int propIndex);
	int createWaypointAtScreenPos(int x, int y, int waypoint_instance = -1);
	int createJumpNodeAtScreenPos(int x, int y);

	// When `insert_waypoints` is true, marked waypoints get a new waypoint
	// inserted into their source path (right after the source waypoint) instead
	// of being duplicated into a fresh path.  Other object types are duplicated
	// either way.  Triggered from Ctrl+Shift+drag in the viewport.
	int duplicate_marked_objects(bool insert_waypoints = false);
	int drag_objects(int x, int y);

	int drag_rotate_objects(int mouse_dx, int mouse_dy);
	void cancel_drag();

	void view_universe(bool just_marked);

	void view_object(int obj_num);

	CameraController camera;

	ViewSettings view;

	int Cursor_over = -1;
	CursorMode Editing_mode = CursorMode::Moving;

	grid* The_grid;

	vec3d Constraint;
	vec3d Anticonstraint;
	bool Single_axis_constraint = false;

	bool Selection_lock = false;

	bool button_down = false;
	int on_object = -1;
	int Dup_drag = 0;

	int cur_model_index = 0;
	int cur_prop_index = -1;
	OtherKind cur_other_kind = OtherKind::Waypoint;

	object_orient_pos rotation_backup[MAX_OBJECTS];

	vec3d original_pos = vmd_zero_vector;

	bool moved = false;

	int Duped_wing;

	bool Group_rotate = true;
	int  toolbar_icon_size = 24;  ///< Toolbar icon size in pixels (16, 24, or 32)
	int  sexp_number_every_n = 5; ///< Show a numbered badge on every Nth argument in sexp trees (0 = disabled)
	bool Offer_autosave_recovery   = true;
	int  autosave_interval_seconds = 300;  // 5 minutes; 0 = disabled
	bool Create_bak_on_save        = true;
	bool Move_ships_when_undocking = true;
	bool Always_save_display_names = false;
	bool Error_checker_checks_potential_issues = true;
	bool Error_checker_apply_auto_corrections = true;
	// One-shot override: when set, the next auto-run of the error checker shows
	// the dialog and forces potential issues on regardless of the user's saved
	// preference. Consumed (cleared) by autoRunErrorChecker. Not persisted.
	bool Error_checker_force_display_potentials_once = false;

	bool Show_sexp_help_mission_events = true;
	bool Show_sexp_help_mission_goals = true;
	bool Show_sexp_help_mission_cutscenes = true;
	bool Show_sexp_help_ship_editor = false;
	bool Show_sexp_help_wing_editor = false;

	ThemeMode Theme_mode = ThemeMode::System;

	void saveSettings() const;

	Editor* editor = nullptr;
	FredRenderer* renderer = nullptr;
	IDialogProvider* dialogProvider = nullptr;

private:
	fix _lasttime = 0;
	vec3d Last_control_pos = vmd_zero_vector;
	matrix Last_control_orient = vmd_identity_matrix;
	int _controlLockCount = 0;

	bool incMissionTime();
	void loadSettings();

	void lockControls();
	void unlockControls();

	// Handle registry. _handle_group_generations[i] increments every time slot
	// i is freed so a stale HandleGroupId pointing at the recycled slot fails
	// the generation check in unregisterHandleGroup / updateHandleGroup.
	std::vector<std::vector<ViewportHandle>> _handle_groups;
	std::vector<int> _handle_group_generations;

	// Active handle drag state. group_index = -1 means no drag in progress.
	HandlePick _active_handle{};
	int _active_handle_generation = 0;
	vec3d _active_handle_last_world = vmd_zero_vector;

	// Handle currently under the cursor (for the hover balloon). {-1,-1} = none.
	// _last_hovered_handle lets game_do_frame schedule a repaint when the hover
	// changes, mirroring Cursor_over / Last_cursor_over for objects.
	HandlePick _hovered_handle{};
	HandlePick _last_hovered_handle{};

	// Viewport-owned volumetric gizmo state.
	HandleGroupId _volumetric_handle_group;
	// Cache so refreshVolumetricHandle() only touches the registry (and thus
	// requests a repaint) when the rendered state actually changes; otherwise
	// per-frame refresh would loop forever via needsUpdate().
	bool _vol_handle_cached_present = false;
	vec3d _vol_handle_cached_pos = vmd_zero_vector;
	SCP_string _vol_handle_cached_label;
	int _vol_handle_cached_color = -1;
	bool _vol_handle_cached_selected = false;

	// Viewport-owned asteroid gizmo state, plus a dirty cache (bounds + toggles
	// + selected) to avoid the per-frame repaint loop, and the outer-box center
	// handle index (spinbox default target).
	HandleGroupId _asteroid_handle_group;
	int _asteroid_center_index = -1;
	bool _ast_handle_cached_present = false;
	bool _ast_handle_cached_inner = false;
	bool _ast_handle_cached_selected = false;
	int _ast_handle_cached_target = -1;
	vec3d _ast_handle_cached_bounds[4] = {vmd_zero_vector, vmd_zero_vector, vmd_zero_vector, vmd_zero_vector};

	// The transform-toolbar's target handle across all groups. {-1,-1} = none.
	HandlePick _selected_handle{};

	// Compute the world-space point under the mouse cursor on the same
	// constraint plane that drag_objects() uses (centered on `anchor`).
	// Returns false if the intersection is behind the camera or invalid.
	bool screen_to_constraint_plane(int cx, int cy, const vec3d& anchor, vec3d* out_world) const;
};

} // namespace fso::fred
