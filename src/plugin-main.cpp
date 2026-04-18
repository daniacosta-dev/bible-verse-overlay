/*
Bible Verse Overlay — OBS Plugin
Copyright (C) 2025 Dani Acosta <daniacosta.dev@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "bible-data.hpp"
#include "i18n.hpp"
#include "overlay-source.hpp"
#include "panel-dock.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QByteArray>
#include <QDockWidget>
#include <QMainWindow>
#include <QSettings>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static BibleData   *g_bible      = nullptr;
static PanelDock   *g_dock       = nullptr;
static QDockWidget *g_dockWidget = nullptr;
static bool         g_obsExiting = false;
static std::string  g_rv1909Path;

// ── Persistencia del dock ────────────────────────────────────────────────────

static void saveDockState()
{
	if (!g_dockWidget) return;
	QSettings s("BibleVerseOverlay", "Panel");
	s.setValue("dockVisible",  g_dockWidget->isVisible());
	s.setValue("dockFloating", g_dockWidget->isFloating());
	s.setValue("dockGeometry", g_dockWidget->saveGeometry());
	if (!g_dockWidget->isFloating()) {
		auto *mw = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		if (mw) s.setValue("dockArea", (int)mw->dockWidgetArea(g_dockWidget));
	}
}

static void restoreDockState()
{
	if (!g_dockWidget) return;
	QSettings s("BibleVerseOverlay", "Panel");
	const bool firstRun = !s.contains("dockVisible");
	bool visible  = s.value("dockVisible",  true).toBool();
	bool floating = s.value("dockFloating", false).toBool();
	auto area     = (Qt::DockWidgetArea)s.value("dockArea", (int)Qt::RightDockWidgetArea).toInt();
	QByteArray geom = s.value("dockGeometry").toByteArray();
	auto *mw = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (firstRun || !floating) {
		if (mw) mw->addDockWidget(area, g_dockWidget);
		g_dockWidget->setFloating(false);
	} else {
		g_dockWidget->setFloating(true);
		if (!geom.isEmpty()) g_dockWidget->restoreGeometry(geom);
	}
	g_dockWidget->setVisible(visible);
}

// ── Evento frontend ──────────────────────────────────────────────────────────

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		g_obsExiting = true;
		saveDockState();
		g_dockWidget = nullptr;
		return;
	}

	if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING) return;

	g_bible = new BibleData();
	if (!g_bible->load(g_rv1909Path))
		obs_log(LOG_WARNING, "rv1909.json not found — place it in the plugin data folder");

	g_dock = new PanelDock(g_bible);
	g_dock->onShowVerse = [](const QString &book, int chapter, int verse,
				 const QString &text, const QString &ref) {
		overlay_show_verse(book.toStdString(), chapter, verse,
				   text.toStdString(), ref.toStdString());
	};
	g_dock->onHideVerse = []() { overlay_hide_verse(); };

	QByteArray title = I18n::t("Bible Overlay", "Bible Overlay").toUtf8();
	obs_frontend_add_dock_by_id("bible_overlay_panel", title.constData(), g_dock);

	g_dockWidget = qobject_cast<QDockWidget *>(g_dock->parentWidget());
	g_dock = nullptr;

	restoreDockState();

	if (g_dockWidget) {
		QObject::connect(g_dockWidget, &QDockWidget::visibilityChanged,
			[](bool v) {
				if (!g_obsExiting) {
					QSettings s("BibleVerseOverlay", "Panel");
					s.setValue("dockVisible", v);
				}
			});
		QObject::connect(g_dockWidget, &QDockWidget::topLevelChanged,
			[](bool) { if (!g_obsExiting) saveDockState(); });
		QObject::connect(g_dockWidget, &QDockWidget::dockLocationChanged,
			[](Qt::DockWidgetArea) { if (!g_obsExiting) saveDockState(); });
	}
}

// ── Módulo ───────────────────────────────────────────────────────────────────

bool obs_module_load()
{
	obs_log(LOG_INFO, "Bible Verse Overlay loaded (version %s)", PLUGIN_VERSION);

	const char *path = obs_module_file("rv1909.json");
	if (path) { g_rv1909Path = path; bfree((void *)path); }

	overlay_source_register();
	overlay_load_fonts();
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	return true;
}

void obs_module_unload()
{
	overlay_source_unload();
	delete g_bible;
	g_bible = nullptr;
	obs_log(LOG_INFO, "Bible Verse Overlay unloaded");
}
