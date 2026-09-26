#pragma once

#include "ui/SyntaxColors.h"

#include <QSet>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

#include <array>

class QPlainTextEdit;

namespace fso::fred {

// Syntax highlighting for mission-file text (the Events editor's Advanced view).
// Mirrors what the parser accepts: section headers, $/+ field keys, SEXP
// operators (checked against the engine's operator list), strings that may span
// lines, numbers, @variables and &containers&, ;;FSO version tags, and the three
// comment forms (; line, /* */ and !* *! blocks). Optional rainbow parentheses.
//
// Colors come from SyntaxColorScheme and follow the light/dark palette; the
// highlighter restyles itself when either changes.
class MissionTextHighlighter final : public QSyntaxHighlighter {
	Q_OBJECT

  public:
	explicit MissionTextHighlighter(QPlainTextEdit* editor);

	// Per-character mask of positions that are real code (outside strings and
	// comments), for bracket matching. Uses the same lexing rules as the
	// highlighter so the two always agree.
	static QVector<bool> codeMask(const QString& text);

  protected:
	void highlightBlock(const QString& text) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

  private:
	void restyle();
	const QTextCharFormat& fmt(SyntaxRole role) const { return m_formats[static_cast<int>(role)]; }

	std::array<QTextCharFormat, SyntaxRoleCount> m_formats;
	QVector<QTextCharFormat> m_parenFormats;
	bool m_rainbow = false;
	QSet<QString> m_operators; // lower case
};

} // namespace fso::fred
