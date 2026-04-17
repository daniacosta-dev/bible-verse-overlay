#include "bible-data.hpp"
#include "nlohmann/json.hpp"

#include <fstream>

using json = nlohmann::json;

bool BibleData::load(const std::string &jsonPath)
{
	std::ifstream file(jsonPath);
	if (!file.is_open())
		return false;

	json root;
	try {
		file >> root;
	} catch (...) {
		return false;
	}

	m_version = root.value("version", "");

	for (const auto &bookObj : root["books"]) {
		BibleBook book;
		book.id = bookObj.value("id", 0);
		book.name = bookObj.value("name", "");
		book.abbrev = bookObj.value("abbrev", "");

		for (const auto &chapObj : bookObj["chapters"]) {
			BibleChapter chap;
			chap.number = chapObj.value("chapter", 0);

			for (const auto &verseObj : chapObj["verses"]) {
				BibleVerse verse;
				verse.number = verseObj.value("verse", 0);
				verse.text = verseObj.value("text", "");
				chap.verses.push_back(std::move(verse));
			}

			book.chapters.push_back(std::move(chap));
		}

		m_books.push_back(std::move(book));
	}

	m_loaded = !m_books.empty();
	return m_loaded;
}

const BibleBook *BibleData::book(int id) const
{
	for (const BibleBook &b : m_books) {
		if (b.id == id)
			return &b;
	}
	return nullptr;
}

const BibleChapter *BibleData::chapter(int bookId, int chapterNum) const
{
	const BibleBook *b = book(bookId);
	if (!b)
		return nullptr;

	for (const BibleChapter &c : b->chapters) {
		if (c.number == chapterNum)
			return &c;
	}
	return nullptr;
}

const BibleVerse *BibleData::verse(int bookId, int chapterNum, int verseNum) const
{
	const BibleChapter *c = chapter(bookId, chapterNum);
	if (!c)
		return nullptr;

	for (const BibleVerse &v : c->verses) {
		if (v.number == verseNum)
			return &v;
	}
	return nullptr;
}
