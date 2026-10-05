#include "ui/SyntaxColors.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>

namespace fso::fred {

namespace {

constexpr auto SETTINGS_GROUP = "Preferences";
constexpr auto SYNTAX_GROUP = "syntax_colors";
constexpr auto RAINBOW_KEY = "syntax_rainbow_parens";
constexpr auto SCHEME_KEY = "syntax_scheme";
constexpr auto BACKGROUND_KEY = "editor_background";
constexpr auto TEXT_KEY = "editor_text";

// Stable settings names; don't rename, or saved choices are lost. Standard's changes stay where they
// were saved before there were schemes (syntax_colors/light|dark); other schemes nest one level deeper.
const char* schemeKey(SyntaxScheme scheme)
{
	switch (scheme) {
	case SyntaxScheme::Standard:  return "standard";
	case SyntaxScheme::Solarized: return "solarized";
	case SyntaxScheme::Count:     break;
	}
	return "standard";
}

QString schemeGroupPrefix(SyntaxScheme scheme)
{
	return scheme == SyntaxScheme::Standard ? QString() : QStringLiteral("%1/").arg(schemeKey(scheme));
}

// Stable settings keys; don't rename, or saved colors are lost.
const char* roleKey(SyntaxRole role)
{
	switch (role) {
	case SyntaxRole::SectionHeader:   return "section_header";
	case SyntaxRole::FieldKey:        return "field_key";
	case SyntaxRole::Operator:        return "operator";
	case SyntaxRole::UnknownOperator: return "unknown_operator";
	case SyntaxRole::String:          return "string";
	case SyntaxRole::Number:          return "number";
	case SyntaxRole::Variable:        return "variable";
	case SyntaxRole::VersionComment:  return "version_comment";
	case SyntaxRole::Comment:         return "comment";
	case SyntaxRole::Count:           break;
	}
	return "unknown";
}

// Saved as "#rrggbb|bold|italic".
QString encodeStyle(const SyntaxStyle& style)
{
	return QStringLiteral("%1|%2|%3").arg(style.color.name()).arg(style.bold ? 1 : 0).arg(style.italic ? 1 : 0);
}

std::optional<SyntaxStyle> decodeStyle(const QString& text)
{
	const auto parts = text.split(QLatin1Char('|'));
	if (parts.size() != 3)
		return std::nullopt;
	SyntaxStyle style;
	style.color = QColor(parts[0]);
	if (!style.color.isValid())
		return std::nullopt;
	style.bold = parts[1] == QLatin1String("1");
	style.italic = parts[2] == QLatin1String("1");
	return style;
}

SyntaxStyle standardStyle(SyntaxRole role, bool dark)
{
	switch (role) {
	case SyntaxRole::SectionHeader:
		return {dark ? QColor(0xC5, 0x86, 0xC0) : QColor(0xAF, 0x00, 0xDB), true, false};
	case SyntaxRole::FieldKey:
		return {dark ? QColor(0x56, 0x9C, 0xD6) : QColor(0x00, 0x00, 0xFF), false, false};
	case SyntaxRole::Operator:
		return {dark ? QColor(0xDC, 0xDC, 0xAA) : QColor(0x79, 0x5E, 0x26), true, false};
	case SyntaxRole::UnknownOperator:
		return {dark ? QColor(0xF4, 0x47, 0x47) : QColor(0xCD, 0x31, 0x31), true, false};
	case SyntaxRole::String:
		return {dark ? QColor(0xCE, 0x91, 0x78) : QColor(0xA3, 0x15, 0x15), false, false};
	case SyntaxRole::Number:
		return {dark ? QColor(0xB5, 0xCE, 0xA8) : QColor(0x09, 0x86, 0x58), false, false};
	case SyntaxRole::Variable:
		return {dark ? QColor(0x4E, 0xC9, 0xB0) : QColor(0x26, 0x7F, 0x99), false, false};
	case SyntaxRole::VersionComment:
		return {dark ? QColor(0xD7, 0xBA, 0x7D) : QColor(0xB8, 0x86, 0x0B), false, true};
	case SyntaxRole::Comment:
		return {dark ? QColor(0x6A, 0x99, 0x55) : QColor(0x00, 0x80, 0x00), false, true};
	case SyntaxRole::Count:
		break;
	}
	return {};
}

// Solarized's accent colors are made to read on both its light and dark backgrounds, so only the
// comment color (a base tone) differs between the two versions.
namespace solarized {
const QColor Yellow(0xB5, 0x89, 0x00);
const QColor Orange(0xCB, 0x4B, 0x16);
const QColor Red(0xDC, 0x32, 0x2F);
const QColor Magenta(0xD3, 0x36, 0x82);
const QColor Violet(0x6C, 0x71, 0xC4);
const QColor Blue(0x26, 0x8B, 0xD2);
const QColor Cyan(0x2A, 0xA1, 0x98);
const QColor Green(0x85, 0x99, 0x00);
const QColor Base03(0x00, 0x2B, 0x36); // dark background
const QColor Base01(0x58, 0x6E, 0x75); // comments on the dark background
const QColor Base00(0x65, 0x7B, 0x83); // text on the light background
const QColor Base0(0x83, 0x94, 0x96);  // text on the dark background
const QColor Base1(0x93, 0xA1, 0xA1);  // comments on the light background
const QColor Base3(0xFD, 0xF6, 0xE3);  // light background
} // namespace solarized

SyntaxStyle solarizedStyle(SyntaxRole role, bool dark)
{
	switch (role) {
	case SyntaxRole::SectionHeader:   return {solarized::Violet, true, false};
	case SyntaxRole::FieldKey:        return {solarized::Blue, false, false};
	case SyntaxRole::Operator:        return {solarized::Green, true, false};
	case SyntaxRole::UnknownOperator: return {solarized::Red, true, false};
	case SyntaxRole::String:          return {solarized::Cyan, false, false};
	case SyntaxRole::Number:          return {solarized::Magenta, false, false};
	case SyntaxRole::Variable:        return {solarized::Orange, false, false};
	case SyntaxRole::VersionComment:  return {solarized::Yellow, false, true};
	case SyntaxRole::Comment:         return {dark ? solarized::Base01 : solarized::Base1, false, true};
	case SyntaxRole::Count:           break;
	}
	return {};
}

} // namespace

SyntaxColorScheme& SyntaxColorScheme::instance()
{
	static SyntaxColorScheme scheme;
	return scheme;
}

SyntaxColorScheme::SyntaxColorScheme()
{
	load();
}

QString SyntaxColorScheme::roleLabel(SyntaxRole role)
{
	switch (role) {
	case SyntaxRole::SectionHeader:   return tr("Section headers");
	case SyntaxRole::FieldKey:        return tr("Field names");
	case SyntaxRole::Operator:        return tr("Operators");
	case SyntaxRole::UnknownOperator: return tr("Unknown operators");
	case SyntaxRole::String:          return tr("Strings");
	case SyntaxRole::Number:          return tr("Numbers");
	case SyntaxRole::Variable:        return tr("Variables and containers");
	case SyntaxRole::VersionComment:  return tr("Version tags");
	case SyntaxRole::Comment:         return tr("Comments");
	case SyntaxRole::Count:           break;
	}
	return {};
}

QString SyntaxColorScheme::schemeLabel(SyntaxScheme scheme)
{
	switch (scheme) {
	case SyntaxScheme::Standard:  return tr("Standard");
	case SyntaxScheme::Solarized: return tr("Solarized");
	case SyntaxScheme::Count:     break;
	}
	return {};
}

SyntaxEditorColors SyntaxColorScheme::defaultEditorColors(SyntaxScheme scheme, bool dark, bool* hasOwn)
{
	if (hasOwn != nullptr)
		*hasOwn = scheme == SyntaxScheme::Solarized;
	if (scheme == SyntaxScheme::Solarized)
		return {dark ? solarized::Base03 : solarized::Base3, dark ? solarized::Base0 : solarized::Base00};
	const QPalette& app = qApp->palette();
	return {app.color(QPalette::Base), app.color(QPalette::Text)};
}

std::optional<SyntaxEditorColors> SyntaxColorScheme::editorColors(bool dark) const
{
	bool hasOwn = false;
	SyntaxEditorColors colors = defaultEditorColors(_settings.scheme, dark, &hasOwn);
	const auto& over = _settings.editorOverrides[static_cast<int>(_settings.scheme)][dark ? 1 : 0];
	if (!hasOwn && !over.background && !over.text)
		return std::nullopt;
	if (over.background)
		colors.background = *over.background;
	if (over.text)
		colors.text = *over.text;
	return colors;
}

SyntaxStyle SyntaxColorScheme::defaultStyle(SyntaxScheme scheme, SyntaxRole role, bool dark)
{
	return scheme == SyntaxScheme::Solarized ? solarizedStyle(role, dark) : standardStyle(role, dark);
}

QColor SyntaxColorScheme::parenColor(SyntaxScheme scheme, int depth, bool dark)
{
	static const QColor darkColors[] = {QColor(0xFF, 0xD7, 0x00), QColor(0xDA, 0x70, 0xD6), QColor(0x17, 0x9F, 0xFF),
		QColor(0x4E, 0xC9, 0x7E), QColor(0xFF, 0x8C, 0x42), QColor(0x9C, 0xDC, 0xFE)};
	static const QColor lightColors[] = {QColor(0x04, 0x31, 0xFA), QColor(0x31, 0x93, 0x31), QColor(0x7B, 0x38, 0x14),
		QColor(0xA0, 0x1B, 0x9E), QColor(0x00, 0x7A, 0x7A), QColor(0xB3, 0x5A, 0x00)};
	// Solarized: its accents, the same in both versions
	static const QColor solarizedColors[] = {solarized::Yellow, solarized::Orange, solarized::Magenta,
		solarized::Violet, solarized::Blue, solarized::Cyan};
	constexpr int count = 6;
	const int i = ((depth % count) + count) % count;
	if (scheme == SyntaxScheme::Solarized)
		return solarizedColors[i];
	return dark ? darkColors[i] : lightColors[i];
}

bool SyntaxColorScheme::paletteIsDark()
{
	// Same test the graph views use.
	return qApp->palette().window().color().value() < 128;
}

SyntaxStyle SyntaxColorScheme::style(SyntaxRole role, bool dark) const
{
	const int r = static_cast<int>(role);
	if (r < 0 || r >= SyntaxRoleCount)
		return {};
	const auto& over = _settings.overrides[static_cast<int>(_settings.scheme)][dark ? 1 : 0][r];
	return over ? *over : defaultStyle(_settings.scheme, role, dark);
}

void SyntaxColorScheme::setSettings(const SyntaxColorSettings& settings)
{
	if (settings == _settings)
		return;
	_settings = settings;
	save();
	Q_EMIT changed();
}

void SyntaxColorScheme::load()
{
	QSettings qs;
	qs.beginGroup(SETTINGS_GROUP);
	_settings.rainbowParens = qs.value(RAINBOW_KEY, false).toBool();
	const QString schemeName = qs.value(SCHEME_KEY, schemeKey(SyntaxScheme::Standard)).toString();
	_settings.scheme = SyntaxScheme::Standard;
	for (int s = 0; s < SyntaxSchemeCount; ++s) {
		if (schemeName == QLatin1String(schemeKey(static_cast<SyntaxScheme>(s))))
			_settings.scheme = static_cast<SyntaxScheme>(s);
	}
	qs.beginGroup(SYNTAX_GROUP);
	for (int s = 0; s < SyntaxSchemeCount; ++s) {
		const QString prefix = schemeGroupPrefix(static_cast<SyntaxScheme>(s));
		for (int theme = 0; theme < 2; ++theme) {
			qs.beginGroup(prefix + (theme == 0 ? QStringLiteral("light") : QStringLiteral("dark")));
			for (int r = 0; r < SyntaxRoleCount; ++r) {
				const QString value = qs.value(roleKey(static_cast<SyntaxRole>(r))).toString();
				_settings.overrides[s][theme][r] = value.isEmpty() ? std::nullopt : decodeStyle(value);
			}
			auto readColor = [&qs](const char* key) -> std::optional<QColor> {
				const QColor color(qs.value(key).toString());
				return color.isValid() ? std::optional<QColor>(color) : std::nullopt;
			};
			_settings.editorOverrides[s][theme].background = readColor(BACKGROUND_KEY);
			_settings.editorOverrides[s][theme].text = readColor(TEXT_KEY);
			qs.endGroup();
		}
	}
	qs.endGroup();
	qs.endGroup();
}

void SyntaxColorScheme::save() const
{
	QSettings qs;
	qs.beginGroup(SETTINGS_GROUP);
	qs.setValue(RAINBOW_KEY, _settings.rainbowParens);
	qs.setValue(SCHEME_KEY, schemeKey(_settings.scheme));
	qs.beginGroup(SYNTAX_GROUP);
	for (int s = 0; s < SyntaxSchemeCount; ++s) {
		const QString prefix = schemeGroupPrefix(static_cast<SyntaxScheme>(s));
		for (int theme = 0; theme < 2; ++theme) {
			qs.beginGroup(prefix + (theme == 0 ? QStringLiteral("light") : QStringLiteral("dark")));
			for (int r = 0; r < SyntaxRoleCount; ++r) {
				const auto& over = _settings.overrides[s][theme][r];
				if (over)
					qs.setValue(roleKey(static_cast<SyntaxRole>(r)), encodeStyle(*over));
				else
					qs.remove(roleKey(static_cast<SyntaxRole>(r)));
			}
			auto writeColor = [&qs](const char* key, const std::optional<QColor>& color) {
				if (color)
					qs.setValue(key, color->name());
				else
					qs.remove(key);
			};
			writeColor(BACKGROUND_KEY, _settings.editorOverrides[s][theme].background);
			writeColor(TEXT_KEY, _settings.editorOverrides[s][theme].text);
			qs.endGroup();
		}
	}
	qs.endGroup();
	qs.endGroup();
}

} // namespace fso::fred
