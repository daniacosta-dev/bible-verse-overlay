#pragma once

#include "bible-data.hpp"

#include <functional>
#include <optional>
#include <vector>

#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QWidget>

class PanelDock : public QWidget {
public:
	explicit PanelDock(BibleData *bible, QWidget *parent = nullptr);

	// Callbacks
	std::function<void(const QString &book, int chapter, int verse,
			   const QString &text, const QString &ref)> onShowVerse;
	std::function<void()> onHideVerse;

private:
	// UI setup
	void buildUi();
	void applyStylesheet();

	// Data helpers
	void populateBooks();
	void populateChapters(int bookId);
	void populateVerses();
	void selectVerseByNumber(int verseNumber);
	void updateActionButtons();
	void updateAddSessionButton();

	// Session helpers
	bool isInSession(int bookId, int chapter, int verse) const;
	void renderSession();
	void saveSession();
	void loadSession();

	// Navigation
	void navigateBy(int delta);
	void navigateTo(int bookId, int chapterNum, int verseNum);

	// Widgets
	QComboBox    *m_bookCombo         = nullptr;
	QComboBox    *m_chapterCombo      = nullptr;
	QListWidget  *m_verseList         = nullptr;
	QPlainTextEdit *m_preview         = nullptr;
	QPushButton  *m_prevBtn           = nullptr;
	QPushButton  *m_nextBtn           = nullptr;
	QPushButton  *m_showBtn           = nullptr;
	QPushButton  *m_hideBtn           = nullptr;
	QPushButton  *m_addSessionBtn     = nullptr;
	QPushButton  *m_clearBtn          = nullptr;
	QLabel       *m_sessionCountLabel = nullptr;
	QPushButton  *m_clearSessionBtn   = nullptr;
	QListWidget  *m_sessionList       = nullptr;

	// Data
	BibleData *m_bible;

	struct SelectedVerse {
		int bookId;
		QString bookName;
		int chapter;
		int verseNumber;
		QString text;
	};

	struct SessionEntry {
		int bookId;
		QString bookName;
		int chapter;
		int verse;
	};

	std::optional<SelectedVerse> m_selected;
	std::vector<SessionEntry> m_session;
};
