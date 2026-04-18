#include "overlay-source.hpp"
#include "i18n.hpp"

#include <mutex>
#include <string>
#include <unordered_map>

#include <obs-module.h>
#include <obs-properties.h>
#include <plugin-support.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
// Custom effect with opacity uniform (for fade)
// ─────────────────────────────────────────────────────────────────────────────
static const char *FADE_EFFECT_SRC = R"(
uniform texture2d image;
uniform float4x4  ViewProj;
uniform float     opacity;

sampler_state def_sampler {
    Filter   = Linear;
    AddressU = Clamp;
    AddressV = Clamp;
};

struct VertInOut {
    float4 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

VertInOut VSDefault(VertInOut v_in)
{
    VertInOut vert_out;
    vert_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
    vert_out.uv  = v_in.uv;
    return vert_out;
}

float4 PSDefault(VertInOut v_in) : TARGET
{
    float4 color = image.Sample(def_sampler, v_in.uv);
    color *= opacity;
    return color;
}

technique Draw
{
    pass
    {
        vertex_shader = VSDefault(v_in);
        pixel_shader  = PSDefault(v_in);
    }
}
)";

static gs_effect_t *g_fade_effect    = nullptr;
static gs_eparam_t *g_param_image    = nullptr;
static gs_eparam_t *g_param_opacity  = nullptr;

static void ensure_fade_effect()
{
	if (g_fade_effect) return;
	char *errors = nullptr;
	g_fade_effect = gs_effect_create(FADE_EFFECT_SRC, nullptr, &errors);
	if (!g_fade_effect) {
		obs_log(LOG_ERROR, "Failed to compile fade effect: %s",
			errors ? errors : "(unknown)");
		bfree(errors);
		return;
	}
	g_param_image   = gs_effect_get_param_by_name(g_fade_effect, "image");
	g_param_opacity = gs_effect_get_param_by_name(g_fade_effect, "opacity");
}

// ─────────────────────────────────────────────────────────────────────────────
// Available bundled Google Fonts
// ─────────────────────────────────────────────────────────────────────────────
struct FontDef {
	const char *label;    // español
	const char *en_label; // english (nullptr = igual que label)
	const char *family;
	const char *filename; // nullptr = system font (no file)
};

static const FontDef FONTS[] = {
	{"Georgia (predeterminado)", "Georgia (default)", "Georgia",         nullptr},
	{"Nunito",                   nullptr,             "Nunito",           "Nunito-Regular.ttf"},
	{"Roboto",                   nullptr,             "Roboto",           "Roboto-Regular.ttf"},
	{"Open Sans",                nullptr,             "Open Sans",        "OpenSans-Regular.ttf"},
	{"Lato",                     nullptr,             "Lato",             "Lato-Regular.ttf"},
	{"Montserrat",               nullptr,             "Montserrat",       "Montserrat-Regular.ttf"},
	{"Poppins",                  nullptr,             "Poppins",          "Poppins-Regular.ttf"},
	{"Oswald",                   nullptr,             "Oswald",           "Oswald-Regular.ttf"},
	{"Raleway",                  nullptr,             "Raleway",          "Raleway-Regular.ttf"},
	{"Playfair Display",         nullptr,             "Playfair Display", "PlayfairDisplay-Regular.ttf"},
	{"Merriweather",             nullptr,             "Merriweather",     "Merriweather-Regular.ttf"},
};

// Maps FontDef::family → actual Qt-registered family name (resolved at load time)
static std::unordered_map<std::string, std::string> g_fontFamilyMap;

// ─────────────────────────────────────────────────────────────────────────────
// Load bundled Google Fonts into Qt font database
// ─────────────────────────────────────────────────────────────────────────────
void overlay_load_fonts()
{
	for (const auto &f : FONTS) {
		// System fonts need no file — register identity mapping
		if (!f.filename) {
			g_fontFamilyMap[f.family] = f.family;
			continue;
		}

		std::string rel = std::string("fonts/") + f.filename;
		const char *path = obs_module_file(rel.c_str());
		if (!path) {
			obs_log(LOG_WARNING, "Font file not found: %s", rel.c_str());
			g_fontFamilyMap[f.family] = f.family;
			continue;
		}

		int id = QFontDatabase::addApplicationFont(QString::fromUtf8(path));
		bfree((void *)path);

		if (id < 0) {
			obs_log(LOG_WARNING, "Failed to load font: %s", f.filename);
			g_fontFamilyMap[f.family] = f.family;
			continue;
		}

		// Variable fonts may register under a different name than the family field
		QStringList registered = QFontDatabase::applicationFontFamilies(id);
		if (!registered.isEmpty()) {
			std::string actual = registered.first().toStdString();
			g_fontFamilyMap[f.family] = actual;
			if (actual != f.family)
				obs_log(LOG_INFO, "Font '%s' registered as '%s'",
					f.family, actual.c_str());
		} else {
			g_fontFamilyMap[f.family] = f.family;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared verse state
// ─────────────────────────────────────────────────────────────────────────────
struct VerseState {
	std::string text;
	std::string ref;
	bool        visible = false;
};

static std::mutex g_mutex;
static VerseState g_state;

// ─────────────────────────────────────────────────────────────────────────────
// Per-instance settings (from OBS properties)
// ─────────────────────────────────────────────────────────────────────────────
struct OverlaySettings {
	int         fontSize     = 38;       // pt at 1920px wide
	std::string fontFamily   = "Georgia";
	QColor      verseColor   = {255, 255, 255, 255};
	QColor      refColor     = {255, 224, 138, 255};
	QColor      bgColor      = {0,   0,   0,   145};
	int         position     = 0;        // 0=bottom 1=center 2=top
	int         textAlign    = 0;        // 0=left 1=center 2=right
	float       fadeDuration = 0.4f;     // seconds
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-instance data
// ─────────────────────────────────────────────────────────────────────────────
struct OverlayInstance {
	obs_source_t   *source  = nullptr;
	gs_texture_t   *texture = nullptr;
	uint32_t        texW    = 0;
	uint32_t        texH    = 0;

	float alpha   = 0.0f;
	bool  fadeIn  = false;
	bool  fadeOut = false;

	std::string     lastText;
	std::string     lastRef;
	bool            lastVisible = false;
	OverlaySettings settings;
	OverlaySettings lastSettings; // detect settings changes
};

// ─────────────────────────────────────────────────────────────────────────────
// Render verse to QImage
// ─────────────────────────────────────────────────────────────────────────────
static QImage renderVerseImage(const QString &text, const QString &ref,
			       int width, int height,
			       const OverlaySettings &s)
{
	QImage img(width, height, QImage::Format_RGBA8888);
	img.fill(Qt::transparent);

	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);

	const int boxMarginH = width * 0.04;
	const int boxPad     = 36;

	// Vertical anchor based on position setting
	int boxBottom;
	if      (s.position == 2) boxBottom = height * 0.30; // top
	else if (s.position == 1) boxBottom = height * 0.60; // center
	else                      boxBottom = height * 0.92; // bottom

	int scaledSize = s.fontSize * width / 1920;
	auto it = g_fontFamilyMap.find(s.fontFamily);
	QString ff = QString::fromStdString(
		it != g_fontFamilyMap.end() ? it->second : s.fontFamily);
	QFont verseFont(ff, scaledSize);
	QFont refFont(ff, scaledSize * 10 / 16);

	QFontMetrics verseFm(verseFont);
	QFontMetrics refFm(refFont);

	int boxInnerW = width - 2 * boxMarginH - 2 * boxPad;
	QRect textBounds(0, 0, boxInnerW, height * 2);

	QRect verseRect = verseFm.boundingRect(textBounds,
		Qt::TextWordWrap, text);
	QRect refRect = refFm.boundingRect(textBounds,
		Qt::TextWordWrap, ref);

	int boxH   = verseRect.height() + refRect.height() + boxPad * 2 + 16;
	int boxTop = boxBottom - boxH;
	int boxW   = width - 2 * boxMarginH;

	// Background
	p.setPen(Qt::NoPen);
	p.setBrush(s.bgColor);
	p.drawRoundedRect(QRectF(boxMarginH, boxTop, boxW, boxH), 12, 12);

	Qt::AlignmentFlag hAlign;
	if      (s.textAlign == 1) hAlign = Qt::AlignHCenter;
	else if (s.textAlign == 2) hAlign = Qt::AlignRight;
	else                       hAlign = Qt::AlignLeft;

	// Verse text
	QRect inner(boxMarginH + boxPad, boxTop + boxPad,
		    boxInnerW, verseRect.height() + 8);
	p.setFont(verseFont);
	p.setPen(s.verseColor);
	p.drawText(inner, Qt::TextWordWrap | hAlign | Qt::AlignTop, text);

	// Reference
	QRect refDraw(boxMarginH + boxPad,
		      boxTop + boxPad + verseRect.height() + 16,
		      boxInnerW, refRect.height());
	p.setFont(refFont);
	p.setPen(s.refColor);
	p.drawText(refDraw, Qt::TextWordWrap | hAlign, ref);

	p.end();
	return img;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: obs color int → QColor
// ─────────────────────────────────────────────────────────────────────────────
// OBS stores colors as 0xAABBGGRR (ABGR) — R is at the lowest byte
static QColor obsColorToQColor(long long c)
{
	int a = (c >> 24) & 0xFF;
	int b = (c >> 16) & 0xFF;
	int g = (c >>  8) & 0xFF;
	int r = (c      ) & 0xFF;
	if (a == 0 && (r | g | b)) a = 255;
	return QColor(r, g, b, a);
}

static long long qcolorToObs(const QColor &c)
{
	return ((long long)c.alpha() << 24) |
	       ((long long)c.blue()  << 16) |
	       ((long long)c.green() <<  8) |
	        (long long)c.red();
}

// Helper para definir colores ABGR desde componentes RGB
static constexpr long long abgr(int a, int r, int g, int b)
{
	return ((long long)a << 24) | ((long long)b << 16) |
	       ((long long)g <<  8) | (long long)r;
}

// ─────────────────────────────────────────────────────────────────────────────
// OBS source callbacks
// ─────────────────────────────────────────────────────────────────────────────
static const char *overlay_get_name(void *) { return "Bible Verse Overlay"; }

static void overlay_update(void *data, obs_data_t *settings);

static void *overlay_create(obs_data_t *settings, obs_source_t *source)
{
	auto *inst   = new OverlayInstance();
	inst->source = source;
	overlay_update(inst, settings);
	return inst;
}

static void overlay_destroy(void *data)
{
	auto *inst = static_cast<OverlayInstance *>(data);
	obs_enter_graphics();
	if (inst->texture) { gs_texture_destroy(inst->texture); }
	obs_leave_graphics();
	delete inst;
}

static void overlay_update(void *data, obs_data_t *settings)
{
	auto *inst = static_cast<OverlayInstance *>(data);
	OverlaySettings &s = inst->settings;

	s.fontSize     = static_cast<int>(obs_data_get_int(settings, "font_size"));
	const char *ff = obs_data_get_string(settings, "font_family");
	s.fontFamily   = (ff && ff[0]) ? ff : "Georgia";
	s.verseColor   = obsColorToQColor(obs_data_get_int(settings, "verse_color"));
	s.refColor     = obsColorToQColor(obs_data_get_int(settings, "ref_color"));
	s.bgColor      = obsColorToQColor(obs_data_get_int(settings, "bg_color"));
	s.position     = static_cast<int>(obs_data_get_int(settings, "position"));
	s.textAlign    = static_cast<int>(obs_data_get_int(settings, "text_align"));
	s.fadeDuration = static_cast<float>(obs_data_get_double(settings, "fade_duration"));

	// Force texture rebuild on next render
	inst->lastText = "";
	inst->lastRef  = "";
}

struct PresetDef {
	const char *id;
	const char *label;    // español
	const char *en_label; // english
	int         fontSize;
	long long   verseColor;
	long long   refColor;
	long long   bgColor;
	int         position;
	int         textAlign;
	double      fade;
};

static const PresetDef PRESETS[] = {
	{"p_clasico",  "Iglesia Clásica", "Classic Church",    38, abgr(255,255,255,255), abgr(255,255,224,138), abgr(145,  0,  0,  0), 0, 0, 0.4},
	{"p_grande",   "Texto Grande",    "Large Text",        54, abgr(255,255,255,255), abgr(255,255,255,255), abgr(191,  0,  0,  0), 0, 1, 0.3},
	{"p_oscuro",   "Fondo Oscuro",    "Dark Background",   36, abgr(255,240,230,200), abgr(255,201,168, 76), abgr(224,  0,  0,  0), 0, 0, 0.4},
	{"p_lower",    "Lower Third",     "Lower Third",       28, abgr(255,255,255,255), abgr(255,126,184,255), abgr(224, 10, 18, 38), 0, 0, 0.2},
	{"p_centrado", "Centrado",        "Centered",          42, abgr(255,255,255,255), abgr(170,255,255,255), abgr(128,  0,  0,  0), 1, 1, 0.5},
	{"p_minimal",  "Minimal",         "Minimal",           44, abgr(255,255,255,255), abgr(153,255,255,255), abgr(  0,  0,  0,  0), 0, 0, 0.4},
};

static bool preset_changed(obs_properties_t *, obs_property_t *,
			   obs_data_t *settings)
{
	const char *id = obs_data_get_string(settings, "preset");
	if (!id || strcmp(id, "none") == 0) return false;

	for (const auto &p : PRESETS) {
		if (strcmp(p.id, id) != 0) continue;
		obs_data_set_int   (settings, "font_size",     p.fontSize);
		obs_data_set_int   (settings, "verse_color",   p.verseColor);
		obs_data_set_int   (settings, "ref_color",     p.refColor);
		obs_data_set_int   (settings, "bg_color",      p.bgColor);
		obs_data_set_int   (settings, "position",      p.position);
		obs_data_set_int   (settings, "text_align",    p.textAlign);
		obs_data_set_double(settings, "fade_duration", p.fade);
		// Reset selector back to placeholder
		obs_data_set_string(settings, "preset", "none");
		return true; // refresh UI
	}
	return false;
}

static obs_properties_t *overlay_get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	// Preset selector
	obs_property_t *preset = obs_properties_add_list(props, "preset",
		"Preset", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(preset,
		I18n::t("— Aplicar preset —", "— Apply preset —").toUtf8().constData(), "none");
	for (const auto &p : PRESETS)
		obs_property_list_add_string(preset,
			I18n::t(p.label, p.en_label).toUtf8().constData(), p.id);
	obs_property_set_modified_callback(preset, preset_changed);

	obs_property_t *fontProp = obs_properties_add_list(props, "font_family",
		I18n::t("Fuente", "Font").toUtf8().constData(),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const auto &f : FONTS)
		obs_property_list_add_string(fontProp,
			I18n::t(f.label, f.en_label ? f.en_label : f.label).toUtf8().constData(),
			f.family);

	obs_properties_add_int_slider(props, "font_size",
		I18n::t("Tamaño de fuente", "Font size").toUtf8().constData(), 12, 72, 1);

	obs_properties_add_color_alpha(props, "verse_color",
		I18n::t("Color del versículo", "Verse color").toUtf8().constData());

	obs_properties_add_color_alpha(props, "ref_color",
		I18n::t("Color de la referencia", "Reference color").toUtf8().constData());

	obs_properties_add_color_alpha(props, "bg_color",
		I18n::t("Color de fondo (con transparencia)",
			"Background color (with transparency)").toUtf8().constData());

	obs_property_t *pos = obs_properties_add_list(props, "position",
		I18n::t("Posición vertical", "Vertical position").toUtf8().constData(),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(pos, I18n::t("Abajo",  "Bottom").toUtf8().constData(), 0);
	obs_property_list_add_int(pos, I18n::t("Centro", "Center").toUtf8().constData(), 1);
	obs_property_list_add_int(pos, I18n::t("Arriba", "Top").toUtf8().constData(),    2);

	obs_property_t *align = obs_properties_add_list(props, "text_align",
		I18n::t("Alineación del texto", "Text alignment").toUtf8().constData(),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(align, I18n::t("Izquierda", "Left").toUtf8().constData(),     0);
	obs_property_list_add_int(align, I18n::t("Centrado",  "Centered").toUtf8().constData(), 1);
	obs_property_list_add_int(align, I18n::t("Derecha",   "Right").toUtf8().constData(),    2);

	obs_properties_add_float_slider(props, "fade_duration",
		I18n::t("Duración del fade (segundos)", "Fade duration (seconds)").toUtf8().constData(),
		0.0, 2.0, 0.05);

	return props;
}

static void overlay_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "preset",        "none");
	obs_data_set_default_string(settings, "font_family",  "Georgia");
	obs_data_set_default_int   (settings, "font_size",     38);
	obs_data_set_default_int   (settings, "verse_color",   abgr(255,255,255,255)); // white
	obs_data_set_default_int   (settings, "ref_color",     abgr(255,255,224,138)); // gold
	obs_data_set_default_int   (settings, "bg_color",      abgr(145,  0,  0,  0)); // dark semi-transparent
	obs_data_set_default_int   (settings, "position",      0);
	obs_data_set_default_int   (settings, "text_align",    0);
	obs_data_set_default_double(settings, "fade_duration", 0.4);
}

static uint32_t overlay_get_width(void *)
{
	obs_video_info ovi;
	if (obs_get_video_info(&ovi)) return ovi.base_width;
	return 1920;
}

static uint32_t overlay_get_height(void *)
{
	obs_video_info ovi;
	if (obs_get_video_info(&ovi)) return ovi.base_height;
	return 1080;
}

static void overlay_video_tick(void *data, float seconds)
{
	auto *inst = static_cast<OverlayInstance *>(data);
	float speed = inst->settings.fadeDuration > 0.0f
		? 1.0f / inst->settings.fadeDuration : 999.0f;

	if (inst->fadeIn) {
		inst->alpha += seconds * speed;
		if (inst->alpha >= 1.0f) { inst->alpha = 1.0f; inst->fadeIn = false; }
	} else if (inst->fadeOut) {
		inst->alpha -= seconds * speed;
		if (inst->alpha <= 0.0f) { inst->alpha = 0.0f; inst->fadeOut = false; }
	}
}

static void overlay_video_render(void *data, gs_effect_t *)
{
	auto *inst = static_cast<OverlayInstance *>(data);

	ensure_fade_effect();

	VerseState snap;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		snap = g_state;
	}

	// Fade triggers
	if (snap.visible != inst->lastVisible) {
		inst->lastVisible = snap.visible;
		if (snap.visible) { inst->fadeIn = true;  inst->fadeOut = false; }
		else              { inst->fadeOut = true; inst->fadeIn  = false; }
	}

	// Rebuild texture on text or settings change
	bool textChanged     = (snap.text != inst->lastText || snap.ref != inst->lastRef);
	bool settingsChanged = (inst->settings.fontSize    != inst->lastSettings.fontSize   ||
				inst->settings.fontFamily  != inst->lastSettings.fontFamily  ||
				inst->settings.verseColor  != inst->lastSettings.verseColor  ||
				inst->settings.refColor    != inst->lastSettings.refColor    ||
				inst->settings.bgColor     != inst->lastSettings.bgColor     ||
				inst->settings.position    != inst->lastSettings.position    ||
				inst->settings.textAlign   != inst->lastSettings.textAlign);

	if ((textChanged || settingsChanged) && !snap.text.empty()) {
		inst->lastText     = snap.text;
		inst->lastRef      = snap.ref;
		inst->lastSettings = inst->settings;

		uint32_t w = overlay_get_width(nullptr);
		uint32_t h = overlay_get_height(nullptr);

		QImage img = renderVerseImage(
			QString::fromStdString(snap.text),
			QString::fromStdString(snap.ref),
			static_cast<int>(w), static_cast<int>(h),
			inst->settings);

		if (img.bytesPerLine() != static_cast<int>(w * 4))
			img = img.convertToFormat(QImage::Format_RGBA8888);

		if (inst->texture) { gs_texture_destroy(inst->texture); inst->texture = nullptr; }

		const uint8_t *bits = img.constBits();
		inst->texture = gs_texture_create(w, h, GS_RGBA, 1, &bits, 0);
		inst->texW    = w;
		inst->texH    = h;
	}

	if (inst->alpha <= 0.0f || !inst->texture || !g_fade_effect) return;

	gs_effect_set_texture(g_param_image,  inst->texture);
	gs_effect_set_float(g_param_opacity, inst->alpha);

	while (gs_effect_loop(g_fade_effect, "Draw")) {
		gs_draw_sprite(inst->texture, 0, inst->texW, inst->texH);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Source info registration
// ─────────────────────────────────────────────────────────────────────────────
static obs_source_info overlay_source_info_data = {};

void overlay_source_register()
{
	overlay_source_info_data.id             = "bible_verse_overlay";
	overlay_source_info_data.type           = OBS_SOURCE_TYPE_INPUT;
	overlay_source_info_data.output_flags   = OBS_SOURCE_VIDEO |
						  OBS_SOURCE_CUSTOM_DRAW;
	overlay_source_info_data.get_name       = overlay_get_name;
	overlay_source_info_data.create         = overlay_create;
	overlay_source_info_data.destroy        = overlay_destroy;
	overlay_source_info_data.update         = overlay_update;
	overlay_source_info_data.get_properties = overlay_get_properties;
	overlay_source_info_data.get_defaults   = overlay_get_defaults;
	overlay_source_info_data.get_width      = overlay_get_width;
	overlay_source_info_data.get_height     = overlay_get_height;
	overlay_source_info_data.video_tick     = overlay_video_tick;
	overlay_source_info_data.video_render   = overlay_video_render;

	obs_register_source(&overlay_source_info_data);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────
void overlay_show_verse(const std::string &book, int chapter, int verse,
			const std::string &text, const std::string &ref)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_state.text    = text;
	g_state.ref     = ref.empty()
		? book + " " + std::to_string(chapter) + ":" + std::to_string(verse)
		: ref;
	g_state.visible = true;
}

void overlay_hide_verse()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_state.visible = false;
}

void overlay_source_unload()
{
	if (g_fade_effect) {
		obs_enter_graphics();
		gs_effect_destroy(g_fade_effect);
		obs_leave_graphics();
		g_fade_effect   = nullptr;
		g_param_image   = nullptr;
		g_param_opacity = nullptr;
	}
}
