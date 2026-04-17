#pragma once

#include <string>
#include <vector>

struct BibleVerse {
	int number;
	std::string text;
};

struct BibleChapter {
	int number;
	std::vector<BibleVerse> verses;
};

struct BibleBook {
	int id;
	std::string name;
	std::string abbrev;
	std::vector<BibleChapter> chapters;
};

class BibleData {
public:
	bool load(const std::string &jsonPath);

	bool isLoaded() const { return m_loaded; }
	const std::string &versionName() const { return m_version; }
	const std::vector<BibleBook> &books() const { return m_books; }

	const BibleBook *book(int id) const;
	const BibleChapter *chapter(int bookId, int chapterNum) const;
	const BibleVerse *verse(int bookId, int chapterNum, int verseNum) const;

private:
	bool m_loaded = false;
	std::string m_version;
	std::vector<BibleBook> m_books;
};
