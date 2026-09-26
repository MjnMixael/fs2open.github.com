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

struct SyntaxStyle {
	QColor color;
	bool bold = false;
	bool italic = false;

	bool operator==(const SyntaxStyle& o) const { return color == o.color && bold == o.bold && italic == o.italic; }
	bool operator!=(const SyntaxStyle& o) const { return !(*this == o); }
};

// The user's choices. Colors are per theme (index 0 = light, 1 = dark), since a
// color that reads well on one rarely does on the other. A role with no override
// follows the theme default, so switching themes never leaves a stale custom color.
struct SyntaxColorSettings {
	std::array<std::array<std::optional<SyntaxStyle>, SyntaxRoleCount>, 2> overrides;
	bool rainbowParens = false;

	bool operator==(const SyntaxColorSettings& o) const
	{
		return overrides == o.overrides && rainbowParens == o.rainbowParens;
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
	static SyntaxStyle defaultStyle(SyntaxRole role, bool dark);
	// Rainbow parenthesis color for a nesting depth (0 = outermost).
	static QColor parenColor(int depth, bool dark);
	// Whether the application palette is currently dark.
	static bool paletteIsDark();

	const SyntaxColorSettings& settings() const { return _settings; }
	// Replaces the settings, saves them, and notifies if anything changed.
	void setSettings(const SyntaxColorSettings& settings);

	// The effective style: the override for this theme, else the default.
	SyntaxStyle style(SyntaxRole role, bool dark) const;
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
