#include "ui/SyntaxColors.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>

namespace fso::fred {

namespace {

constexpr auto SETTINGS_GROUP = "Preferences";
constexpr auto SYNTAX_GROUP = "syntax_colors";
constexpr auto RAINBOW_KEY = "syntax_rainbow_parens";

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

SyntaxStyle SyntaxColorScheme::defaultStyle(SyntaxRole role, bool dark)
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

QColor SyntaxColorScheme::parenColor(int depth, bool dark)
{
	static const QColor darkColors[] = {QColor(0xFF, 0xD7, 0x00), QColor(0xDA, 0x70, 0xD6), QColor(0x17, 0x9F, 0xFF),
		QColor(0x4E, 0xC9, 0x7E), QColor(0xFF, 0x8C, 0x42), QColor(0x9C, 0xDC, 0xFE)};
	static const QColor lightColors[] = {QColor(0x04, 0x31, 0xFA), QColor(0x31, 0x93, 0x31), QColor(0x7B, 0x38, 0x14),
		QColor(0xA0, 0x1B, 0x9E), QColor(0x00, 0x7A, 0x7A), QColor(0xB3, 0x5A, 0x00)};
	constexpr int count = 6;
	const int i = ((depth % count) + count) % count;
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
	const auto& over = _settings.overrides[dark ? 1 : 0][r];
	return over ? *over : defaultStyle(role, dark);
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
	qs.beginGroup(SYNTAX_GROUP);
	for (int theme = 0; theme < 2; ++theme) {
		qs.beginGroup(theme == 0 ? QStringLiteral("light") : QStringLiteral("dark"));
		for (int r = 0; r < SyntaxRoleCount; ++r) {
			const QString value = qs.value(roleKey(static_cast<SyntaxRole>(r))).toString();
			_settings.overrides[theme][r] = value.isEmpty() ? std::nullopt : decodeStyle(value);
		}
		qs.endGroup();
	}
	qs.endGroup();
	qs.endGroup();
}

void SyntaxColorScheme::save() const
{
	QSettings qs;
	qs.beginGroup(SETTINGS_GROUP);
	qs.setValue(RAINBOW_KEY, _settings.rainbowParens);
	qs.beginGroup(SYNTAX_GROUP);
	for (int theme = 0; theme < 2; ++theme) {
		qs.beginGroup(theme == 0 ? QStringLiteral("light") : QStringLiteral("dark"));
		for (int r = 0; r < SyntaxRoleCount; ++r) {
			const auto& over = _settings.overrides[theme][r];
			if (over)
				qs.setValue(roleKey(static_cast<SyntaxRole>(r)), encodeStyle(*over));
			else
				qs.remove(roleKey(static_cast<SyntaxRole>(r)));
		}
		qs.endGroup();
	}
	qs.endGroup();
	qs.endGroup();
}

} // namespace fso::fred
