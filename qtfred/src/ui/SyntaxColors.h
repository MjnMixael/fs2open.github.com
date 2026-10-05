#pragma once

#include <QColor>
#include <QObject>
#include <QString>

#include <array>
#include <optional>

namespace fso::fred {

// Token categories for mission-file text (the Events editor's Advanced view).
enum class SyntaxRole {
	SectionHeader,   // #Events
	FieldKey,        // $Formula:  +Name:
	Operator,        // a known SEXP operator
	UnknownOperator, // an operator position holding a name the engine doesn't know
	String,          // "quoted"
	Number,          // 100  -3.5
	Variable,        // @var  &container&
	VersionComment,  // ;;FSO 21.0.0;;
	Comment,         // ; line   /* block */   !* block *!
	Count
};

constexpr int SyntaxRoleCount = static_cast<int>(SyntaxRole::Count);

// Built-in color schemes. Each has a light and a dark version; the app's palette picks which shows.
enum class SyntaxScheme {
	Standard,
	Solarized, // https://ethanschoonover.com/solarized/
	Count
};

constexpr int SyntaxSchemeCount = static_cast<int>(SyntaxScheme::Count);

// The highlighted text box's background and plain (unhighlighted) text color. A scheme may bring its
// own (Solarized does); otherwise the box uses the app theme's colors.
struct SyntaxEditorColors {
	QColor background;
	QColor text;
};

// The user's changes to those two colors; an empty one follows the scheme, or the app theme.
struct SyntaxEditorOverrides {
	std::optional<QColor> background;
	std::optional<QColor> text;

	bool operator==(const SyntaxEditorOverrides& o) const { return background == o.background && text == o.text; }
	bool operator!=(const SyntaxEditorOverrides& o) const { return !(*this == o); }
};

struct SyntaxStyle {
	QColor color;
	bool bold = false;
	bool italic = false;

	bool operator==(const SyntaxStyle& o) const { return color == o.color && bold == o.bold && italic == o.italic; }
	bool operator!=(const SyntaxStyle& o) const { return !(*this == o); }
};

// The user's choices. Each scheme keeps its own changes, so switching schemes and back returns to
// them, and within a scheme they are per version (index 0 = light, 1 = dark), since a color that
// reads well on one rarely does on the other. A role with no override follows the scheme's color.
using SyntaxSchemeOverrides = std::array<std::array<std::optional<SyntaxStyle>, SyntaxRoleCount>, 2>;

struct SyntaxColorSettings {
	SyntaxScheme scheme = SyntaxScheme::Standard;
	std::array<SyntaxSchemeOverrides, SyntaxSchemeCount> overrides;
	// background and plain text, per scheme and version like the role colors
	std::array<std::array<SyntaxEditorOverrides, 2>, SyntaxSchemeCount> editorOverrides;
	bool rainbowParens = false;

	// Whether the user changed any color in this version of the scheme.
	bool hasOverrides(SyntaxScheme which, bool dark) const
	{
		const auto& editor = editorOverrides[static_cast<int>(which)][dark ? 1 : 0];
		if (editor.background || editor.text)
			return true;
		for (const auto& over : overrides[static_cast<int>(which)][dark ? 1 : 0]) {
			if (over)
				return true;
		}
		return false;
	}

	bool operator==(const SyntaxColorSettings& o) const
	{
		return scheme == o.scheme && overrides == o.overrides && editorOverrides == o.editorOverrides &&
			rainbowParens == o.rainbowParens;
	}
	bool operator!=(const SyntaxColorSettings& o) const { return !(*this == o); }
};

// Shared syntax color scheme: defaults, the saved user settings, and a change
// notification so open highlighters restyle when Preferences changes it.
class SyntaxColorScheme : public QObject {
	Q_OBJECT

  public:
	static SyntaxColorScheme& instance();

	static QString roleLabel(SyntaxRole role);
	static QString schemeLabel(SyntaxScheme scheme);
	// The scheme's own style for a role, before any change the user made.
	static SyntaxStyle defaultStyle(SyntaxScheme scheme, SyntaxRole role, bool dark);
	// Rainbow parenthesis color for a nesting depth (0 = outermost), from the scheme's palette.
	static QColor parenColor(SyntaxScheme scheme, int depth, bool dark);
	// The scheme's own background and plain text color, or the app theme's for a scheme without
	// them (Standard); hasOwn says which.
	static SyntaxEditorColors defaultEditorColors(SyntaxScheme scheme, bool dark, bool* hasOwn = nullptr);
	// Whether the application palette is currently dark.
	static bool paletteIsDark();

	const SyntaxColorSettings& settings() const { return _settings; }
	// Replaces the settings, saves them, and notifies if anything changed.
	void setSettings(const SyntaxColorSettings& settings);

	// The effective style in the chosen scheme: the user's override for this version, else the scheme's.
	SyntaxStyle style(SyntaxRole role, bool dark) const;
	SyntaxScheme scheme() const { return _settings.scheme; }
	// The text box colors to apply, or nothing when the box should simply follow the app theme
	// (a scheme without its own colors and no change by the user).
	std::optional<SyntaxEditorColors> editorColors(bool dark) const;
	bool rainbowParens() const { return _settings.rainbowParens; }

  signals:
	void changed();

  private:
	SyntaxColorScheme();
	void load();
	void save() const;

	SyntaxColorSettings _settings;
};

} // namespace fso::fred
