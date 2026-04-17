#include "panel-dock.hpp"

#include <QApplication>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QSettings>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>
#define _USE_MATH_DEFINES
#include <cmath>

#include "i18n.hpp"
using I18n::t;

static const int ROLE_VERSE_NUMBER = Qt::UserRole;
static const int ROLE_VERSE_TEXT   = Qt::UserRole + 1;
static const int ROLE_IN_SESSION   = Qt::UserRole + 2;

// ── Iconos dibujados con QPainter (no dependen de fuente) ──────────────────

static QIcon makePlayIcon(int sz, const QColor &col)
{
	QPixmap pm(sz, sz);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(col);
	qreal m = sz * 0.18;
	QPolygonF tri;
	tri << QPointF(m, m)
	    << QPointF(m, sz - m)
	    << QPointF(sz - m * 0.7, sz / 2.0);
	p.drawPolygon(tri);
	return QIcon(pm);
}

static QIcon makeStarIcon(int sz, const QColor &col, bool filled)
{
	QPixmap pm(sz, sz);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	const qreal cx = sz / 2.0, cy = sz / 2.0;
	const qreal outer = sz * 0.44, inner = sz * 0.19;
	QPainterPath path;
	for (int i = 0; i < 5; ++i) {
		qreal a1 = -M_PI / 2.0 + i * 2.0 * M_PI / 5.0;
		qreal a2 = a1 + M_PI / 5.0;
		QPointF op(cx + outer * std::cos(a1), cy + outer * std::sin(a1));
		QPointF ip(cx + inner * std::cos(a2), cy + inner * std::sin(a2));
		if (i == 0) path.moveTo(op); else path.lineTo(op);
		path.lineTo(ip);
	}
	path.closeSubpath();
	p.setPen(QPen(col, sz * 0.07));
	p.setBrush(filled ? col : Qt::NoBrush);
	p.drawPath(path);
	return QIcon(pm);
}

static QIcon makeCloseIcon(int sz, const QColor &col)
{
	QPixmap pm(sz, sz);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(col, sz * 0.16, Qt::SolidLine, Qt::RoundCap));
	qreal m = sz * 0.22;
	p.drawLine(QPointF(m, m),      QPointF(sz - m, sz - m));
	p.drawLine(QPointF(sz - m, m), QPointF(m,      sz - m));
	return QIcon(pm);
}

// ─────────────────────────────────────────────────────────────────────────────
// Verse row widget: [num] [text...]
// ─────────────────────────────────────────────────────────────────────────────
static QWidget *makeVerseWidget(int verseNum, const QString &text, bool inSession)
{
	QWidget *w = new QWidget();
	w->setAttribute(Qt::WA_TransparentForMouseEvents);
	QHBoxLayout *lay = new QHBoxLayout(w);
	lay->setContentsMargins(6, 3, 6, 3);
	lay->setSpacing(6);

	QLabel *numLbl = new QLabel(QString::number(verseNum));
	numLbl->setFixedWidth(20);
	numLbl->setAlignment(Qt::AlignRight | Qt::AlignTop);
	numLbl->setStyleSheet(inSession
		? "color:#3fa35a;font-size:10px;font-weight:bold;"
		: "font-size:10px;font-weight:bold;opacity:0.5;");

	QLabel *textLbl = new QLabel(text);
	textLbl->setWordWrap(false);
	textLbl->setMaximumHeight(36);
	textLbl->setStyleSheet("font-size:12px;");

	lay->addWidget(numLbl, 0, Qt::AlignTop);
	lay->addWidget(textLbl, 1, Qt::AlignTop);
	return w;
}

// Session item widget: [ref label] [▶] [✕]
static QWidget *makeSessionWidget(const QString &ref,
				  std::function<void()> onPlay,
				  std::function<void()> onRemove)
{
	QWidget *w = new QWidget();
	QHBoxLayout *lay = new QHBoxLayout(w);
	lay->setContentsMargins(8, 4, 4, 4);
	lay->setSpacing(4);

	QLabel *refLbl = new QLabel(ref);
	refLbl->setStyleSheet("font-size:11px;font-weight:bold;");

	QColor hlCol = QApplication::palette().color(QPalette::Highlight);
	QColor hlHov = hlCol.lighter(115);
	QColor hlTxt = QApplication::palette().color(QPalette::HighlightedText);

	QPushButton *playBtn = new QPushButton();
	playBtn->setIcon(makePlayIcon(12, hlTxt));
	playBtn->setIconSize(QSize(12, 12));
	playBtn->setFixedSize(26, 22);
	playBtn->setStyleSheet(QString(
		"QPushButton{background:%1;border-radius:3px;border:none;}"
		"QPushButton:hover{background:%2;}")
		.arg(hlCol.name(), hlHov.name()));

	QColor remCol = QApplication::palette().color(QPalette::ButtonText);
	QPushButton *removeBtn = new QPushButton();
	removeBtn->setIcon(makeCloseIcon(11, remCol));
	removeBtn->setIconSize(QSize(11, 11));
	removeBtn->setFixedSize(22, 22);
	removeBtn->setStyleSheet(
		"QPushButton{background:transparent;border:none;}"
		"QPushButton:hover{background:rgba(200,50,50,60);border-radius:3px;}");

	QObject::connect(playBtn,   &QPushButton::clicked, onPlay);
	QObject::connect(removeBtn, &QPushButton::clicked, onRemove);

	lay->addWidget(refLbl, 1);
	lay->addWidget(playBtn);
	lay->addWidget(removeBtn);
	return w;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
PanelDock::PanelDock(BibleData *bible, QWidget *parent)
	: QWidget(parent), m_bible(bible)
{
	setMinimumWidth(240);
	buildUi();
	applyStylesheet();
	loadSession();
	renderSession();
	populateBooks();
}

// ─────────────────────────────────────────────────────────────────────────────
// UI construction — lambdas replace Q_OBJECT slots
// ─────────────────────────────────────────────────────────────────────────────
void PanelDock::buildUi()
{
	QWidget *root = new QWidget(this);
	QVBoxLayout *vbox = new QVBoxLayout(root);
	vbox->setContentsMargins(8, 8, 8, 8);
	vbox->setSpacing(6);

	// ── Book ──
	auto *bookLbl = new QLabel(t("Libro", "Book"));
	bookLbl->setStyleSheet("font-size:11px;");
	m_bookCombo = new QComboBox();
	m_bookCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	QObject::connect(m_bookCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		[this](int idx) {
			int bookId = m_bookCombo->itemData(idx).toInt();
			populateChapters(bookId);
		});

	// ── Chapter ──
	auto *chapLbl = new QLabel(t("Capítulo", "Chapter"));
	chapLbl->setStyleSheet("font-size:11px;");
	m_chapterCombo = new QComboBox();
	m_chapterCombo->setEnabled(false);
	m_chapterCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	QObject::connect(m_chapterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		[this](int idx) {
			int chapNum = m_chapterCombo->itemData(idx).toInt();
			m_verseList->clear();
			m_preview->clear();
			m_selected.reset();
			updateActionButtons();
			if (chapNum > 0) populateVerses();
		});

	// ── Verse list ──
	auto *verseLbl = new QLabel(t("Versículos", "Verses"));
	verseLbl->setStyleSheet("font-size:11px;");
	m_verseList = new QListWidget();
	m_verseList->setFixedHeight(200);
	m_verseList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	QObject::connect(m_verseList, &QListWidget::itemClicked,
		[this](QListWidgetItem *item) {
			if (!item) return;
			int bookId   = m_bookCombo->currentData().toInt();
			int chapNum  = m_chapterCombo->currentData().toInt();
			int verseNum = item->data(ROLE_VERSE_NUMBER).toInt();
			QString text = item->data(ROLE_VERSE_TEXT).toString();
			QString bookName = m_bookCombo->currentText();
			m_selected = SelectedVerse{bookId, bookName, chapNum, verseNum, text};
			m_preview->setPlainText(text + "\n\n— " + bookName + " " +
				QString::number(chapNum) + ":" + QString::number(verseNum));
			updateActionButtons();
			updateAddSessionButton();
		});

	// ── Navigation ──
	auto *navRow = new QWidget();
	auto *navLay = new QHBoxLayout(navRow);
	navLay->setContentsMargins(0, 0, 0, 0);
	navLay->setSpacing(4);
	// font-family va en el stylesheet del botón, no en setFont(), porque
	// el stylesheet del panel padre anularía setFont() en los hijos
	static const char *SYM_FONT = "font-family:'Segoe UI Symbol';";

	m_prevBtn = new QPushButton(t("\u25C0  Anterior", "\u25C0  Previous"));
	m_nextBtn = new QPushButton(t("Siguiente  \u25B6", "Next  \u25B6"));
	m_prevBtn->setStyleSheet(SYM_FONT);
	m_nextBtn->setStyleSheet(SYM_FONT);
	m_prevBtn->setEnabled(false);
	m_nextBtn->setEnabled(false);
	navLay->addWidget(m_prevBtn);
	navLay->addWidget(m_nextBtn);
	QObject::connect(m_prevBtn, &QPushButton::clicked, [this]{ navigateBy(-1); });
	QObject::connect(m_nextBtn, &QPushButton::clicked, [this]{ navigateBy(+1); });

	// ── Preview ──
	auto *previewLbl = new QLabel(t("Vista previa", "Preview"));
	previewLbl->setStyleSheet("font-size:11px;");
	m_preview = new QPlainTextEdit();
	m_preview->setReadOnly(true);
	m_preview->setMinimumHeight(120);
	m_preview->setMaximumHeight(180);
	m_preview->setPlaceholderText(t("Selecciona un versículo...", "Select a verse..."));
	m_preview->setTextInteractionFlags(Qt::NoTextInteraction);
	m_preview->setFocusPolicy(Qt::NoFocus);
	m_preview->setCursor(Qt::ArrowCursor);
	m_preview->viewport()->setCursor(Qt::ArrowCursor);
	m_preview->setStyleSheet(
		"QPlainTextEdit { border: 1px solid transparent; }"
		"QPlainTextEdit:hover { border: 1px solid transparent; }");

	// ── Action buttons ──
	auto *btnRow = new QWidget();
	auto *btnLay = new QHBoxLayout(btnRow);
	btnLay->setContentsMargins(0, 0, 0, 0);
	btnLay->setSpacing(4);
	// \uFE0E = Variation Selector-15: fuerza presentación de texto en vez de emoji
	m_showBtn = new QPushButton(t("Mostrar  \u25B6\uFE0E", "Show  \u25B6\uFE0E"));
	m_hideBtn = new QPushButton(t("Ocultar  \u25A0\uFE0E", "Hide  \u25A0\uFE0E"));
	m_addSessionBtn = new QPushButton();
	m_addSessionBtn->setToolTip(t("Agregar a sesión", "Add to session"));
	m_clearBtn = new QPushButton();
	m_clearBtn->setToolTip(t("Limpiar selección", "Clear selection"));
	{
		QColor tc = QApplication::palette().color(QPalette::ButtonText);
		m_addSessionBtn->setIcon(makeStarIcon(16, tc, false));
		m_addSessionBtn->setIconSize(QSize(16, 16));
		m_clearBtn->setIcon(makeCloseIcon(14, tc));
		m_clearBtn->setIconSize(QSize(14, 14));
	}
	m_hideBtn->setStyleSheet(SYM_FONT);
	m_showBtn->setEnabled(false);
	m_addSessionBtn->setEnabled(false);
	m_showBtn->setObjectName("btnShow");
	m_hideBtn->setObjectName("btnHide");
	m_addSessionBtn->setFixedWidth(34);
	m_clearBtn->setFixedWidth(32);
	btnLay->addWidget(m_showBtn, 1);
	btnLay->addWidget(m_hideBtn);
	btnLay->addWidget(m_addSessionBtn);
	btnLay->addWidget(m_clearBtn);

	QObject::connect(m_showBtn, &QPushButton::clicked, [this] {
		if (!m_selected || !onShowVerse) return;
		QString ref = m_selected->bookName + " " +
			QString::number(m_selected->chapter) + ":" +
			QString::number(m_selected->verseNumber);
		onShowVerse(m_selected->bookName, m_selected->chapter,
			    m_selected->verseNumber, m_selected->text, ref);
	});
	QObject::connect(m_hideBtn, &QPushButton::clicked, [this] {
		if (onHideVerse) onHideVerse();
	});
	QObject::connect(m_addSessionBtn, &QPushButton::clicked, [this] {
		if (!m_selected) return;
		if (isInSession(m_selected->bookId, m_selected->chapter, m_selected->verseNumber)) return;
		m_session.push_back({m_selected->bookId, m_selected->bookName,
				     m_selected->chapter, m_selected->verseNumber});
		saveSession();
		renderSession();
		updateAddSessionButton();
		if (auto *cur = m_verseList->currentItem()) {
			cur->setData(ROLE_IN_SESSION, true);
			cur->setBackground(QColor(26, 48, 40));
		}
	});
	QObject::connect(m_clearBtn, &QPushButton::clicked, [this] {
		m_bookCombo->setCurrentIndex(0);
		m_verseList->clear();
		m_preview->clear();
		m_selected.reset();
		updateActionButtons();
		if (onHideVerse) onHideVerse();
	});

	// ── Session header ──
	auto *sessionHeader = new QWidget();
	auto *sessionHeadLay = new QHBoxLayout(sessionHeader);
	sessionHeadLay->setContentsMargins(0, 0, 0, 0);
	m_sessionCountLabel = new QLabel(t("Sesión", "Session"));
	m_sessionCountLabel->setStyleSheet("font-size:11px;");
	m_clearSessionBtn = new QPushButton(t("Limpiar todo", "Clear all"));
	m_clearSessionBtn->setFlat(true);
	sessionHeadLay->addWidget(m_sessionCountLabel, 1);
	sessionHeadLay->addWidget(m_clearSessionBtn);
	QObject::connect(m_clearSessionBtn, &QPushButton::clicked, [this] {
		m_session.clear();
		saveSession();
		renderSession();
		updateAddSessionButton();
		for (int i = 0; i < m_verseList->count(); ++i) {
			auto *item = m_verseList->item(i);
			item->setData(ROLE_IN_SESSION, false);
			item->setBackground(QBrush());
		}
	});

	// ── Session list ──
	m_sessionList = new QListWidget();
	m_sessionList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_sessionList->setMaximumHeight(160);
	m_sessionList->setFocusPolicy(Qt::NoFocus);
	m_sessionList->setSelectionMode(QAbstractItemView::NoSelection);

	// ── Assemble ──
	vbox->addWidget(bookLbl);
	vbox->addWidget(m_bookCombo);
	vbox->addWidget(chapLbl);
	vbox->addWidget(m_chapterCombo);
	vbox->addWidget(verseLbl);
	vbox->addWidget(m_verseList);
	vbox->addWidget(navRow);
	vbox->addWidget(previewLbl);
	vbox->addWidget(m_preview);
	vbox->addWidget(btnRow);
	vbox->addWidget(sessionHeader);
	vbox->addWidget(m_sessionList);
	vbox->addStretch();

	// Attribution
	auto *attrib = new QLabel(t("Desarrollado por Dani Acosta", "Developed by Dani Acosta"));
	attrib->setAlignment(Qt::AlignCenter);
	attrib->setStyleSheet("font-size:10px; padding:2px 0 0 0;");
	attrib->setEnabled(false);
	vbox->addWidget(attrib);

	// Donate button
	auto *kofiBtn = new QPushButton(t("\u2615  Apoyar el desarrollo", "\u2615  Support development"));
	kofiBtn->setStyleSheet(
		"QPushButton {"
		"  background-color: #FF5E5B;"
		"  color: white;"
		"  font-size: 11px;"
		"  font-weight: bold;"
		"  border-radius: 4px;"
		"  border: none;"
		"  padding: 5px 0;"
		"  margin: 4px 0 2px 0;"
		"}"
		"QPushButton:hover { background-color: #ff7572; }"
		"QPushButton:pressed { background-color: #e04a47; }");
	kofiBtn->setToolTip(t("Apoyar en Ko-fi", "Support on Ko-fi"));
	QObject::connect(kofiBtn, &QPushButton::clicked, [] {
		QDesktopServices::openUrl(QUrl("https://ko-fi.com/daniacostadev"));
	});
	vbox->addWidget(kofiBtn);

	auto *scroll = new QScrollArea(this);
	scroll->setWidget(root);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	// Heredar background del tema de OBS en lugar de usar blanco
	scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");
	scroll->viewport()->setAutoFillBackground(false);

	auto *outerLayout = new QVBoxLayout(this);
	outerLayout->setContentsMargins(0, 0, 0, 0);
	outerLayout->addWidget(scroll);
	setLayout(outerLayout);
}

void PanelDock::applyStylesheet()
{
	QColor hl    = QApplication::palette().color(QPalette::Highlight);
	QColor hlHov = hl.lighter(115);
	QColor hlTxt = QApplication::palette().color(QPalette::HighlightedText);
	QColor dimBg  = QApplication::palette().color(QPalette::Button).lighter(110);
	QColor dimTxt = QApplication::palette().color(QPalette::ButtonText);
	// Hover de btnHide en el panel; btnShow tiene stylesheet propio (ver abajo)
	setStyleSheet(QString(
		"QPushButton#btnHide:hover:enabled {"
		"  background-color: %1; color: %2; border-radius: 3px;"
		"}"
	).arg(dimBg.name(), dimTxt.name()));

	// Stylesheet directo en el botón: garantiza que :hover funcione en Windows
	m_showBtn->setStyleSheet(QString(
		"QPushButton {"
		"  background-color: %1; color: %2;"
		"  font-weight: bold; border-radius: 3px;"
		"  font-family: 'Segoe UI Symbol';"
		"}"
		"QPushButton:hover:enabled { background-color: %3; }"
		"QPushButton:disabled { background-color: %1; opacity: 0.4; }"
	).arg(hl.name(), hlTxt.name(), hlHov.name()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Data population
// ─────────────────────────────────────────────────────────────────────────────
void PanelDock::populateBooks()
{
	m_bookCombo->blockSignals(true);
	m_bookCombo->clear();
	m_bookCombo->addItem(t("Selecciona un libro...", "Select a book..."), -1);
	for (const BibleBook &b : m_bible->books())
		m_bookCombo->addItem(QString::fromStdString(b.name), b.id);
	m_bookCombo->blockSignals(false);
}

void PanelDock::populateChapters(int bookId)
{
	m_chapterCombo->blockSignals(true);
	m_chapterCombo->clear();
	m_verseList->clear();
	m_preview->clear();
	m_selected.reset();
	updateActionButtons();

	if (bookId < 0) {
		m_chapterCombo->setEnabled(false);
		m_chapterCombo->blockSignals(false);
		return;
	}
	const BibleBook *book = m_bible->book(bookId);
	if (!book) {
		m_chapterCombo->setEnabled(false);
		m_chapterCombo->blockSignals(false);
		return;
	}
	m_chapterCombo->addItem(t("Capítulo...", "Chapter..."), 0);
	for (const BibleChapter &c : book->chapters)
		m_chapterCombo->addItem(QString::number(c.number), c.number);
	m_chapterCombo->setEnabled(true);
	m_chapterCombo->blockSignals(false);
}

void PanelDock::populateVerses()
{
	m_verseList->clear();
	int bookId  = m_bookCombo->currentData().toInt();
	int chapNum = m_chapterCombo->currentData().toInt();

	const BibleChapter *chap = m_bible->chapter(bookId, chapNum);
	if (!chap) return;

	for (const BibleVerse &v : chap->verses) {
		bool inSession = isInSession(bookId, chapNum, v.number);
		auto *item = new QListWidgetItem(m_verseList);
		item->setData(ROLE_VERSE_NUMBER, v.number);
		item->setData(ROLE_VERSE_TEXT,   QString::fromStdString(v.text));
		item->setData(ROLE_IN_SESSION,   inSession);
		if (inSession) item->setBackground(QColor(26, 48, 40));
		item->setSizeHint(QSize(0, 42));
		m_verseList->setItemWidget(item, makeVerseWidget(
			v.number, QString::fromStdString(v.text), inSession));
	}
}

void PanelDock::updateActionButtons()
{
	bool has = m_selected.has_value();
	m_showBtn->setEnabled(has);
	m_prevBtn->setEnabled(has);
	m_nextBtn->setEnabled(has);
	m_addSessionBtn->setEnabled(has);
}

void PanelDock::updateAddSessionButton()
{
	if (!m_selected) return;
	bool already = isInSession(m_selected->bookId, m_selected->chapter,
				   m_selected->verseNumber);
	QColor tc = QApplication::palette().color(QPalette::ButtonText);
	m_addSessionBtn->setIcon(makeStarIcon(16, already ? QColor("#e8b84b") : tc, already));
	m_addSessionBtn->setEnabled(!already);
}

void PanelDock::navigateTo(int bookId, int chapterNum, int verseNum)
{
	// Set book
	int bookIdx = m_bookCombo->findData(bookId);
	if (bookIdx < 0) return;
	m_bookCombo->blockSignals(true);
	m_bookCombo->setCurrentIndex(bookIdx);
	m_bookCombo->blockSignals(false);

	// Populate and set chapter
	populateChapters(bookId);
	int chapIdx = m_chapterCombo->findData(chapterNum);
	if (chapIdx >= 0) {
		m_chapterCombo->blockSignals(true);
		m_chapterCombo->setCurrentIndex(chapIdx);
		m_chapterCombo->blockSignals(false);
	}

	// Populate verses and select
	populateVerses();
	for (int i = 0; i < m_verseList->count(); ++i) {
		QListWidgetItem *item = m_verseList->item(i);
		if (item->data(ROLE_VERSE_NUMBER).toInt() == verseNum) {
			m_verseList->setCurrentItem(item);
			m_verseList->scrollToItem(item, QAbstractItemView::EnsureVisible);
			QString text = item->data(ROLE_VERSE_TEXT).toString();
			m_selected = SelectedVerse{bookId, m_bookCombo->currentText(),
						   chapterNum, verseNum, text};
			m_preview->setPlainText(text + "\n\n— " + m_selected->bookName +
				" " + QString::number(chapterNum) + ":" +
				QString::number(verseNum));
			updateActionButtons();
			updateAddSessionButton();
			break;
		}
	}
}

void PanelDock::navigateBy(int delta)
{
	if (!m_selected || m_verseList->count() == 0) return;
	int next = m_verseList->currentRow() + delta;
	if (next < 0 || next >= m_verseList->count()) return;
	m_verseList->setCurrentRow(next);
	auto *item = m_verseList->item(next);
	if (!item) return;
	int verseNum = item->data(ROLE_VERSE_NUMBER).toInt();
	QString text = item->data(ROLE_VERSE_TEXT).toString();
	m_selected = SelectedVerse{
		m_bookCombo->currentData().toInt(),
		m_bookCombo->currentText(),
		m_chapterCombo->currentData().toInt(),
		verseNum, text};
	m_preview->setPlainText(text + "\n\n— " + m_selected->bookName + " " +
		QString::number(m_selected->chapter) + ":" +
		QString::number(verseNum));
	updateActionButtons();
	updateAddSessionButton();
	m_verseList->scrollToItem(item, QAbstractItemView::EnsureVisible);
}

// ─────────────────────────────────────────────────────────────────────────────
// Session
// ─────────────────────────────────────────────────────────────────────────────
bool PanelDock::isInSession(int bookId, int chapter, int verse) const
{
	for (const SessionEntry &e : m_session)
		if (e.bookId == bookId && e.chapter == chapter && e.verse == verse)
			return true;
	return false;
}

void PanelDock::renderSession()
{
	m_sessionList->clear();
	int count = static_cast<int>(m_session.size());
	m_sessionCountLabel->setText(count > 0
		? t("Sesión (%1)", "Session (%1)").arg(count) : t("Sesión", "Session"));

	if (m_session.empty()) {
		auto *ph = new QListWidgetItem(t("Sin versículos en sesión", "No verses in session"));
		ph->setForeground(QColor("#4a6070"));
		ph->setFlags(Qt::NoItemFlags);
		m_sessionList->addItem(ph);
		return;
	}

	for (int i = 0; i < count; ++i) {
		const SessionEntry &e = m_session[static_cast<size_t>(i)];
		QString ref = e.bookName + " " + QString::number(e.chapter) +
			      ":" + QString::number(e.verse);
		auto *item = new QListWidgetItem(m_sessionList);
		item->setSizeHint(QSize(0, 34));
		item->setFlags(Qt::ItemIsEnabled);

		int idx = i;
		m_sessionList->setItemWidget(item, makeSessionWidget(ref,
			[this, idx] {
				if (idx >= static_cast<int>(m_session.size())) return;
				const SessionEntry &entry = m_session[static_cast<size_t>(idx)];

				// Look up verse text
				const BibleVerse *v = m_bible->verse(
					entry.bookId, entry.chapter, entry.verse);
				QString text = v ? QString::fromStdString(v->text) : "";
				QString r = entry.bookName + " " +
					    QString::number(entry.chapter) + ":" +
					    QString::number(entry.verse);

				// Show on overlay
				if (onShowVerse)
					onShowVerse(entry.bookName, entry.chapter,
						    entry.verse, text, r);

				// Navigate panel to this verse
				navigateTo(entry.bookId, entry.chapter, entry.verse);
			},
			[this, idx] {
				if (idx >= static_cast<int>(m_session.size())) return;
				m_session.erase(m_session.begin() + idx);
				saveSession();
				renderSession();
				updateAddSessionButton();
				populateVerses();
			}));
	}
}

void PanelDock::saveSession()
{
	QSettings s("BibleVerseOverlay", "Panel");
	s.beginWriteArray("session");
	for (int i = 0; i < static_cast<int>(m_session.size()); ++i) {
		s.setArrayIndex(i);
		const SessionEntry &e = m_session[static_cast<size_t>(i)];
		s.setValue("bookId",   e.bookId);
		s.setValue("bookName", e.bookName);
		s.setValue("chapter",  e.chapter);
		s.setValue("verse",    e.verse);
	}
	s.endArray();
}

void PanelDock::loadSession()
{
	QSettings s("BibleVerseOverlay", "Panel");
	int count = s.beginReadArray("session");
	for (int i = 0; i < count; ++i) {
		s.setArrayIndex(i);
		m_session.push_back({
			s.value("bookId").toInt(),
			s.value("bookName").toString(),
			s.value("chapter").toInt(),
			s.value("verse").toInt()});
	}
	s.endArray();
}
