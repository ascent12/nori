/* SPDX-License-Identifier: MIT */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <math.h>

#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>

#include <hb.h>
#include <hb-ft.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_BITMAP_H

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-server-core.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>

#include "scene.h"
#include "wayland.h"
#include "vulkan.h"

#include "xdg-shell-protocol.h"

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))

/* Horrible hack until I wire up scene to toplevel/renderer properly */
struct wl_array glyphs, indices;

int main(void)
{
	const char *to_print = u8"AaBbCcDdEe";
	size_t to_print_len = strlen(to_print);
	hb_unicode_funcs_t *funcs = hb_unicode_funcs_get_default();

	int pixel_size = 18;
	int line_height = 0;

	FT_Library ft_lib;
	FT_Init_FreeType(&ft_lib);

	FcResult result;

	/* Load font set */

	FcFontSet *font_set = FcFontSetCreate();

	FcPattern *pat = FcPatternCreate();
	//FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"xos4 Terminus");
	//FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"monospace");
	FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"Noto Sans CJK JP");
	FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"Noto Sans");
	FcPatternAddInteger(pat, FC_SIZE, pixel_size);

	FcConfigSubstitute(NULL, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);

	FcFontSet *matching = FcFontSort(NULL, pat, 1, NULL, &result);
	/* No fonts? */
	if (!matching || matching->nfont == 0)
		return 1;

	int spacing = FC_PROPORTIONAL;
	FcPatternGetInteger(matching->fonts[0], FC_SPACING, 0, &spacing);

	for (int i = 0; i < matching->nfont; ++i) {
		FcPattern *p;
		FcChar8 *file = NULL;
		int s = FC_PROPORTIONAL;

		FcPatternGetInteger(matching->fonts[i], FC_SPACING, 0, &s);
		if (spacing != FC_PROPORTIONAL && s == FC_PROPORTIONAL)
			continue;

		FcPatternGetString(matching->fonts[i], FC_FILE, 0, &file);
		printf("file: %s\n", file);

		p = FcFontRenderPrepare(NULL, pat, matching->fonts[i]);
		if (p)
			FcFontSetAdd(font_set, p);
	}

	FcBool scalable;
	result = FcPatternGetBool(matching->fonts[0], FC_SCALABLE, 0, &scalable);
	if (result == FcResultMatch && !scalable) {
		double size = pixel_size;
		FcPatternGetDouble(matching->fonts[0], FC_PIXEL_SIZE, 0, &size);
		pixel_size = size;
	}

	FcFontSetDestroy(matching);
	FcPatternDestroy(pat);

	printf("===\n");

	hb_buffer_t *buf = hb_buffer_create();
	hb_buffer_t *shaped = hb_buffer_create();
	hb_buffer_t *scratch = hb_buffer_create();

	//struct wl_array glyphs, indices;
	wl_array_init(&glyphs);
	wl_array_init(&indices);

	/* 26.6 fixed point format */
	int32_t pen_26_6 = 0;

	int primitive_index = 0;

	struct scene *scene = scene_create();
	struct scene_layer *root = scene_layer_create();
	scene_set_root(scene, root);

	for (const char *str = to_print; str <= to_print + to_print_len;) {
		FcChar32 ucs = 0;
		hb_script_t script = HB_SCRIPT_INVALID;
		char tag[5] = {0};

		if (*str) {
			str += FcUtf8ToUcs4((FcChar8 *)str, &ucs, to_print + to_print_len - str);
			script = hb_unicode_script(funcs, ucs);
		} else {
			++str;
		}

		if (script == hb_buffer_get_script(buf) || script == HB_SCRIPT_INHERITED)
			goto add;

		if (hb_buffer_get_length(buf) == 0)
			goto skip;

		FT_Face ft_face;
		int best_missing = INT_MAX;

		for (int i = 0; i < font_set->nfont; ++i) {
			/* Load font */

			if (FcPatternGetFTFace(font_set->fonts[i], FC_FT_FACE, 0, &ft_face) == FcResultNoMatch) {
				FT_Error error;
				FcChar8 *file = NULL;

				FcPatternGetString(font_set->fonts[i], FC_FILE, 0, &file);
				printf("file: %s\n", file);

				error = FT_New_Face(ft_lib, (const char *)file, 0, &ft_face);
				if (error) {
					fprintf(stderr, "FT_New_Face: %d\n", error);
					return 1;
				}

				if (ft_face->num_fixed_sizes) {
					int best = 0;
					int best_diff = INT_MAX;

					printf("Num fixed sizes: %d\n", ft_face->num_fixed_sizes);
					for (int i = 0; i < ft_face->num_fixed_sizes; ++i) {
						printf("Fixed size: %d\n",
							       ft_face->available_sizes[i].width);
						int diff = abs(pixel_size -
							       ft_face->available_sizes[i].width);
						if (diff < best_diff) {
							best = i;
							best_diff = diff;
						}
					}

					printf("Best: %d\n", best);
					FT_Select_Size(ft_face, best);
				} else {
					FT_Set_Pixel_Sizes(ft_face, 0, pixel_size);
				}

				if (line_height < ft_face->ascender - ft_face->descender)
					line_height = ft_face->ascender - ft_face->descender;

				FcPatternAddFTFace(font_set->fonts[i], FC_FT_FACE, ft_face);
			}

			hb_font_t *hb_font = hb_ft_font_create_referenced(ft_face);

			hb_segment_properties_t props;
			hb_buffer_get_segment_properties(buf, &props);

			hb_buffer_clear_contents(scratch);
			hb_buffer_set_segment_properties(scratch, &props);
			hb_buffer_append(scratch, buf, 0, hb_buffer_get_length(buf));

			hb_shape(hb_font, scratch, NULL, 0);//features, sizeof features / sizeof features[0]);
			hb_buffer_set_cluster_level(scratch, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);

			hb_font_destroy(hb_font);

			int missing = 0;
			unsigned len;
			hb_glyph_info_t *info = hb_buffer_get_glyph_infos(scratch, &len);
			for (unsigned i = 0; i < len; ++i) {
				if (info[i].codepoint == 0)
					++missing;
			}

			if (missing < best_missing) {
				best_missing = missing;

				hb_buffer_t *tmp = shaped;
				shaped = scratch;
				scratch = tmp;
			}
			if (missing == 0)
				break;
		}

		printf("missing characters: %d\n", best_missing);

		unsigned len;
		hb_glyph_info_t *info = hb_buffer_get_glyph_infos(shaped, &len);
		hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(shaped, &len);

		for (unsigned i = 0; i < len; ++i) {
			printf("SHAPE: %d: advance: %dx%d, offset: %dx%d\n",
				info[i].codepoint,
				pos[i].x_advance >> 6, pos[i].y_advance >> 6,
				pos[i].x_offset >> 6, pos[i].y_offset >> 6);
			printf("Cluster: %d\n", info[i].cluster);

			FT_Load_Glyph(ft_face, info[i].codepoint, 0);
			FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL);
			FT_Bitmap *bitmap = &ft_face->glyph->bitmap;

			printf("bitmap: %dx%d %d %d %d\n", bitmap->width, bitmap->rows,
				bitmap->pitch, bitmap->pixel_mode, bitmap->num_grays);

			if (bitmap->width == 0 || bitmap->rows == 0)
				goto advance;

			int32_t x = (pen_26_6 >> 6) + ft_face->glyph->bitmap_left;
			int32_t y = (ft_face->ascender >> 6) - ft_face->glyph->bitmap_top;
			int32_t width = bitmap->width;
			int32_t height = bitmap->rows;

			float *verts = wl_array_add(&glyphs, sizeof *verts * 8);
			// Top left
			verts[0] = x;
			verts[1] = y;
			// Top right
			verts[2] = x + width;
			verts[3] = y;
			// Bottom right
			verts[4] = x + width;
			verts[5] = y + height;
			// Bottom left
			verts[6] = x;
			verts[7] = y + height;

			uint16_t *ind = wl_array_add(&indices, sizeof *ind * 6);
			ind[0] = primitive_index + 0;
			ind[1] = primitive_index + 1;
			ind[2] = primitive_index + 2;
			ind[3] = primitive_index + 0;
			ind[4] = primitive_index + 2;
			ind[5] = primitive_index + 3;
			primitive_index += 4;

			struct scene_view *v = scene_view_create(width, height);
			scene_push(root, v);
			scene_set_pos(v, x, y);
advance:
			pen_26_6 += pos[i].x_advance;
		}

skip:
		hb_tag_to_string(hb_script_to_iso15924_tag(script), tag);
		printf("Tag: %s\n", tag);

		hb_buffer_clear_contents(buf);

		hb_buffer_set_content_type(buf, HB_BUFFER_CONTENT_TYPE_UNICODE);
		hb_buffer_set_script(buf, script);
		hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
		hb_buffer_set_cluster_level(buf, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);

add:
		if (ucs) {
			printf("%lc\n", ucs);
			hb_buffer_add(buf, ucs, hb_buffer_get_length(buf));
		}
	}

	printf("===\n");

	scene_dump(scene);

	printf("===\n");

	float (*glyphs_data)[8] = glyphs.data;
	for (size_t i = 0; i < glyphs.size / sizeof *glyphs_data; ++i) {
		for (int j = 0; j < 4; ++j)
			printf("(%.1f, %.1f), ", glyphs_data[i][j * 2],
			       glyphs_data[i][j * 2 + 1]);
		printf("\n");
	}

	printf("===\n");

	uint16_t (*indices_data)[6] = indices.data;
	for (size_t i = 0; i < indices.size / sizeof *indices_data; ++i) {
		for (int j = 0; j < 6; ++j)
			printf("%hu, ", indices_data[i][j]);
		printf("\n");
	}

	printf("===\n");

	struct wl_event_loop *ev = wl_event_loop_create();
	struct wayland wl = {0};
	struct vulkan vk = {0};

	wl_list_init(&wl.seats);

	if (wayland_connect(&wl, ev) < 0)
		return 1;

	if (vulkan_create(&vk, wl.display) < 0)
		return 1;

	if (vulkan_init_renderpass(&vk, &vk.renderpass) < 0)
		return 1;

	struct wayland_toplevel *top = wayland_toplevel_create(&wl, &vk);

	wayland_surface_schedule_repaint(&top->base);
	while (!wl.exit && !top->close)
		wl_event_loop_dispatch(ev, -1);

	return 0;
}
