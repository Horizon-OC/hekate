/*
 * Copyright (c) 2026 Souldbminer, Lightos_ and Horizon OC contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>

#include <bdk.h>

#include "gui.h"
#include "gui_l4t_oc.h"
#include <libs/fatfs/ff.h>
#include <utils/dirlist.h>
#include <utils/ini.h>

#define DRAM_VDD2_OC_MIN_VOLTAGE 1050
#define DRAM_VDD2_OC_MAX_VOLTAGE 1175
#define DRAM_VDDQ_OC_MIN_VOLTAGE 550
#define DRAM_VDDQ_OC_MAX_VOLTAGE 650

#define L4T_OC_FREQ_MIN 1600000
#define L4T_OC_FREQ_MAX 3200000

enum {
  L4T_IDX_WRL = 0,
  L4T_IDX_TRP,
  L4T_IDX_TRAS,
  L4T_IDX_TRCD,
  L4T_IDX_TRTW,
  L4T_IDX_TWTR,
  L4T_IDX_TRFC,
  L4T_IDX_TRRD,
  L4T_IDX_VDD2,
  L4T_IDX_VDDQ,
  L4T_IDX_FREQ,
  L4T_IDX_AC,
  L4T_IDX_HP,
};

static struct {
  char sec_name[128];
  char path[384];

  u32 freq;
  u32 vdd2;
  u32 vddq;
  u8 wrl;
  u8 ac;
  u8 hp;
  u8 t[7]; // tRP, tRAS, tRCD, tRTW, tWTR, tRFC, tRRD digits.

  lv_obj_t *win;
  lv_obj_t *preview;
  lv_obj_t *kb_ta;
  lv_obj_t *edit_btn;
  u32 edit_idx;
  char preview_buf[256];
} l4t_oc;

static u32 _l4t_encode_opt() {
  u32 flags = (l4t_oc.ac ? 2 : 0) + (l4t_oc.hp ? 1 : 0);

  return l4t_oc.wrl * 100000000u + flags * 10000000u + l4t_oc.t[0] * 1000000u +
         l4t_oc.t[1] * 100000u + l4t_oc.t[2] * 10000u + l4t_oc.t[3] * 1000u +
         l4t_oc.t[4] * 100u + l4t_oc.t[5] * 10u + l4t_oc.t[6];
}

static void _l4t_decode_opt(u32 opt) {
  l4t_oc.wrl = (opt / 100000000u) % 10;
  u32 flags = (opt / 10000000u) % 10;
  l4t_oc.ac = (flags >> 1) & 1;
  l4t_oc.hp = flags & 1;
  l4t_oc.t[0] = (opt / 1000000u) % 10;
  l4t_oc.t[1] = (opt / 100000u) % 10;
  l4t_oc.t[2] = (opt / 10000u) % 10;
  l4t_oc.t[3] = (opt / 1000u) % 10;
  l4t_oc.t[4] = (opt / 100u) % 10;
  l4t_oc.t[5] = (opt / 10u) % 10;
  l4t_oc.t[6] = opt % 10;
}

static const char *_l4t_timing_name(u32 idx) {
  switch (idx) {
  case L4T_IDX_TRP:
    return "tRP";
  case L4T_IDX_TRAS:
    return "tRAS";
  case L4T_IDX_TRCD:
    return "tRCD";
  case L4T_IDX_TRTW:
    return "tRTW";
  case L4T_IDX_TWTR:
    return "tWTR";
  case L4T_IDX_TRFC:
    return "tRFC";
  default:
    return "tRRD";
  }
}

static int _l4t_pct(u32 idx, u32 v) {
  if (v == 0)
    return 0;

  u32 num, den;
  if (idx <= L4T_IDX_TRCD) // tRP, tRAS, tRCD.
  {
    num = 1000 + 500 * v;
    den = 110 + 5 * v;
  } else // tRTW, tWTR, tRFC, tRRD.
  {
    num = 1000 + 1000 * v;
    den = 110 + 10 * v;
  }

  return -(int)((num + den / 2) / den);
}

static void _l4t_format_field(u32 idx, char *buf) {
  switch (idx) {
  case L4T_IDX_FREQ:
    if (l4t_oc.freq)
      s_printf(buf, SYMBOL_KEYBOARD " Frequency: %d", l4t_oc.freq);
    else
      strcpy(buf, SYMBOL_KEYBOARD " Frequency: Auto");
    break;

  case L4T_IDX_VDD2:
    if (l4t_oc.vdd2)
      s_printf(buf, SYMBOL_KEYBOARD " VDD2: %d mV", l4t_oc.vdd2);
    else
      strcpy(buf, SYMBOL_KEYBOARD " VDD2: Auto");
    break;

  case L4T_IDX_VDDQ:
    if (l4t_oc.vddq)
      s_printf(buf, SYMBOL_KEYBOARD " VDDQ: %d mV", l4t_oc.vddq);
    else
      strcpy(buf, SYMBOL_KEYBOARD " VDDQ: Auto");
    break;

  case L4T_IDX_WRL:
    if (l4t_oc.wrl <= 1)
      s_printf(buf, SYMBOL_KEYBOARD " W/R Latency: %d", l4t_oc.wrl);
    else
      s_printf(buf, SYMBOL_KEYBOARD " W/R Latency: %d  #C7EA46 (-%d)#",
               l4t_oc.wrl, l4t_oc.wrl - 1);
    break;

  default: // Timings.
  {
    u8 v = l4t_oc.t[idx - L4T_IDX_TRP];
    int pct = _l4t_pct(idx, v);
    if (pct)
      s_printf(buf, SYMBOL_KEYBOARD " %s: %d  #C7EA46 (%d%%)#",
               _l4t_timing_name(idx), v, pct);
    else
      s_printf(buf, SYMBOL_KEYBOARD " %s: %d", _l4t_timing_name(idx), v);
    break;
  }
  }
}

static void _l4t_update_preview() {
  char *b = l4t_oc.preview_buf;

  s_printf(b,
           "#00DDFF ram_oc#=%d\n"
           "#00DDFF ram_oc_opt#=%d\n"
           "#00DDFF ram_oc_vdd2#=%d\n"
           "#00DDFF ram_oc_vddq#=%d",
           l4t_oc.freq, _l4t_encode_opt(), l4t_oc.vdd2, l4t_oc.vddq);

  lv_label_set_text(l4t_oc.preview, b);
}

static bool _l4t_read_file(const char *path, const char *name, bool *is_l4t,
                           u32 *oc, u32 *opt, u32 *vdd2, u32 *vddq) {
  LIST_INIT(ini_sections);

  if (ini_parse(&ini_sections, path, false))
    return false;

  bool found = false;
  LIST_FOREACH_ENTRY(ini_sec_t, sec, &ini_sections, link) {
    if (sec->type != INI_CHOICE || strcmp(sec->name, name))
      continue;

    found = true;
    *is_l4t = false;
    *oc = 0;
    *opt = 0;
    *vdd2 = 0;
    *vddq = 0;

    LIST_FOREACH_ENTRY(ini_kv_t, kv, &sec->kvs, link) {
      if (!strcmp(kv->key, "l4t"))
        *is_l4t = (kv->val[0] == '1');
      else if (!strcmp(kv->key, "ram_oc"))
        *oc = atoi(kv->val);
      else if (!strcmp(kv->key, "ram_oc_opt"))
        *opt = atoi(kv->val);
      else if (!strcmp(kv->key, "ram_oc_vdd2"))
        *vdd2 = atoi(kv->val);
      else if (!strcmp(kv->key, "ram_oc_vddq"))
        *vddq = atoi(kv->val);
    }
    break;
  }

  ini_free(&ini_sections);

  return found;
}

static bool _l4t_find_file(const char *name, char *out_path, bool *is_l4t,
                           u32 *oc, u32 *opt, u32 *vdd2, u32 *vddq) {
  if (_l4t_read_file("bootloader/hekate_ipl.ini", name, is_l4t, oc, opt, vdd2,
                     vddq)) {
    strcpy(out_path, "bootloader/hekate_ipl.ini");
    return true;
  }

  dirlist_t *dl = dirlist("bootloader/ini", "*.ini", DIR_ASCII_ORDER);
  if (!dl)
    return false;

  bool found = false;
  for (u32 i = 0; i < DIR_MAX_ENTRIES && dl->name[i]; i++) {
    char path[384];
    s_printf(path, "bootloader/ini/%s", dl->name[i]);
    if (_l4t_read_file(path, name, is_l4t, oc, opt, vdd2, vddq)) {
      strcpy(out_path, path);
      found = true;
      break;
    }
  }

  free(dl);

  return found;
}

static bool _l4t_write_file(const char *path, const char *name, u32 oc, u32 opt,
                            u32 vdd2, u32 vddq) {
  LIST_INIT(ini_sections);

  if (ini_parse(&ini_sections, path, false))
    return false;

  FIL fp;
  if (f_open(&fp, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
    ini_free(&ini_sections);
    return false;
  }

  char lbuf[32];
  LIST_FOREACH_ENTRY(ini_sec_t, sec, &ini_sections, link) {
    switch (sec->type) {
    case INI_CHOICE: {
      bool target = !strcmp(sec->name, name);

      f_puts("[", &fp);
      f_puts(sec->name, &fp);
      f_puts("]\n", &fp);

      LIST_FOREACH_ENTRY(ini_kv_t, kv, &sec->kvs, link) {
        if (target &&
            (!strcmp(kv->key, "ram_oc") || !strcmp(kv->key, "ram_oc_opt") ||
             !strcmp(kv->key, "ram_oc_vdd2") ||
             !strcmp(kv->key, "ram_oc_vddq")))
          continue;

        f_puts(kv->key, &fp);
        f_puts("=", &fp);
        f_puts(kv->val, &fp);
        f_puts("\n", &fp);
      }

      if (target) {
        if (oc) {
          f_puts("ram_oc=", &fp);
          itoa(oc, lbuf, 10);
          f_puts(lbuf, &fp);
          f_puts("\n", &fp);
        }
        if (opt) {
          f_puts("ram_oc_opt=", &fp);
          itoa(opt, lbuf, 10);
          f_puts(lbuf, &fp);
          f_puts("\n", &fp);
        }
        if (vdd2) {
          f_puts("ram_oc_vdd2=", &fp);
          itoa(vdd2, lbuf, 10);
          f_puts(lbuf, &fp);
          f_puts("\n", &fp);
        }
        if (vddq) {
          f_puts("ram_oc_vddq=", &fp);
          itoa(vddq, lbuf, 10);
          f_puts(lbuf, &fp);
          f_puts("\n", &fp);
        }
      }
      break;
    }
    case INI_CAPTION:
      f_puts("{", &fp);
      f_puts(sec->name, &fp);
      f_puts("}\n", &fp);
      break;
    case INI_COMMENT:
      f_puts("#", &fp);
      f_puts(sec->name, &fp);
      f_puts("\n", &fp);
      break;
    case INI_NEWLINE:
      f_puts("\n", &fp);
      break;
    }
  }

  f_close(&fp);
  ini_free(&ini_sections);

  return true;
}

static void _l4t_field_set(u32 idx, u32 v) {
  switch (idx) {
  case L4T_IDX_FREQ:
    if (v) {
      if (v < L4T_OC_FREQ_MIN)
        v = L4T_OC_FREQ_MIN;
      if (v > L4T_OC_FREQ_MAX)
        v = L4T_OC_FREQ_MAX;
    }
    l4t_oc.freq = v;
    break;
  case L4T_IDX_VDD2:
    if (v) {
      if (v < DRAM_VDD2_OC_MIN_VOLTAGE)
        v = DRAM_VDD2_OC_MIN_VOLTAGE;
      if (v > DRAM_VDD2_OC_MAX_VOLTAGE)
        v = DRAM_VDD2_OC_MAX_VOLTAGE;
    }
    l4t_oc.vdd2 = v;
    break;
  case L4T_IDX_VDDQ:
    if (v) {
      if (v < DRAM_VDDQ_OC_MIN_VOLTAGE)
        v = DRAM_VDDQ_OC_MIN_VOLTAGE;
      if (v > DRAM_VDDQ_OC_MAX_VOLTAGE)
        v = DRAM_VDDQ_OC_MAX_VOLTAGE;
    }
    l4t_oc.vddq = v;
    break;
  case L4T_IDX_WRL:
    l4t_oc.wrl = (v > 9) ? 9 : v;
    break;
  default: // Timing digits.
    l4t_oc.t[idx - L4T_IDX_TRP] = (v > 9) ? 9 : v;
    break;
  }
}

static u32 _l4t_field_get(u32 idx) {
  switch (idx) {
  case L4T_IDX_FREQ:
    return l4t_oc.freq;
  case L4T_IDX_VDD2:
    return l4t_oc.vdd2;
  case L4T_IDX_VDDQ:
    return l4t_oc.vddq;
  case L4T_IDX_WRL:
    return l4t_oc.wrl;
  default:
    return l4t_oc.t[idx - L4T_IDX_TRP];
  }
}

static void _l4t_update_field_label(lv_obj_t *btn) {
  char buf[64];
  _l4t_format_field(lv_obj_get_free_num(btn), buf);
  lv_label_set_text((lv_obj_t *)lv_obj_get_free_ptr(btn), buf);
}

static lv_res_t _l4t_toggle_action(lv_obj_t *btn) {
  u32 idx = lv_obj_get_free_num(btn);
  bool on;

  if (idx == L4T_IDX_AC) {
    l4t_oc.ac = !l4t_oc.ac;
    on = l4t_oc.ac;
  } else {
    l4t_oc.hp = !l4t_oc.hp;
    on = l4t_oc.hp;
  }

  lv_btn_set_state(btn, on ? LV_BTN_STATE_TGL_REL : LV_BTN_STATE_REL);
  nyx_generic_onoff_toggle(btn);

  _l4t_update_preview();

  return LV_RES_OK;
}

static void _l4t_topbar_hidden(bool hidden) {
  lv_obj_set_hidden(status_bar.bar_bg, hidden);
  lv_obj_set_hidden(status_bar.line_top, hidden);
  lv_obj_set_hidden(status_bar.line_bottom, hidden);
}

static lv_res_t _l4t_kb_ok_action(lv_obj_t *kb) {
  _l4t_field_set(l4t_oc.edit_idx, atoi(lv_ta_get_text(l4t_oc.kb_ta)));
  _l4t_update_field_label(l4t_oc.edit_btn);
  _l4t_update_preview();

  lv_obj_del(lv_obj_get_parent(kb));
  _l4t_topbar_hidden(false);

  return LV_RES_INV;
}

static lv_res_t _l4t_kb_close_action(lv_obj_t *kb) {
  lv_obj_del(lv_obj_get_parent(kb));
  _l4t_topbar_hidden(false);

  return LV_RES_INV;
}

static lv_res_t _l4t_field_btn_action(lv_obj_t *btn) {
  u32 idx = lv_obj_get_free_num(btn);
  l4t_oc.edit_idx = idx;
  l4t_oc.edit_btn = btn;

  _l4t_topbar_hidden(true);

  char hint[160];
  u16 maxlen;
  switch (idx) {
  case L4T_IDX_FREQ:
    strcpy(hint, "RAM Frequency in #C7EA46 kHz# (e.g. #FF8000 2131200#).\n"
                 "#FF8000 0# = Auto. 1600 - 3200MHz.");
    maxlen = 7;
    break;
  case L4T_IDX_VDD2:
    strcpy(hint,
           "#C7EA46 VDD2# voltage in mV. #FF8000 0# = Auto. 1050-1175mV.");
    maxlen = 4;
    break;
  case L4T_IDX_VDDQ:
    strcpy(hint, "#C7EA46 VDDQ# voltage in mV. #FF8000 0# = Auto. 550-650mV.");
    maxlen = 4;
    break;
  default:
    strcpy(
        hint,
        "Enter a value from #C7EA46 0# to #C7EA46 9#. #FF8000 0# = no change.");
    maxlen = 1;
    break;
  }

  lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
  lv_obj_set_style(dark_bg, &mbox_darken);
  lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

  lv_obj_t *cont = lv_cont_create(dark_bg, NULL);
  lv_cont_set_fit(cont, false, true);
  lv_obj_set_width(cont, LV_HOR_RES * 5 / 10);

  lv_obj_t *lbl = lv_label_create(cont, NULL);
  lv_label_set_recolor(lbl, true);
  lv_label_set_text(lbl, hint);
  lv_obj_align(lbl, NULL, LV_ALIGN_IN_TOP_LEFT, LV_DPI / 8, LV_DPI / 8);

  lv_obj_t *ta = lv_ta_create(cont, NULL);
  lv_ta_set_one_line(ta, true);
  lv_ta_set_cursor_type(ta, LV_CURSOR_LINE);

  lv_ta_set_max_length(ta, maxlen);
  char cur[16];
  s_printf(cur, "%d", _l4t_field_get(idx));
  lv_ta_set_text(ta, cur);
  lv_obj_set_width(ta, LV_HOR_RES * 4 / 10);
  lv_obj_align(ta, lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 4);
  l4t_oc.kb_ta = ta;

  lv_obj_t *kb = lv_kb_create(dark_bg, NULL);
  lv_kb_set_ta(kb, ta);
  lv_kb_set_mode(kb, LV_KB_MODE_NUM);
  lv_kb_set_cursor_manage(kb, true);
  lv_kb_set_ok_action(kb, _l4t_kb_ok_action);
  lv_kb_set_hide_action(kb, _l4t_kb_close_action);
  lv_obj_set_size(kb, LV_HOR_RES, LV_VER_RES * 2 / 5);
  lv_obj_align(kb, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, 0);

  lv_obj_align(cont, kb, LV_ALIGN_OUT_TOP_MID, 0, -LV_DPI / 4);
  lv_obj_set_top(cont, true);

  return LV_RES_OK;
}

static lv_res_t _l4t_save_action(lv_obj_t *btn) {
  if (sd_mount())
    return LV_RES_OK;

  _l4t_write_file(l4t_oc.path, l4t_oc.sec_name, l4t_oc.freq, _l4t_encode_opt(),
                  l4t_oc.vdd2, l4t_oc.vddq);

  sd_unmount();

  lv_obj_del(l4t_oc.win);

  return LV_RES_INV;
}

static lv_res_t _l4t_close_action(lv_obj_t *btn) {
  lv_obj_del(lv_win_get_from_btn(btn));

  return LV_RES_INV;
}

#define L4T_COL_W 340
#define L4T_ROW_H (LV_DPI * 2 / 5)
#define L4T_ROW_GAP (LV_DPI / 6)

static lv_obj_t *_l4t_add_field(lv_obj_t *col, lv_obj_t *prev, u32 idx) {
  lv_obj_t *btn = lv_btn_create(col, NULL);
  lv_btn_set_layout(btn, LV_LAYOUT_OFF);
  lv_obj_set_size(btn, L4T_COL_W, L4T_ROW_H);

  lv_obj_t *lbl = lv_label_create(btn, NULL);
  lv_label_set_recolor(lbl, true);
  lv_obj_align(lbl, btn, LV_ALIGN_IN_LEFT_MID, LV_DPI / 8, 0);

  lv_obj_set_free_num(btn, idx);
  lv_obj_set_free_ptr(btn, lbl);
  lv_btn_set_action(btn, LV_BTN_ACTION_CLICK, _l4t_field_btn_action);

  lv_obj_align(btn, prev, LV_ALIGN_OUT_BOTTOM_LEFT, 0, L4T_ROW_GAP);

  _l4t_update_field_label(btn);

  return btn;
}

static lv_obj_t *_l4t_add_toggle(lv_obj_t *col, lv_obj_t *prev,
                                 const char *name, bool on, u32 idx) {
  lv_theme_t *th = lv_theme_get_current();

  lv_obj_t *btn = lv_btn_create(col, NULL);
  nyx_create_onoff_button(th, col, btn, name, _l4t_toggle_action, false);
  lv_obj_align(btn, prev, LV_ALIGN_OUT_BOTTOM_LEFT, 0, L4T_ROW_GAP);

  lv_obj_set_free_num(btn, idx);
  if (on)
    lv_btn_set_state(btn, LV_BTN_STATE_TGL_REL);
  nyx_generic_onoff_toggle(btn);

  return btn;
}

static lv_obj_t *_l4t_make_column(lv_theme_t *th, u16 x, const char *title) {
  lv_obj_t *col = lv_cont_create(l4t_oc.win, NULL);
  lv_cont_set_style(col, &lv_style_transp);
  lv_cont_set_fit(col, false, false);
  lv_obj_set_size(col, L4T_COL_W, 560);
  lv_cont_set_layout(col, LV_LAYOUT_OFF);
  lv_obj_set_pos(col, x, 10);

  lv_obj_t *lbl = lv_label_create(col, NULL);
  lv_label_set_static_text(lbl, title);
  lv_obj_set_style(lbl, th->label.prim);
  lv_obj_align(lbl, NULL, LV_ALIGN_IN_TOP_LEFT, 0, 0);

  return lbl;
}

void create_window_l4t_oc_editor(const char *entry_name) {
  if (sd_mount())
    return;

  bool is_l4t = false;
  u32 oc = 0, opt = 0, vdd2 = 0, vddq = 0;
  char path[384];
  bool found =
      _l4t_find_file(entry_name, path, &is_l4t, &oc, &opt, &vdd2, &vddq);

  sd_unmount();

  if (!found || !is_l4t)
    return;

  memset(&l4t_oc, 0, sizeof(l4t_oc));
  s_printf(l4t_oc.sec_name, "%s", entry_name);
  s_printf(l4t_oc.path, "%s", path);
  l4t_oc.freq = oc;
  l4t_oc.vdd2 = vdd2;
  l4t_oc.vddq = vddq;
  _l4t_decode_opt(opt);

  lv_theme_t *th = lv_theme_get_current();

  // Window.
  lv_obj_t *win = lv_win_create(lv_scr_act(), NULL);
  char title[160];
  s_printf(title, SYMBOL_CHIP " L4T RAM OC - %s", entry_name);
  lv_win_set_title(win, title);
  lv_obj_set_size(win, LV_HOR_RES, LV_VER_RES);
  lv_win_add_btn(win, NULL, SYMBOL_SAVE " Save", _l4t_save_action);
  lv_win_add_btn(win, NULL, SYMBOL_CLOSE " Close", _l4t_close_action);
  l4t_oc.win = win;

  lv_obj_t *colA = _l4t_make_column(th, 20, "Frequency / Voltage");
  lv_obj_t *prev = _l4t_add_field(lv_obj_get_parent(colA), colA, L4T_IDX_FREQ);
  prev = _l4t_add_field(lv_obj_get_parent(colA), prev, L4T_IDX_VDD2);
  prev = _l4t_add_field(lv_obj_get_parent(colA), prev, L4T_IDX_VDDQ);

  lv_obj_t *colB = _l4t_make_column(th, 445, "Options");
  prev = _l4t_add_toggle(lv_obj_get_parent(colB), colB, "AC mode", l4t_oc.ac,
                         L4T_IDX_AC);
  prev = _l4t_add_toggle(lv_obj_get_parent(colB), prev, "HP Mode", l4t_oc.hp,
                         L4T_IDX_HP);

  lv_obj_t *colC = _l4t_make_column(th, 870, "Timings");
  prev = _l4t_add_field(lv_obj_get_parent(colC), colC, L4T_IDX_WRL);
  prev = _l4t_add_field(lv_obj_get_parent(colC), prev, L4T_IDX_TRP);
  prev = _l4t_add_field(lv_obj_get_parent(colC), prev, L4T_IDX_TRAS);
  prev = _l4t_add_field(lv_obj_get_parent(colC), prev, L4T_IDX_TRCD);
  prev = _l4t_add_field(lv_obj_get_parent(colC), prev, L4T_IDX_TRTW);
  prev = _l4t_add_field(lv_obj_get_parent(colC), prev, L4T_IDX_TWTR);
  prev = _l4t_add_field(lv_obj_get_parent(colC), prev, L4T_IDX_TRFC);
  prev = _l4t_add_field(lv_obj_get_parent(colC), prev, L4T_IDX_TRRD);

  l4t_oc.preview = lv_label_create(win, NULL);
  lv_label_set_recolor(l4t_oc.preview, true);
  lv_obj_set_style(l4t_oc.preview, &monospace_text);
  lv_obj_set_pos(l4t_oc.preview, 20, 470);
  _l4t_update_preview();
}
