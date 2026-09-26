#include "ui/widgets/MissionTextHighlighter.h"

#include "parse/sexp.h"

#include <QEvent>
#include <QPlainTextEdit>
#include <QRegularExpression>

#include <algorithm>

namespace fso::fred {

namespace {

enum class Tok { Header, Key, Operator, UnknownOperator, String, Number, Variable, Version, Comment, OpenParen, CloseParen };

// Lexer state carried from one line to the next (a QSyntaxHighlighter block
// state): which block comment is open, whether a string is open (the parser lets
// quotes span lines), and the parenthesis depth for the rainbow colors.
struct LineState {
	int comment = 0; // 0 none, 1 /* */, 2 !* *!
	bool inQuote = false;
	int depth = 0;

	static LineState unpack(int packed)
	{
		LineState s;
		if (packed < 0)
			return s;
		s.comment = packed & 3;
		s.inQuote = (packed & 4) != 0;
		s.depth = packed >> 3;
		return s;
	}
	int pack() const { return comment | (inQuote ? 4 : 0) | (std::min(depth, 1 << 20) << 3); }
};

bool isDelimiter(QChar c)
{
	return c.isSpace() || c == QLatin1Char('(') || c == QLatin1Char(')') || c == QLatin1Char('"') ||
		c == QLatin1Char(',') || c == QLatin1Char(';');
}

int tokenEnd(const QString& text, int from)
{
	int i = from;
	while (i < text.size() && !isDelimiter(text[i]))
		++i;
	return i;
}

bool isNumber(const QString& token)
{
	static const QRegularExpression re(QStringLiteral("^[-+]?(\\d+\\.?\\d*|\\.\\d+)$"));
	return re.match(token).hasMatch();
}

// Lex one line, reporting each token through sink(start, length, tok, depth).
// Mirrors strip_comments(): comments aren't recognized inside quotes, and a
// ;;FSO x.y.z;; tag is a comment-like marker rather than the start of a ; comment.
template <typename Sink>
void lexLine(const QString& text, LineState& st, const QSet<QString>* operators, Sink&& sink)
{
	static const QRegularExpression versionTag(QStringLiteral(";;!?FSO [^;]*;;"));
	const int n = text.size();
	int i = 0;

	// Line-start constructs: a #Section header or a $Field:/+Field: key.
	if (st.comment == 0 && !st.inQuote) {
		int j = 0;
		while (j < n && text[j].isSpace())
			++j;
		if (j < n && text[j] == QLatin1Char('#')) {
			const int end = tokenEnd(text, j);
			sink(j, end - j, Tok::Header, 0);
			i = end;
		} else if (j < n && (text[j] == QLatin1Char('$') || text[j] == QLatin1Char('+'))) {
			const int colon = text.indexOf(QLatin1Char(':'), j);
			const int semi = text.indexOf(QLatin1Char(';'), j);
			if (colon > j && (semi < 0 || colon < semi)) {
				sink(j, colon - j + 1, Tok::Key, 0);
				i = colon + 1;
			}
		}
	}

	while (i < n) {
		if (st.comment != 0) {
			const QString close = (st.comment == 1) ? QStringLiteral("*/") : QStringLiteral("*!");
			const int pos = text.indexOf(close, i);
			const int end = (pos < 0) ? n : pos + 2;
			if (pos >= 0)
				st.comment = 0;
			sink(i, end - i, Tok::Comment, 0);
			i = end;
			continue;
		}
		if (st.inQuote) {
			const int pos = text.indexOf(QLatin1Char('"'), i);
			const int end = (pos < 0) ? n : pos + 1;
			if (pos >= 0)
				st.inQuote = false;
			sink(i, end - i, Tok::String, 0);
			i = end;
			continue;
		}

		const QChar c = text[i];
		if (c == QLatin1Char('"')) {
			const int pos = text.indexOf(QLatin1Char('"'), i + 1);
			const int end = (pos < 0) ? n : pos + 1;
			st.inQuote = (pos < 0);
			sink(i, end - i, Tok::String, 0);
			i = end;
			continue;
		}
		if (c == QLatin1Char('/') && i + 1 < n && text[i + 1] == QLatin1Char('*')) {
			st.comment = 1;
			sink(i, 2, Tok::Comment, 0);
			i += 2;
			continue;
		}
		if (c == QLatin1Char('!') && i + 1 < n && text[i + 1] == QLatin1Char('*')) {
			st.comment = 2;
			sink(i, 2, Tok::Comment, 0);
			i += 2;
			continue;
		}
		if (c == QLatin1Char(';')) {
			const auto m = versionTag.match(text, i, QRegularExpression::NormalMatch,
				QRegularExpression::AnchorAtOffsetMatchOption);
			if (m.hasMatch()) {
				sink(i, m.capturedLength(), Tok::Version, 0);
				i += m.capturedLength();
				continue;
			}
			sink(i, n - i, Tok::Comment, 0);
			break;
		}
		if (c == QLatin1Char('(')) {
			sink(i, 1, Tok::OpenParen, st.depth);
			++st.depth;
			++i;
			// The first token inside a paren is the operator, unless it's data
			// (a string, number or variable, e.g. a flag list or a number list).
			int k = i;
			while (k < n && text[k].isSpace() && text[k] != QLatin1Char('\n'))
				++k;
			const int end = tokenEnd(text, k);
			if (end > k && operators != nullptr) {
				const QString token = text.mid(k, end - k);
				const QChar first = token[0];
				if (first != QLatin1Char('@') && first != QLatin1Char('&') && !isNumber(token)) {
					sink(k, end - k, operators->contains(token) ? Tok::Operator : Tok::UnknownOperator, 0);
					i = end;
				}
			}
			continue;
		}
		if (c == QLatin1Char(')')) {
			st.depth = std::max(0, st.depth - 1);
			sink(i, 1, Tok::CloseParen, st.depth);
			++i;
			continue;
		}
		if (c.isSpace() || c == QLatin1Char(',')) {
			++i;
			continue;
		}
		if (c == QLatin1Char('&')) {
			// &container& (the closing & ends it); a lone & is just text.
			const int close = text.indexOf(QLatin1Char('&'), i + 1);
			const int end = (close > i) ? close + 1 : tokenEnd(text, i);
			sink(i, end - i, Tok::Variable, 0);
			i = end;
			continue;
		}
		const int end = tokenEnd(text, i);
		const QString token = text.mid(i, end - i);
		if (c == QLatin1Char('@'))
			sink(i, end - i, Tok::Variable, 0);
		else if (isNumber(token))
			sink(i, end - i, Tok::Number, 0);
		i = std::max(end, i + 1);
	}
}

QTextCharFormat makeFormat(const SyntaxStyle& style)
{
	QTextCharFormat f;
	f.setForeground(style.color);
	f.setFontWeight(style.bold ? QFont::Bold : QFont::Normal);
	f.setFontItalic(style.italic);
	return f;
}

} // namespace

MissionTextHighlighter::MissionTextHighlighter(QPlainTextEdit* editor) : QSyntaxHighlighter(editor->document())
{
	for (const auto& op : Operators)
		m_operators.insert(QString::fromStdString(op.text));

	// The editor gets PaletteChange when the theme changes; watching it (not qApp)
	// means one restyle per change.
	editor->installEventFilter(this);
	connect(&SyntaxColorScheme::instance(), &SyntaxColorScheme::changed, this, &MissionTextHighlighter::restyle);
	restyle();
}

void MissionTextHighlighter::restyle()
{
	const auto& scheme = SyntaxColorScheme::instance();
	const bool dark = SyntaxColorScheme::paletteIsDark();
	for (int r = 0; r < SyntaxRoleCount; ++r)
		m_formats[r] = makeFormat(scheme.style(static_cast<SyntaxRole>(r), dark));

	// An unknown operator is also underlined, so it reads as an error in any color.
	auto& unknown = m_formats[static_cast<int>(SyntaxRole::UnknownOperator)];
	unknown.setUnderlineStyle(QTextCharFormat::WaveUnderline);
	unknown.setUnderlineColor(unknown.foreground().color());

	m_rainbow = scheme.rainbowParens();
	m_parenFormats.clear();
	for (int d = 0; d < 6; ++d) {
		QTextCharFormat f;
		f.setForeground(SyntaxColorScheme::parenColor(d, dark));
		m_parenFormats.push_back(f);
	}
	rehighlight();
}

bool MissionTextHighlighter::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::PaletteChange)
		restyle();
	return QSyntaxHighlighter::eventFilter(watched, event);
}

void MissionTextHighlighter::highlightBlock(const QString& text)
{
	LineState st = LineState::unpack(previousBlockState());
	lexLine(text, st, &m_operators, [this](int start, int length, Tok tok, int depth) {
		switch (tok) {
		case Tok::Header:          setFormat(start, length, fmt(SyntaxRole::SectionHeader)); break;
		case Tok::Key:             setFormat(start, length, fmt(SyntaxRole::FieldKey)); break;
		case Tok::Operator:        setFormat(start, length, fmt(SyntaxRole::Operator)); break;
		case Tok::UnknownOperator: setFormat(start, length, fmt(SyntaxRole::UnknownOperator)); break;
		case Tok::String:          setFormat(start, length, fmt(SyntaxRole::String)); break;
		case Tok::Number:          setFormat(start, length, fmt(SyntaxRole::Number)); break;
		case Tok::Variable:        setFormat(start, length, fmt(SyntaxRole::Variable)); break;
		case Tok::Version:         setFormat(start, length, fmt(SyntaxRole::VersionComment)); break;
		case Tok::Comment:         setFormat(start, length, fmt(SyntaxRole::Comment)); break;
		case Tok::OpenParen:
		case Tok::CloseParen:
			if (m_rainbow)
				setFormat(start, length, m_parenFormats[depth % m_parenFormats.size()]);
			break;
		}
	});
	setCurrentBlockState(st.pack());
}

QVector<bool> MissionTextHighlighter::codeMask(const QString& text)
{
	QVector<bool> mask(text.size(), true);
	LineState st;
	int lineStart = 0;
	while (lineStart <= text.size()) {
		int lineEnd = text.indexOf(QLatin1Char('\n'), lineStart);
		if (lineEnd < 0)
			lineEnd = text.size();
		const QString line = text.mid(lineStart, lineEnd - lineStart);
		lexLine(line, st, nullptr, [&mask, lineStart](int start, int length, Tok tok, int) {
			if (tok == Tok::String || tok == Tok::Comment || tok == Tok::Version)
				std::fill(mask.begin() + lineStart + start, mask.begin() + lineStart + start + length, false);
		});
		lineStart = lineEnd + 1;
	}
	return mask;
}

} // namespace fso::fred
