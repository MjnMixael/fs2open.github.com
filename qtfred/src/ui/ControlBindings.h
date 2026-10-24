#pragma once

#include <QKeyEvent>
#include <QKeySequence>
#include <QString>

#include <array>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace fso::fred {

enum class ControlAction {
	MoveLeft,
	MoveRight,
	MoveForward,
	MoveBackward,
	MoveUp,
	MoveDown,
	YawLeft,
	YawRight,
	PitchUp,
	PitchDown,
	ToggleSelectionLock,
};

// The Ctrl+click combinations that place a new object in the viewport
enum class PlacementModifier {
	Ctrl,
	CtrlShift,
	CtrlAlt,
};

// Which combination places each kind of object, indexed like CreateKind (ship, prop, other).
// The three always differ, so every kind can be placed.
constexpr int NumPlacementKinds = 3;
using PlacementModifiers = std::array<PlacementModifier, NumPlacementKinds>;

struct ControlBindingDefinition {
	ControlAction action;
	QString id;
	QString label;
	QKeySequence defaultKey;
};

class ControlBindings {
 public:
	static ControlBindings& instance();

	const std::vector<ControlBindingDefinition>& definitions() const;
	QKeySequence keyFor(ControlAction action) const;
	void setKey(ControlAction action, const QKeySequence& sequence);
	void resetToDefaults();
	void load();
	void save() const;

	bool handleKeyPress(QKeyEvent* event);
	bool handleKeyRelease(QKeyEvent* event);
	bool matches(QKeyEvent* event) const;

	bool isPressed(ControlAction action) const;
	bool takeTriggered(ControlAction action);

	static PlacementModifiers defaultPlacementModifiers();
	const PlacementModifiers& placementModifiers() const { return _placement; }
	void setPlacementModifiers(const PlacementModifiers& placement);
	// The kind (a CreateKind value) a Ctrl+click with these modifiers places. Ctrl+Alt+Shift counts
	// as plain Ctrl.
	int placementKind(Qt::KeyboardModifiers modifiers) const;
	// "Ctrl+click", "Ctrl+Shift+click" or "Ctrl+Alt+click"
	static QString placementText(PlacementModifier modifier);

 private:
	ControlBindings();

	static int normalizedCode(const QKeySequence& sequence);
	static int normalizedCode(const QKeyEvent* event);
	void rebuildReverseMap();

	std::vector<ControlBindingDefinition> _definitions;
	std::map<ControlAction, QKeySequence> _bindings;
	std::unordered_map<int, std::vector<ControlAction>> _actionByCode;
	std::set<ControlAction> _pressedActions;
	std::set<ControlAction> _triggeredActions;
	PlacementModifiers _placement = defaultPlacementModifiers();
};

} // namespace fso::fred
