#include "gui_emu_tools.h"
#include "gui.h"
#include "gui_emummc_tools.h"
#include "gui_emusd_tools.h"
#include <libs/lvgl/lv_core/lv_obj.h>
#include <libs/lvgl/lv_core/lv_style.h>
#include <libs/lvgl/lv_objx/lv_cont.h>
#include <libs/lvgl/lv_objx/lv_label.h>
#include <libs/lvgl/lv_objx/lv_line.h>
#include <libs/lvgl/lv_objx/lv_page.h>
#include <libs/lvgl/lv_objx/lv_tabview.h>
#include <libs/lvgl/lv_objx/lv_win.h>
#include <libs/lvgl/lv_themes/lv_theme.h>
#include <gfx_utils.h>

static lv_obj_t *_create_container(lv_obj_t *parent){
	static lv_style_t h_style;
	lv_style_copy(&h_style, &lv_style_transp);
	h_style.body.padding.inner = 0;
	h_style.body.padding.hor = 0;
	h_style.body.padding.ver = 0;

	lv_obj_t *h1 = lv_cont_create(parent, NULL);
	lv_cont_set_style(h1, &h_style);
	lv_cont_set_fit(h1, false, true);
	lv_obj_set_width(h1, LV_HOR_RES - 62);
	lv_obj_set_click(h1, false);
	lv_cont_set_layout(h1, LV_LAYOUT_OFF);

	return h1;
}

lv_res_t create_win_emu_tools(lv_obj_t *btn){
	lv_obj_t *win = nyx_create_standard_window(SYMBOL_EDIT "  emuMMC Manage", NULL);

	static lv_style_t win_style_no_pad;
	lv_style_copy(&win_style_no_pad, lv_win_get_style(win, LV_WIN_STYLE_CONTENT_BG));
	win_style_no_pad.body.padding.hor = 0;
	win_style_no_pad.body.padding.inner = 0;

	lv_win_set_style(win, LV_WIN_STYLE_CONTENT_BG, &win_style_no_pad);

	lv_obj_t *tv = lv_tabview_create(win, NULL);
	lv_obj_set_size(tv, LV_HOR_RES - 62, 572);

	gfx_printf("w: %d\n",(u32)lv_obj_get_width(tv));

	static lv_style_t tv_style;
	lv_style_copy(&tv_style, lv_theme_get_current()->tabview.btn.rel);
	tv_style.body.padding.ver = LV_DPI / 8;
	tv_style.body.padding.hor = 0;

	lv_tabview_set_style(tv, LV_TABVIEW_STYLE_BTN_REL, &tv_style);

	if(hekate_bg){
		lv_tabview_set_style(tv, LV_TABVIEW_STYLE_BTN_PR, &tabview_btn_pr);
		lv_tabview_set_style(tv, LV_TABVIEW_STYLE_BTN_TGL_PR, &tabview_btn_tgl_pr);
	}

	lv_tabview_set_sliding(tv, false);
	lv_tabview_set_btns_pos(tv, LV_TABVIEW_BTNS_POS_BOTTOM);

	lv_obj_t *tab_emummc = lv_tabview_add_tab(tv, SYMBOL_CHIP "  emuMMC");
	lv_obj_t *tab_emusd = lv_tabview_add_tab(tv, SYMBOL_SD "  emuSD");

	lv_obj_t *line_sep = lv_line_create(tv, NULL);
	static const lv_point_t line_pp[] = {{0,0}, {0, LV_DPI / 4}};
	lv_line_set_points(line_sep, line_pp, 2);
	lv_line_set_style(line_sep, lv_theme_get_current()->line.decor);
	lv_obj_align(line_sep, tv, LV_ALIGN_IN_BOTTOM_MID, -1, -LV_DPI * 2 / 12);

	create_tab_emummc_tools(_create_container(tab_emummc));
	create_tab_emusd_tools(_create_container(tab_emusd));

	lv_tabview_set_tab_act(tv, 0, false);

	return LV_RES_OK;
}

