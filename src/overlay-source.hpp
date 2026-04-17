#pragma once

#include <string>

// Registra el tipo de source con OBS — llamar en obs_module_load
void overlay_source_register();

// Libera recursos gráficos — llamar en obs_module_unload
void overlay_source_unload();

// Mostrar / ocultar versículo en todas las instancias activas
void overlay_show_verse(const std::string &book, int chapter, int verse,
			const std::string &text, const std::string &ref);
void overlay_hide_verse();
