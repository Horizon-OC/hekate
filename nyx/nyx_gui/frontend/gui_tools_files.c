/*
 * Copyright (c) 2018-2026 CTCaer
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
#include "gui_tools_files.h"
#include <libs/fatfs/ff.h>

#define FM_PATH_SIZE     1024
#define FM_NAME_SIZE     256
#define FM_MAX_ENTRIES   2048
#define FM_COPY_BUF_SIZE 0x100000

#define FM_KB_NEWDIR 0
#define FM_KB_RENAME 1

#define FM_LONGPRESS_MS 400
#define FM_VIEW_MAX      0x10000

typedef struct _fm_entry_t
{
	char  name[FM_NAME_SIZE];
	u64   size;
	u16   date;
	u16   time;
	bool  is_dir;
} fm_entry_t;

typedef struct _file_manager_t
{
	lv_obj_t *list;
	lv_obj_t *path_lbl;
	lv_obj_t *status_lbl;
	lv_obj_t *kb_ta;
	u32       kb_op;
	char      cwd[FM_PATH_SIZE];
	char      sel[FM_NAME_SIZE];
	bool      has_sel;
	bool      sel_is_dir;
	char      clip[FM_PATH_SIZE];
	bool      has_clip;
	bool      clip_cut;
	bool      clip_is_dir;
} file_manager_t;

static file_manager_t fm;
static fm_entry_t *fm_entries = NULL;
static u32 fm_entry_count = 0;
static lv_obj_t *fm_tools_win = NULL;
static u32 fm_press_ms = 0;
static u32 fm_press_idx = 0;
static lv_indev_t *fm_press_indev = NULL;
static bool fm_press_pending = false;
static bool fm_press_fired = false;
static lv_obj_t *fm_view_win = NULL;
static lv_obj_t *fm_view_ta = NULL;
static lv_obj_t *fm_view_kb = NULL;
static char fm_view_path[FM_PATH_SIZE];
static bool fm_view_editing = false;
static bool fm_view_truncated = false;

static void _fm_refresh(void);
static void _fm_update_status(void);
static void _fm_open_tools(void);

static void _fm_join(char *out, const char *dir, const char *name)
{
	if (dir[1] == 0)
		s_printf(out, "/%s", name);
	else
		s_printf(out, "%s/%s", dir, name);
}

static void _fm_go_up(void)
{
	if (fm.cwd[1] == 0)
		return;

	char *p = strrchr(fm.cwd, '/');
	if (p == fm.cwd)
		p[1] = 0;
	else
		*p = 0;
}

static const char *_fm_basename(const char *path)
{
	const char *p = strrchr(path, '/');
	return p ? p + 1 : path;
}

static bool _fm_name_valid(const char *name)
{
	if (!name[0])
		return false;

	if (!strcmp(name, ".") || !strcmp(name, ".."))
		return false;

	for (const char *p = name; *p; p++)
	{
		char c = *p;
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
			return false;
	}

	return true;
}

static void _fm_size_str(char *out, u64 size)
{
	if (size < 1024)
		s_printf(out, "%d B", (u32)size);
	else if (size < 1024 * 1024)
		s_printf(out, "%d KiB", (u32)(size / 1024));
	else if (size < 1024ull * 1024 * 1024)
		s_printf(out, "%d.%d MiB", (u32)(size / (1024 * 1024)), (u32)(((size % (1024 * 1024)) * 10) / (1024 * 1024)));
	else
		s_printf(out, "%d.%d GiB", (u32)(size / (1024ull * 1024 * 1024)), (u32)(((size % (1024ull * 1024 * 1024)) * 10) / (1024ull * 1024 * 1024)));
}

static void _fm_date_str(char *out, u16 date, u16 time)
{
	if (!date)
	{
		strcpy(out, "-");
		return;
	}

	u32 year  = 1980 + ((date >> 9) & 0x7F);
	u32 month = (date >> 5) & 0xF;
	u32 day   = date & 0x1F;
	u32 hour  = (time >> 11) & 0x1F;
	u32 min   = (time >> 5) & 0x3F;

	s_printf(out, "%04d-%02d-%02d %02d:%02d", year, month, day, hour, min);
}

static void _fm_msg(const char *text)
{
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char *mbox_btn_map[] = { "\251", "\222OK", "\251", "" };
	lv_obj_t *mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);
	lv_mbox_set_text(mbox, text);
	lv_mbox_add_btns(mbox, mbox_btn_map, nyx_mbox_action);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 5);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);
}

static int _fm_copy_file(const char *src, const char *dst)
{
	FIL *fs = malloc(sizeof(FIL));
	FIL *fd = malloc(sizeof(FIL));
	u8  *buf = NULL;

	int res = f_open(fs, src, FA_READ | FA_OPEN_EXISTING);
	if (res != FR_OK)
		goto out;

	res = f_open(fd, dst, FA_WRITE | FA_CREATE_ALWAYS);
	if (res != FR_OK)
	{
		f_close(fs);
		goto out;
	}

	buf = malloc(FM_COPY_BUF_SIZE);

	for (;;)
	{
		UINT br = 0, bw = 0;

		res = f_read(fs, buf, FM_COPY_BUF_SIZE, &br);
		if (res != FR_OK || br == 0)
			break;

		res = f_write(fd, buf, br, &bw);
		if (res != FR_OK)
			break;

		if (bw != br)
		{
			res = FR_DISK_ERR;
			break;
		}

		manual_system_maintenance(true);
	}

	f_close(fs);
	f_close(fd);

out:
	if (buf)
		free(buf);
	free(fs);
	free(fd);

	return res;
}

static int _fm_copy_recursive(char *src, char *dst)
{
	FILINFO st;
	int res = f_stat(src, &st);
	if (res != FR_OK)
		return res;

	if (!(st.fattrib & AM_DIR))
		return _fm_copy_file(src, dst);

	res = f_mkdir(dst);
	if (res != FR_OK && res != FR_EXIST)
		return res;

	DIR dir;
	res = f_opendir(&dir, src);
	if (res != FR_OK)
		return res;

	FILINFO *fno = malloc(sizeof(FILINFO));
	u32 slen = strlen(src);
	u32 dlen = strlen(dst);

	for (;;)
	{
		res = f_readdir(&dir, fno);
		if (res != FR_OK || fno->fname[0] == 0)
			break;

		src[slen] = '/';
		strcpy(&src[slen + 1], fno->fname);
		dst[dlen] = '/';
		strcpy(&dst[dlen + 1], fno->fname);

		res = _fm_copy_recursive(src, dst);

		src[slen] = 0;
		dst[dlen] = 0;

		if (res != FR_OK)
			break;
	}

	f_closedir(&dir);
	free(fno);

	return res;
}

static int _fm_delete_recursive(char *path)
{
	FILINFO st;
	if (f_stat(path, &st) != FR_OK)
		return FR_NO_FILE;

	if (!(st.fattrib & AM_DIR))
		return f_unlink(path);

	DIR dir;
	int res = f_opendir(&dir, path);
	if (res != FR_OK)
		return res;

	FILINFO *fno = malloc(sizeof(FILINFO));
	u32 len = strlen(path);

	for (;;)
	{
		res = f_readdir(&dir, fno);
		if (res != FR_OK || fno->fname[0] == 0)
			break;

		path[len] = '/';
		strcpy(&path[len + 1], fno->fname);

		if (fno->fattrib & AM_DIR)
			res = _fm_delete_recursive(path);
		else
			res = f_unlink(path);

		path[len] = 0;

		manual_system_maintenance(true);

		if (res != FR_OK)
			break;
	}

	f_closedir(&dir);
	free(fno);

	if (res == FR_OK)
		res = f_unlink(path);

	return res;
}

static void _fm_update_status(void)
{
	char buf[FM_PATH_SIZE + FM_NAME_SIZE + 64];
	char clip[FM_PATH_SIZE + 16];

	if (fm.has_clip)
		s_printf(clip, "%s %s", fm.clip_cut ? "Move:" : "Copy:", fm.clip);
	else
		strcpy(clip, "Clipboard empty");

	if (fm.has_sel)
		s_printf(buf, "#C7EA46 Selected:# %s%s   #888888 |#   %s", fm.sel, fm.sel_is_dir ? "/" : "", clip);
	else
		s_printf(buf, "No selection   #888888 |#   %s", clip);

	lv_label_set_text(fm.status_lbl, buf);
}

static bool _fm_ext_is(const char *name, const char *ext)
{
	u32 nl = strlen(name);
	u32 el = strlen(ext);

	if (el + 1 >= nl)
		return false;

	if (name[nl - el - 1] != '.')
		return false;

	for (u32 k = 0; k < el; k++)
	{
		char a = name[nl - el + k];
		if (a >= 'A' && a <= 'Z')
			a += 32;
		if (a != ext[k])
			return false;
	}

	return true;
}

static bool _fm_is_text(const char *name)
{
	static const char *exts[] = {
		"txt", "log", "ini", "cfg", "conf", "json", "md", "xml",
		"html", "htm", "css", "js", "c", "h", "cpp", "hpp", "py",
		"sh", "yaml", "yml", "toml", "csv", "nfo", "asm", "mk",
		"bat", "rc", "list", "map", "srt"
	};

	for (u32 k = 0; k < sizeof(exts) / sizeof(exts[0]); k++)
		if (_fm_ext_is(name, exts[k]))
			return true;

	return false;
}

static bool _fm_write_file(const char *path, const char *txt)
{
	if (sd_mount())
		return false;

	FIL *fp = malloc(sizeof(FIL));
	if (f_open(fp, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		free(fp);
		return false;
	}

	u32 len = strlen(txt);
	UINT bw = 0;
	int res = f_write(fp, txt, len, &bw);

	f_close(fp);
	free(fp);

	return res == FR_OK && bw == len;
}

static void _fm_view_exit_edit(void)
{
	if (fm_view_kb)
	{
		lv_obj_del(fm_view_kb);
		fm_view_kb = NULL;
	}

	if (fm_view_ta)
	{
		lv_ta_set_cursor_type(fm_view_ta, LV_CURSOR_NONE);
		lv_obj_set_size(fm_view_ta, LV_HOR_RES * 94 / 100, LV_VER_RES * 80 / 100);
	}

	fm_view_editing = false;
}

static lv_res_t _fm_view_save(lv_obj_t *kb)
{
	bool ok = _fm_write_file(fm_view_path, lv_ta_get_text(fm_view_ta));

	_fm_view_exit_edit();
	_fm_msg(ok ? "#96FF00 File saved.#" : "#FFDD00 Save failed!#");

	return LV_RES_INV;
}

static lv_res_t _fm_view_edit_cancel(lv_obj_t *kb)
{
	_fm_view_exit_edit();

	return LV_RES_INV;
}

static void _fm_view_toggle_edit(void)
{
	if (!fm_view_ta)
		return;

	if (fm_view_editing)
	{
		_fm_view_exit_edit();
		return;
	}

	if (fm_view_truncated)
	{
		_fm_msg("#FFDD00 File too large to edit.#");
		return;
	}

	fm_view_editing = true;

	lv_ta_set_cursor_type(fm_view_ta, LV_CURSOR_LINE);
	lv_obj_set_size(fm_view_ta, LV_HOR_RES * 94 / 100, LV_VER_RES * 48 / 100);

	lv_obj_t *kb = lv_kb_create(lv_scr_act(), NULL);
	lv_kb_set_ta(kb, fm_view_ta);
	lv_kb_set_mode(kb, LV_KB_MODE_TEXT);
	lv_kb_set_cursor_manage(kb, true);
	lv_kb_set_ok_action(kb, _fm_view_save);
	lv_kb_set_hide_action(kb, _fm_view_edit_cancel);
	lv_obj_set_size(kb, LV_HOR_RES, LV_VER_RES * 2 / 5);
	lv_obj_align(kb, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, 0);
	fm_view_kb = kb;
}

static lv_res_t _fm_view_edit_btn(lv_obj_t *btn)
{
	_fm_view_toggle_edit();

	return LV_RES_OK;
}

static void _fm_view_dpad(int dir)
{
	if (!fm_view_editing || !fm_view_ta)
		return;

	switch (dir)
	{
	case NYX_DPAD_LEFT:  lv_ta_cursor_left(fm_view_ta);  break;
	case NYX_DPAD_RIGHT: lv_ta_cursor_right(fm_view_ta); break;
	case NYX_DPAD_UP:    lv_ta_cursor_up(fm_view_ta);    break;
	case NYX_DPAD_DOWN:  lv_ta_cursor_down(fm_view_ta);  break;
	}
}

static lv_res_t _fm_view_close(lv_obj_t *btn)
{
	nyx_jc_y_action = NULL;
	nyx_jc_dpad_action = NULL;

	if (fm_view_kb)
	{
		lv_obj_del(fm_view_kb);
		fm_view_kb = NULL;
	}

	fm_view_ta = NULL;
	fm_view_win = NULL;
	fm_view_editing = false;

	lv_obj_set_hidden(status_bar.bar_bg, false);
	lv_obj_set_hidden(status_bar.line_bottom, false);

	return nyx_win_close_action(btn);
}

static void _fm_view_text(void)
{
	if (sd_mount())
	{
		_fm_msg("#FFDD00 Failed to init SD!#");
		return;
	}

	_fm_join(fm_view_path, fm.cwd, fm.sel);

	FIL *fp = malloc(sizeof(FIL));
	int res = f_open(fp, fm_view_path, FA_READ | FA_OPEN_EXISTING);

	if (res != FR_OK)
	{
		free(fp);
		_fm_msg("#FFDD00 Failed to open file!#");
		return;
	}

	u32 fsize = f_size(fp);
	bool truncated = fsize > FM_VIEW_MAX;
	u32 cap = truncated ? FM_VIEW_MAX : fsize;

	char *text = malloc(cap + 64);
	UINT total = 0, br = 0;

	while (total < cap)
	{
		if (f_read(fp, text + total, cap - total, &br) != FR_OK || br == 0)
			break;
		total += br;
	}

	f_close(fp);
	free(fp);

	u32 w = 0;
	for (u32 r = 0; r < total; r++)
		if (text[r] != '\r')
			text[w++] = text[r];
	total = w;

	text[total] = 0;
	if (truncated)
		strcpy(&text[total], "\n\n[... file truncated ...]");

	fm_view_truncated = truncated;

	char title[FM_NAME_SIZE + 16];
	s_printf(title, SYMBOL_FILE"  %s", fm.sel);

	lv_obj_set_hidden(status_bar.bar_bg, true);
	lv_obj_set_hidden(status_bar.line_bottom, true);

	lv_obj_t *win = nyx_create_standard_window(title, _fm_view_close);
	lv_win_add_btn(win, NULL, SYMBOL_EDIT" Edit", _fm_view_edit_btn);
	fm_view_win = win;
	fm_view_kb = NULL;
	fm_view_editing = false;

	lv_obj_t *ta = lv_ta_create(win, NULL);
	lv_ta_ext_t *ta_ext = lv_obj_get_ext_attr(ta);
	lv_obj_set_style(ta_ext->label, &monospace_text);
	lv_ta_set_cursor_type(ta, LV_CURSOR_NONE);
	lv_ta_set_text(ta, text);
	lv_obj_set_size(ta, LV_HOR_RES * 94 / 100, LV_VER_RES * 80 / 100);
	lv_obj_align(ta, NULL, LV_ALIGN_IN_TOP_MID, 0, LV_DPI / 8);
	fm_view_ta = ta;

	nyx_jc_y_action = _fm_view_toggle_edit;
	nyx_jc_dpad_action = _fm_view_dpad;

	free(text);
}

static lv_res_t _fm_entry_press(lv_obj_t *btn)
{
	fm_press_ms = get_tmr_ms();
	fm_press_idx = lv_obj_get_free_num(btn);
	fm_press_indev = lv_indev_get_act();
	fm_press_pending = true;
	fm_press_fired = false;

	return LV_RES_OK;
}

static void _fm_longpress_task(void *unused)
{
	if (!fm_press_pending)
		return;

	if (fm_press_indev && lv_indev_is_dragging(fm_press_indev))
	{
		fm_press_pending = false;
		return;
	}

	if ((get_tmr_ms() - fm_press_ms) < FM_LONGPRESS_MS)
		return;

	fm_press_pending = false;
	fm_press_fired = true;

	u32 i = fm_press_idx;
	if (i >= fm_entry_count)
		return;

	strcpy(fm.sel, fm_entries[i].name);
	fm.sel_is_dir = fm_entries[i].is_dir;
	fm.has_sel = true;
	_fm_update_status();
	_fm_open_tools();
}

static lv_res_t _fm_entry_action(lv_obj_t *btn)
{
	u32 i = lv_obj_get_free_num(btn);

	fm_press_pending = false;

	if (fm_press_fired)
	{
		fm_press_fired = false;
		return LV_RES_OK;
	}

	if (i >= fm_entry_count)
		return LV_RES_OK;

	if (!fm_entries[i].is_dir)
	{
		strcpy(fm.sel, fm_entries[i].name);
		fm.sel_is_dir = false;
		fm.has_sel = true;
		_fm_update_status();

		if (_fm_is_text(fm.sel))
			_fm_view_text();

		return LV_RES_OK;
	}

	char path[FM_PATH_SIZE];
	_fm_join(path, fm.cwd, fm_entries[i].name);
	strcpy(fm.cwd, path);
	fm.has_sel = false;
	_fm_refresh();

	return LV_RES_INV;
}

static lv_res_t _fm_updir_action(lv_obj_t *btn)
{
	_fm_go_up();
	fm.has_sel = false;
	_fm_refresh();

	return LV_RES_INV;
}

static void _fm_add_entry_row(u32 i)
{
	fm_entry_t *e = &fm_entries[i];
	char buf[FM_NAME_SIZE + 96];
	char date[32];

	_fm_date_str(date, e->date, e->time);

	if (e->is_dir)
	{
		s_printf(buf, SYMBOL_DIRECTORY"  %s      %s", e->name, date);
	}
	else
	{
		char sz[32];
		_fm_size_str(sz, e->size);
		s_printf(buf, SYMBOL_FILE"  %s      %s  %s", e->name, sz, date);
	}

	lv_obj_t *btn = lv_list_add(fm.list, NULL, buf, _fm_entry_action);
	lv_obj_set_free_num(btn, i);
	lv_btn_set_action(btn, LV_BTN_ACTION_PR, _fm_entry_press);
}

static void _fm_refresh(void)
{
	lv_list_clean(fm.list);

	if (fm_entries)
	{
		free(fm_entries);
		fm_entries = NULL;
	}
	fm_entry_count = 0;

	if (sd_mount())
	{
		lv_label_set_text(fm.path_lbl, "#FFDD00 Failed to init SD!#");
		_fm_update_status();
		return;
	}

	fm_entries = malloc(sizeof(fm_entry_t) * FM_MAX_ENTRIES);

	DIR dir;
	FILINFO *fno = malloc(sizeof(FILINFO));

	if (f_opendir(&dir, fm.cwd) == FR_OK)
	{
		while (fm_entry_count < FM_MAX_ENTRIES)
		{
			if (f_readdir(&dir, fno) != FR_OK || fno->fname[0] == 0)
				break;

			fm_entry_t *e = &fm_entries[fm_entry_count];
			strcpy(e->name, fno->fname);
			e->is_dir = (fno->fattrib & AM_DIR) != 0;
			e->size = fno->fsize;
			e->date = fno->fdate;
			e->time = fno->ftime;
			fm_entry_count++;
		}
		f_closedir(&dir);
	}

	free(fno);

	if (fm.cwd[1] != 0)
		lv_list_add(fm.list, NULL, SYMBOL_UP"  ..", _fm_updir_action);

	for (u32 i = 0; i < fm_entry_count; i++)
		if (fm_entries[i].is_dir)
			_fm_add_entry_row(i);

	for (u32 i = 0; i < fm_entry_count; i++)
		if (!fm_entries[i].is_dir)
			_fm_add_entry_row(i);

	lv_label_set_text(fm.path_lbl, fm.cwd);
	_fm_update_status();
}

static lv_res_t _fm_kb_ok_action(lv_obj_t *kb)
{
	char name[FM_NAME_SIZE];
	const char *txt = lv_ta_get_text(fm.kb_ta);
	u32 op = fm.kb_op;

	strncpy(name, txt, sizeof(name) - 1);
	name[sizeof(name) - 1] = 0;

	lv_obj_t *dark_bg = lv_obj_get_parent(kb);
	lv_obj_del(dark_bg);
	lv_obj_set_hidden(status_bar.bar_bg, false);

	if (!_fm_name_valid(name))
	{
		_fm_msg("#FFDD00 Invalid name.#");
		return LV_RES_INV;
	}

	if (!sd_mount())
	{
		int res = FR_OK;
		char *path = malloc(FM_PATH_SIZE);

		if (op == FM_KB_NEWDIR)
		{
			_fm_join(path, fm.cwd, name);
			res = f_mkdir(path);
		}
		else
		{
			char *npath = malloc(FM_PATH_SIZE);
			_fm_join(path, fm.cwd, fm.sel);
			_fm_join(npath, fm.cwd, name);
			res = f_rename(path, npath);
			free(npath);

			if (res == FR_OK)
			{
				strncpy(fm.sel, name, sizeof(fm.sel) - 1);
				fm.sel[sizeof(fm.sel) - 1] = 0;
			}
		}

		free(path);

		if (res != FR_OK)
			_fm_msg("#FFDD00 Operation failed!#");
	}
	else
	{
		_fm_msg("#FFDD00 Failed to init SD!#");
	}

	_fm_refresh();

	return LV_RES_INV;
}

static lv_res_t _fm_kb_close_action(lv_obj_t *kb)
{
	lv_obj_t *dark_bg = lv_obj_get_parent(kb);
	lv_obj_del(dark_bg);
	lv_obj_set_hidden(status_bar.bar_bg, false);

	return LV_RES_INV;
}

static void _fm_open_keyboard(const char *title, const char *prefill, u32 op)
{
	fm.kb_op = op;

	lv_obj_set_hidden(status_bar.bar_bg, true);

	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	lv_obj_t *cont = lv_cont_create(dark_bg, NULL);
	lv_cont_set_fit(cont, false, true);
	lv_obj_set_width(cont, LV_HOR_RES * 7 / 10);

	lv_obj_t *lbl = lv_label_create(cont, NULL);
	lv_label_set_recolor(lbl, true);
	lv_label_set_static_text(lbl, title);
	lv_obj_align(lbl, NULL, LV_ALIGN_IN_TOP_LEFT, LV_DPI / 8, LV_DPI / 8);

	lv_obj_t *ta = lv_ta_create(cont, NULL);
	lv_ta_set_one_line(ta, true);
	lv_ta_set_cursor_type(ta, LV_CURSOR_LINE);
	lv_ta_set_max_length(ta, FM_NAME_SIZE - 1);
	lv_ta_set_text(ta, prefill);
	lv_obj_set_width(ta, LV_HOR_RES * 6 / 10);
	lv_obj_align(ta, lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 8);
	fm.kb_ta = ta;

	lv_obj_t *kb = lv_kb_create(dark_bg, NULL);
	lv_kb_set_ta(kb, ta);
	lv_kb_set_mode(kb, LV_KB_MODE_TEXT);
	lv_kb_set_cursor_manage(kb, true);
	lv_kb_set_ok_action(kb, _fm_kb_ok_action);
	lv_kb_set_hide_action(kb, _fm_kb_close_action);
	lv_obj_set_size(kb, LV_HOR_RES, LV_VER_RES * 2 / 5);
	lv_obj_align(kb, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, 0);

	lv_obj_align(cont, kb, LV_ALIGN_OUT_TOP_MID, 0, -LV_DPI / 4);
	lv_obj_set_top(cont, true);
}

static lv_res_t _fm_open_action(lv_obj_t *btn)
{
	if (!fm.has_sel)
	{
		_fm_msg("#FFDD00 Nothing selected.#");
		return LV_RES_OK;
	}

	if (!fm.sel_is_dir)
	{
		if (_fm_is_text(fm.sel))
			_fm_view_text();
		else
			_fm_msg("#FFDD00 Not a folder or text file.#");

		return LV_RES_OK;
	}

	char path[FM_PATH_SIZE];
	_fm_join(path, fm.cwd, fm.sel);
	strcpy(fm.cwd, path);
	fm.has_sel = false;
	_fm_refresh();

	return LV_RES_OK;
}

static lv_res_t _fm_newfolder_action(lv_obj_t *btn)
{
	_fm_open_keyboard(SYMBOL_DIRECTORY"  Enter new folder name:", "", FM_KB_NEWDIR);

	return LV_RES_OK;
}

static lv_res_t _fm_rename_action(lv_obj_t *btn)
{
	if (!fm.has_sel)
	{
		_fm_msg("#FFDD00 Nothing selected.#");
		return LV_RES_OK;
	}

	_fm_open_keyboard(SYMBOL_EDIT"  Enter new name:", fm.sel, FM_KB_RENAME);

	return LV_RES_OK;
}

static lv_res_t _fm_copy_action(lv_obj_t *btn)
{
	if (!fm.has_sel)
	{
		_fm_msg("#FFDD00 Nothing selected.#");
		return LV_RES_OK;
	}

	_fm_join(fm.clip, fm.cwd, fm.sel);
	fm.clip_cut = false;
	fm.clip_is_dir = fm.sel_is_dir;
	fm.has_clip = true;
	_fm_update_status();

	return LV_RES_OK;
}

static lv_res_t _fm_cut_action(lv_obj_t *btn)
{
	if (!fm.has_sel)
	{
		_fm_msg("#FFDD00 Nothing selected.#");
		return LV_RES_OK;
	}

	_fm_join(fm.clip, fm.cwd, fm.sel);
	fm.clip_cut = true;
	fm.clip_is_dir = fm.sel_is_dir;
	fm.has_clip = true;
	_fm_update_status();

	return LV_RES_OK;
}

static lv_res_t _fm_paste_action(lv_obj_t *btn)
{
	if (!fm.has_clip)
	{
		_fm_msg("#FFDD00 Clipboard is empty.#");
		return LV_RES_OK;
	}

	char *src = malloc(FM_PATH_SIZE);
	char *dst = malloc(FM_PATH_SIZE);
	strcpy(src, fm.clip);
	_fm_join(dst, fm.cwd, _fm_basename(fm.clip));

	if (!strcmp(src, dst))
	{
		free(src);
		free(dst);
		_fm_msg("#FFDD00 Source and destination are the same.#");
		return LV_RES_OK;
	}

	u32 srclen = strlen(src);
	if (fm.clip_is_dir && !strncmp(dst, src, srclen) && dst[srclen] == '/')
	{
		free(src);
		free(dst);
		_fm_msg("#FFDD00 Cannot paste a folder into itself.#");
		return LV_RES_OK;
	}

	if (sd_mount())
	{
		free(src);
		free(dst);
		_fm_msg("#FFDD00 Failed to init SD!#");
		return LV_RES_OK;
	}

	FILINFO st;
	if (f_stat(dst, &st) == FR_OK)
	{
		free(src);
		free(dst);
		_fm_msg("#FFDD00 Target already exists.#");
		return LV_RES_OK;
	}

	int res;
	if (fm.clip_cut)
	{
		res = f_rename(src, dst);
		if (res != FR_OK)
		{
			res = _fm_copy_recursive(src, dst);
			if (res == FR_OK)
				res = _fm_delete_recursive(src);
		}
	}
	else
	{
		res = _fm_copy_recursive(src, dst);
	}

	free(src);
	free(dst);

	if (res != FR_OK)
		_fm_msg("#FFDD00 Operation failed!#");
	else if (fm.clip_cut)
		fm.has_clip = false;

	fm.has_sel = false;
	_fm_refresh();

	return LV_RES_OK;
}

static lv_res_t _fm_delete_confirm_action(lv_obj_t *btns, const char *txt)
{
	lv_obj_t *mbox = lv_mbox_get_from_btn(btns);
	lv_obj_t *dark_bg = lv_obj_get_parent(mbox);

	bool do_delete = !strcmp(txt, "Delete");
	lv_obj_del(dark_bg);

	if (do_delete && fm.has_sel)
	{
		if (!sd_mount())
		{
			char *path = malloc(FM_PATH_SIZE);
			_fm_join(path, fm.cwd, fm.sel);

			int res;
			if (fm.sel_is_dir)
				res = _fm_delete_recursive(path);
			else
				res = f_unlink(path);

			free(path);

			if (res != FR_OK)
				_fm_msg("#FFDD00 Delete failed!#");
		}
		else
		{
			_fm_msg("#FFDD00 Failed to init SD!#");
		}

		fm.has_sel = false;
		_fm_refresh();
	}

	return LV_RES_INV;
}

static lv_res_t _fm_delete_action(lv_obj_t *btn)
{
	if (!fm.has_sel)
	{
		_fm_msg("#FFDD00 Nothing selected.#");
		return LV_RES_OK;
	}

	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char *mbox_btn_map[] = { "\222Delete", "\222Cancel", "" };
	lv_obj_t *mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 5);

	char buf[FM_NAME_SIZE + 96];
	s_printf(buf, "Delete #FF8000 %s#%s?\n#FFDD00 This cannot be undone.#", fm.sel, fm.sel_is_dir ? " and all of its contents" : "");
	lv_mbox_set_text(mbox, buf);

	lv_mbox_add_btns(mbox, mbox_btn_map, _fm_delete_confirm_action);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);

	return LV_RES_OK;
}

static lv_res_t _fm_refresh_action(lv_obj_t *btn)
{
	fm.has_sel = false;
	_fm_refresh();

	return LV_RES_OK;
}

static void _fm_close_tools(void)
{
	if (fm_tools_win)
	{
		lv_obj_del(fm_tools_win);
		fm_tools_win = NULL;
		close_btn = NULL;
	}
}

static lv_res_t _fm_tw_select(lv_obj_t *btn)
{
	bool had = fm.has_sel;
	_fm_close_tools();

	if (!had)
		_fm_msg("#FFDD00 Nothing selected.#");
	else
		_fm_update_status();

	return LV_RES_INV;
}

static lv_res_t _fm_tw_open(lv_obj_t *btn)      { _fm_close_tools(); _fm_open_action(NULL);      return LV_RES_INV; }
static lv_res_t _fm_tw_newfolder(lv_obj_t *btn) { _fm_close_tools(); _fm_newfolder_action(NULL); return LV_RES_INV; }
static lv_res_t _fm_tw_copy(lv_obj_t *btn)      { _fm_close_tools(); _fm_copy_action(NULL);      return LV_RES_INV; }
static lv_res_t _fm_tw_cut(lv_obj_t *btn)       { _fm_close_tools(); _fm_cut_action(NULL);       return LV_RES_INV; }
static lv_res_t _fm_tw_paste(lv_obj_t *btn)     { _fm_close_tools(); _fm_paste_action(NULL);     return LV_RES_INV; }
static lv_res_t _fm_tw_rename(lv_obj_t *btn)    { _fm_close_tools(); _fm_rename_action(NULL);    return LV_RES_INV; }
static lv_res_t _fm_tw_delete(lv_obj_t *btn)    { _fm_close_tools(); _fm_delete_action(NULL);    return LV_RES_INV; }
static lv_res_t _fm_tw_refresh(lv_obj_t *btn)   { _fm_close_tools(); _fm_refresh_action(NULL);   return LV_RES_INV; }

static lv_res_t _fm_tools_close(lv_obj_t *btn)
{
	fm_tools_win = NULL;
	close_btn = NULL;

	return nyx_win_close_action(btn);
}

static void _fm_tool_btn(lv_obj_t *win, lv_obj_t *anchor, u32 idx, const char *txt, lv_action_t action)
{
	lv_obj_t *btn = lv_btn_create(win, NULL);
	if (hekate_bg)
	{
		lv_btn_set_style(btn, LV_BTN_STYLE_REL, &btn_transp_rel);
		lv_btn_set_style(btn, LV_BTN_STYLE_PR, &btn_transp_pr);
	}
	lv_obj_set_size(btn, LV_HOR_RES * 26 / 100, LV_DPI * 7 / 8);

	lv_obj_t *lbl = lv_label_create(btn, NULL);
	lv_label_set_static_text(lbl, txt);

	u32 col = idx % 3;
	u32 row = idx / 3;
	lv_obj_align(btn, anchor, LV_ALIGN_OUT_BOTTOM_LEFT,
		col * (LV_HOR_RES * 31 / 100),
		LV_DPI / 2 + row * (LV_DPI * 9 / 8));

	lv_btn_set_action(btn, LV_BTN_ACTION_CLICK, action);
}

static void _fm_open_tools(void)
{
	lv_obj_t *win = nyx_create_standard_window(SYMBOL_LIST"  File Tools", _fm_tools_close);
	fm_tools_win = win;

	lv_obj_t *lbl = lv_label_create(win, NULL);
	lv_label_set_recolor(lbl, true);
	lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
	lv_obj_set_width(lbl, LV_HOR_RES * 9 / 10);

	char buf[FM_PATH_SIZE + FM_NAME_SIZE + 96];
	if (fm.has_sel)
		s_printf(buf, "#C7EA46 Selected:# %s%s\n#888888 Folder:# %s", fm.sel, fm.sel_is_dir ? "/" : "", fm.cwd);
	else
		s_printf(buf, "#FFDD00 No file selected.# Long-press an item to pick one.\n#888888 Folder:# %s", fm.cwd);
	lv_label_set_text(lbl, buf);
	lv_obj_align(lbl, NULL, LV_ALIGN_IN_TOP_LEFT, LV_DPI / 2, LV_DPI / 4);

	_fm_tool_btn(win, lbl, 0, SYMBOL_OK"  Select",          _fm_tw_select);
	_fm_tool_btn(win, lbl, 1, SYMBOL_RIGHT"  Open",         _fm_tw_open);
	_fm_tool_btn(win, lbl, 2, SYMBOL_DIRECTORY"  New Folder", _fm_tw_newfolder);
	_fm_tool_btn(win, lbl, 3, SYMBOL_COPY"  Copy",          _fm_tw_copy);
	_fm_tool_btn(win, lbl, 4, SYMBOL_EDIT"  Cut",           _fm_tw_cut);
	_fm_tool_btn(win, lbl, 5, SYMBOL_SAVE"  Paste",         _fm_tw_paste);
	_fm_tool_btn(win, lbl, 6, SYMBOL_KEYBOARD"  Rename",    _fm_tw_rename);
	_fm_tool_btn(win, lbl, 7, SYMBOL_TRASH"  Delete",       _fm_tw_delete);
	_fm_tool_btn(win, lbl, 8, SYMBOL_REFRESH"  Refresh",    _fm_tw_refresh);
}

static lv_res_t _fm_tools_action(lv_obj_t *btn)
{
	_fm_open_tools();

	return LV_RES_OK;
}

void create_tab_files(lv_theme_t *th, lv_obj_t *parent)
{
	fm.cwd[0] = '/';
	fm.cwd[1] = 0;
	fm.has_sel = false;
	fm.has_clip = false;

	lv_obj_t *path_lbl = lv_label_create(parent, NULL);
	lv_label_set_long_mode(path_lbl, LV_LABEL_LONG_DOT);
	lv_label_set_recolor(path_lbl, true);
	lv_obj_set_width(path_lbl, LV_HOR_RES * 60 / 100);
	lv_label_set_text(path_lbl, "/");
	lv_obj_set_pos(path_lbl, LV_DPI / 4, LV_DPI / 4);
	fm.path_lbl = path_lbl;

	lv_obj_t *hint = lv_label_create(parent, NULL);
	lv_label_set_recolor(hint, true);
	lv_label_set_static_text(hint, "#888888 Tap a folder or text file to open "SYMBOL_DOT" Long-press an item for tools#");
	lv_obj_align(hint, path_lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 12);

	lv_obj_t *list = lv_list_create(parent, NULL);
	lv_obj_set_size(list, LV_HOR_RES * 76 / 100, LV_VER_RES * 60 / 100);
	lv_obj_set_pos(list, LV_DPI / 4, LV_DPI);
	lv_list_set_single_mode(list, true);
	fm.list = list;

	lv_obj_t *toolbtn = lv_btn_create(parent, NULL);
	if (hekate_bg)
	{
		lv_btn_set_style(toolbtn, LV_BTN_STYLE_REL, &btn_transp_rel);
		lv_btn_set_style(toolbtn, LV_BTN_STYLE_PR, &btn_transp_pr);
	}
	lv_obj_set_size(toolbtn, LV_HOR_RES * 16 / 100, LV_VER_RES * 60 / 100);
	lv_obj_align(toolbtn, list, LV_ALIGN_OUT_RIGHT_MID, LV_DPI / 2, 0);

	lv_obj_t *toolbtn_lbl = lv_label_create(toolbtn, NULL);
	lv_label_set_align(toolbtn_lbl, LV_LABEL_ALIGN_CENTER);
	lv_label_set_static_text(toolbtn_lbl, SYMBOL_LIST"\n\nFile\nTools\n\n"SYMBOL_RIGHT);
	lv_btn_set_action(toolbtn, LV_BTN_ACTION_CLICK, _fm_tools_action);

	lv_obj_t *status_lbl = lv_label_create(parent, NULL);
	lv_label_set_long_mode(status_lbl, LV_LABEL_LONG_DOT);
	lv_label_set_recolor(status_lbl, true);
	lv_obj_set_width(status_lbl, LV_HOR_RES * 90 / 100);
	lv_label_set_text(status_lbl, "No selection");
	lv_obj_align(status_lbl, list, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 6);
	fm.status_lbl = status_lbl;

	lv_task_create(_fm_longpress_task, 30, LV_TASK_PRIO_MID, NULL);

	_fm_refresh();
}
