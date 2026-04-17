#pragma once
#include <QString>
#include <obs-module.h>

namespace I18n {

inline bool isSpanish()
{
	// obs_get_locale() devuelve el idioma configurado en OBS, ej. "es-ES", "en-US"
	const char *locale = obs_get_locale();
	return locale && locale[0] == 'e' && locale[1] == 's';
}

// t(español, english) — devuelve el string según el idioma de OBS
inline QString t(const char *es, const char *en)
{
	return QString::fromUtf8(isSpanish() ? es : en);
}

} // namespace I18n
