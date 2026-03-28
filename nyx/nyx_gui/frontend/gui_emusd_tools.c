#include "gui_emusd_tools.h"
#include "fe_emusd_tools.h"
#include "gui_tools_partition_manager.h"
#include "gui.h"
#include <fatfs_cfg.h>
#include <libs/fatfs/ff.h>
#include <libs/lvgl/lv_core/lv_obj.h>
#include <libs/lvgl/lv_objx/lv_btnm.h>
#include <libs/lvgl/lv_objx/lv_list.h>
#include <libs/lvgl/lv_objx/lv_mbox.h>
#include <storage/boot_storage.h>
#include "fe_emusd_tools.h" 
#include <stdlib.h>
#include <storage/emmc.h>
#include <storage/mbr_gpt.h>
#include <storage/sd.h>
#include <storage/sdmmc.h>
#include <string.h>
#include <utils/sprintf.h>
#include <mem/heap.h>
#include <utils/types.h>
#include <gfx_utils.h>

typedef struct _gpt_ctxt_t{
	u32 sector[3];
	u32 sector_mbr[3];
	u32 part_idx[3];
	u32 size[3];
	char name[3][36];
	u8 cnt;
	u8 valid_parts[3];
	u8 valid_cnt;
} gpt_ctxt_t;

static gpt_ctxt_t gpt_ctxt;
static u8 emusd_drive;
static lv_obj_t *emusd_parent_cont;
static lv_res_t (*emusd_tools)(lv_obj_t *parent);

static void _get_emusd_parts(gpt_t *gpt){
	u8 cnt = 0;
	s32 gpt_idx = gpt_get_part_by_name(gpt, "emusd_mbr", -1);
	while(gpt_idx != -1 && cnt < 3){
		u32 sector_mbr = gpt->entries[gpt_idx].lba_start;
		gpt_idx = gpt_get_part_by_name(gpt, "emusd", gpt_idx);
		if(gpt_idx == -1){
			// should never happen
			continue;
		}

		u32 sector = gpt->entries[gpt_idx].lba_start;
		u32 size = gpt->entries[gpt_idx].lba_end - sector + 1;

		gpt_ctxt.sector_mbr[cnt] = sector_mbr;
		gpt_ctxt.sector[cnt] = sector;
		gpt_ctxt.part_idx[cnt] = gpt_idx;
		gpt_ctxt.size[cnt] = size;
		wctombs(gpt->entries[gpt_idx].name, gpt_ctxt.name[cnt], 36);

		cnt++;

		gpt_idx = gpt_get_part_by_name(gpt, "emusd", gpt_idx);
	}
	gpt_ctxt.cnt = cnt;
}

static lv_res_t _close_mbox_and_reload(lv_obj_t *btns, const char *txt){
	lv_obj_clean(emusd_parent_cont);
	nyx_mbox_action(btns, txt);

	(*emusd_tools)(emusd_parent_cont);

	return LV_RES_INV;
}

static lv_res_t _emusd_create_action(lv_obj_t *btns, const char *txt){
	int idx = lv_btnm_get_pressed(btns);
	nyx_mbox_action(btns, txt);

	if(idx < gpt_ctxt.cnt) {
		lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
		lv_obj_set_style(dark_bg, &mbox_darken);
		lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

		static const char *mbox_btns_map[] = { "\251", "\222OK", "\251", "" };
		lv_obj_t * mbox = lv_mbox_create(dark_bg, NULL);
		lv_mbox_set_recolor_text(mbox, true);
		lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6);

		lv_mbox_set_text(mbox,
			"#FF8000 emuSD creation tool#\n\n"
			"#00DDFF Status:# Creating partition based emuSD...");
		lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
		manual_system_maintenance(true);

		if(emusd_drive == DRIVE_SD){
			sd_initialize(false);
		}else{
			emmc_initialize(false);
		}
		boot_storage_mount();
		int res = create_emusd(gpt_ctxt.part_idx[idx], gpt_ctxt.sector_mbr[idx], gpt_ctxt.sector[idx], gpt_ctxt.size[idx], gpt_ctxt.name[idx], emusd_drive);
		boot_storage_unmount();
		if(emusd_drive == DRIVE_SD){
			sd_end(false);
		}else{
			emmc_end(false);
		}
		if(res == FR_OK){
			lv_mbox_set_text(mbox,
			"#FF8000 emuSD creation tool#\n\n"
			"#00DDFF Status:# Done!");
		}else{
			lv_mbox_set_text(mbox,
			"#FF8000 emuSD creation tool#\n\n"
			"#00DDFF Status:# Failed to create emuSD!");
		}

		lv_mbox_add_btns(mbox, mbox_btns_map, _close_mbox_and_reload);

		lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
		lv_obj_set_top(mbox, true);
	}

	return LV_RES_INV;
}

static lv_res_t _emusd_format_action(lv_obj_t *btns, const char *txt){
	u32 idx = lv_btnm_get_pressed(btns);

	nyx_mbox_action(btns, txt);

	if(!idx){
		create_window_partition_manager(btns, emusd_drive);
	}

	return LV_RES_INV;
}

typedef struct _slider_ctx_t{
	lv_obj_t *label;
	u32 emu_size;  // mb
} slider_ctx_t;

static slider_ctx_t slider_ctx;

static lv_res_t _action_slider_emusd_file(lv_obj_t *slider){
	u32 slider_val = lv_slider_get_value(slider);
	u32 size = slider_val <<= 10;

	slider_ctx.emu_size = size;

	char txt_buf[0x20];
	s_printf(txt_buf, "#FF00D6 %d GiB#", size >> 10);

	lv_label_set_text(slider_ctx.label, txt_buf);

	return LV_RES_OK;
}

static lv_res_t _create_emusd_file_based_action(lv_obj_t *btns, const char *txt){
	int idx = lv_btnm_get_pressed(btns);
	nyx_mbox_action(btns, txt);

	if(!idx && slider_ctx.emu_size) {
		lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
		lv_obj_set_style(dark_bg, &mbox_darken);
		lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

		static const char *mbox_btns_map[] = { "\251", "\222OK", "\251", "" };
		lv_obj_t * mbox = lv_mbox_create(dark_bg, NULL);
		lv_mbox_set_recolor_text(mbox, true);
		lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6);

		lv_mbox_set_text(mbox,
			"#FF8000 emuSD creation tool#\n\n"
			"#00DDFF Status:# Creating file based emuSD...");
		lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
		manual_system_maintenance(true);

		int res = 0;
		if(emusd_drive == DRIVE_SD){
			if(!sd_mount()){
				res = FR_DISK_ERR;
			}
		}else{
			if(!emmc_mount()){
				res = FR_DISK_ERR;
			}
		}
		boot_storage_mount();
		if(!res){
			res = create_emusd_file(slider_ctx.emu_size << 11, emusd_drive);
		}
		boot_storage_unmount();
		if(emusd_drive == DRIVE_SD){
			sd_unmount();
		}else{
			emmc_unmount();
		}

		if(res){
			lv_mbox_set_text(mbox,
			"#FF8000 emuSD creation tool#\n\n"
			"#00DDFF Status:# Done!");
		}else{
			lv_mbox_set_text(mbox,
			"#FF8000 emuSD creation tool#\n\n"
			"#00DDFF Status:# Failed to create emuSD!");
		}

		lv_mbox_add_btns(mbox, mbox_btns_map, _close_mbox_and_reload);

		lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
		lv_obj_set_top(mbox, true);
	}

	return LV_RES_INV;
}

static void _create_mbox_emusd_file_based(){
	char *txt_buf = malloc(SZ_4K);

	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	lv_obj_t * mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6);

	static const char *mbox_btns[] = { "\222Continue", "\222Cancel", "" };
	static const char *mbox_btns_ok[] = { "\251", "\222OK", "\251", "" };

	lv_mbox_set_text(mbox, "#FF8000 emuSD creation tool#\n\n"
	                       "#C7EA46 Select emuSD size!#\n\n"
	                       "#00DDFF Status:# Checking for available free space...");
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	manual_system_maintenance(true);

	FATFS *fs;
	if(emusd_drive == DRIVE_SD){
		sd_mount();
		fs = &sd_fs;
		f_getfree("sd:", &fs->free_clst, NULL);
	}else{
		emmc_mount();
		fs = &emmc_fs;
		f_getfree("emmc:", &fs->free_clst, NULL);
	}
	emmc_initialize(false);
	if(emusd_drive == DRIVE_SD){
		sd_unmount();
	}else{
		emmc_unmount();
	}

	// leave 1gb free
	u32 available = fs->free_clst * fs->csize;
	available >>= 11;
	available = available >= (2 * 1024) ? available - (1 * 1024) : 0;

	if(available == 0){
		s_printf(txt_buf, "#FF8000 emuSD creation tool#\n\n"
					               "#FFDD00 Note:# Not enough free space on %s", emusd_drive == DRIVE_SD ? "SD" : "eMMC");
		lv_mbox_set_text(mbox, txt_buf);

	}else{
		lv_mbox_set_text(mbox, "#FF8000 emuSD creation tool#\n\n"
		                             "#C7EA46 Select emuSD size!#");
	}
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);

	slider_ctx.emu_size = 1024;

	if(available){

		lv_coord_t pad = lv_mbox_get_style(mbox, LV_MBOX_STYLE_BG)->body.padding.hor;
		lv_coord_t w = lv_obj_get_width(mbox) - 2 * pad - 2 * LV_DPI;

		// Set eMUMMC bar styles.
		static lv_style_t bar_emu_bg, bar_emu_ind, bar_emu_btn;
		lv_style_copy(&bar_emu_bg, lv_theme_get_current()->bar.bg);
		bar_emu_bg.body.main_color = LV_COLOR_HEX(0x96007e);
		bar_emu_bg.body.grad_color = bar_emu_bg.body.main_color;
		lv_style_copy(&bar_emu_ind, lv_theme_get_current()->bar.indic);
		bar_emu_ind.body.main_color = LV_COLOR_HEX(0xff00d6);
		bar_emu_ind.body.grad_color = bar_emu_ind.body.main_color;
		lv_style_copy(&bar_emu_btn, lv_theme_get_current()->slider.knob);
		bar_emu_btn.body.main_color = LV_COLOR_HEX(0xc700a7);
		bar_emu_btn.body.grad_color = bar_emu_btn.body.main_color;

		lv_obj_t *slider_cont = lv_cont_create(mbox, NULL);
		lv_cont_set_fit(slider_cont, false, true);
		lv_cont_set_style(slider_cont, &lv_style_transp);
		lv_obj_set_width(slider_cont, lv_obj_get_width(mbox));

		lv_obj_t *slider = lv_slider_create(slider_cont, NULL);
		lv_obj_set_size(slider, w, LV_DPI / 3);
		lv_slider_set_range(slider, 1, available >> 10);
		lv_slider_set_value(slider, slider_ctx.emu_size >> 10);
		lv_slider_set_style(slider, LV_SLIDER_STYLE_BG, &bar_emu_bg);
		lv_slider_set_style(slider, LV_SLIDER_STYLE_INDIC, &bar_emu_ind);
		lv_slider_set_style(slider, LV_SLIDER_STYLE_KNOB, &bar_emu_btn);
		lv_slider_set_action(slider, _action_slider_emusd_file);
		lv_obj_align(slider, slider_cont, LV_ALIGN_CENTER, - (LV_DPI / 2), 0);

		lv_obj_t *label = lv_label_create(slider_cont, NULL);
		lv_label_set_recolor(label, true);
		s_printf(txt_buf, "#FF00D6 %d GiB#", slider_ctx.emu_size >> 10);

		lv_label_set_text(label, txt_buf);
		lv_obj_align(label, slider, LV_ALIGN_OUT_RIGHT_MID, LV_DPI * 2 / 5, 0);

		slider_ctx.label = label;
	}

	if(available){
		lv_mbox_add_btns(mbox, mbox_btns, _create_emusd_file_based_action);
	}else{
		lv_mbox_add_btns(mbox, mbox_btns_ok, _create_emusd_file_based_action);
	}

	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
}

static lv_res_t _create_emusd_select_file_type_action(lv_obj_t *btns, const char *txt){
	int btn_idx = lv_btnm_get_pressed(btns);

	nyx_mbox_action(btns, txt);

	switch(btn_idx){
	case 0:
		emusd_drive = DRIVE_SD;
		_create_mbox_emusd_file_based();
		break;
	case 1:
		emusd_drive = DRIVE_EMMC;
		_create_mbox_emusd_file_based();
		break;
	case 2:
		break;
	}

	return LV_RES_INV;
}

static void _create_mbox_emusd_select_file_type(){
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char * mbox_btn_map[] = { "\222SD file", "\222eMMC file", "\222Cancel", "" };
	lv_obj_t * mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6);

	lv_mbox_set_text(mbox,
		"Welcome to #C7EA46 emuSD# creation tool!\n\n"
		"Please choose what type of file based emuSD to create.");

	lv_mbox_add_btns(mbox, mbox_btn_map, _create_emusd_select_file_type_action);

	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);
}

static void _create_mbox_emusd_raw() {
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	lv_obj_t * mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6);

	static char mbox_part_strs[5][0x10];
	static char *mbox_btn_parts[] = {mbox_part_strs[0], mbox_part_strs[1], mbox_part_strs[2], mbox_part_strs[3], mbox_part_strs[4]};
	static const char *mbox_btn_format[] = {"\222Continue", "\222Cancel", ""};

	char *txt_buf = (char*)malloc(SZ_16K);

	if(emusd_drive == DRIVE_EMMC) {
		emmc_initialize(false);
		emmc_set_partition(EMMC_GPP);
	}else{
		sd_initialize(false);
	}

	sdmmc_storage_t *storage = emusd_drive == DRIVE_SD ? &sd_storage : &emmc_storage;

	gpt_t *gpt = NULL;
	mbr_t mbr = {0};
	u8 cnt = 0;

	sdmmc_storage_read(storage, 0, 1, &mbr);

	if(mbr_has_gpt(&mbr)){
		gpt = zalloc(sizeof(*gpt));
		sdmmc_storage_read(storage, 1, sizeof(*gpt) / 0x200, gpt);

		_get_emusd_parts(gpt);

		cnt = gpt_ctxt.cnt;
	}

	if(cnt){
		s_printf(txt_buf, 
		         "#C7EA46 Found applicable %s partition(s)!#\n"
				 "#FF8000 Choose a partition to continue:#\n\n", emusd_drive == DRIVE_SD ? "SD" : "eMMC");
		for(u32 i = 0; i < cnt; i++){
			char name[36];
			u8 idx = gpt_ctxt.part_idx[i];
			u32 size = gpt->entries[idx].lba_end - gpt->entries[idx].lba_start + 1;
			wctombs(gpt->entries[idx].name, name, 36);
			s_printf(txt_buf + strlen(txt_buf), "Part %d (%s): Start: 0x%x, Size: 0x%x\n", idx, name, gpt_ctxt.sector[i], size);
			s_printf(mbox_btn_parts[i], "\222Part %d", gpt_ctxt.part_idx[i]);

		}
		s_printf(mbox_btn_parts[cnt], "\222Cancel");
		mbox_btn_parts[cnt + 1][0] = '\0';

		lv_mbox_add_btns(mbox, (const char **)mbox_btn_parts, _emusd_create_action);
	}else{
		s_printf(txt_buf, "#FFDD00 Failed to find applicable partition!#\n\n"
		                  "#FF8000 Do you want to partition the %s?#\n"
						  "#FF8000 (You will be asked on how to proceed)#", emusd_drive == DRIVE_SD ? "SD Card" : "eMMC");
		lv_mbox_add_btns(mbox, mbox_btn_format, _emusd_format_action);
	}

	lv_mbox_set_text(mbox, txt_buf);
	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);

	if(emusd_drive == DRIVE_EMMC){
		emmc_end();
	}else{
		sd_end();
	}

	free(txt_buf);
	free(gpt);
}

static lv_res_t _create_emusd_select_raw_type_action(lv_obj_t *btns, const char *txt){
	int btn_idx = lv_btnm_get_pressed(btns);

	nyx_mbox_action(btns, txt);

	switch(btn_idx){
	case 0:
		emusd_drive = DRIVE_SD;
		_create_mbox_emusd_raw();
		break;
	case 1:
		emusd_drive = DRIVE_EMMC;
		_create_mbox_emusd_raw();
		break;
	case 2:
		break;
	}

	return LV_RES_INV;
}

static void _create_mbox_emusd_select_raw_type(){
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char * mbox_btn_map[] = { "\222SD partition", "\222eMMC partition", "\222Cancel", "" };
	lv_obj_t * mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6);

	lv_mbox_set_text(mbox,
		"Welcome to #C7EA46 emuSD# creation tool!\n\n"
		"Please choose what type of partition based emuSD to create.");

	lv_mbox_add_btns(mbox, mbox_btn_map, _create_emusd_select_raw_type_action);

	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);
}

static lv_res_t _create_emusd_select_type_action(lv_obj_t *btns, const char *txt){
	int btn_idx = lv_btnm_get_pressed(btns);

	nyx_mbox_action(btns, txt);

	switch(btn_idx){
	case 0:
		_create_mbox_emusd_select_file_type();
		break;
	case 1:
		_create_mbox_emusd_select_raw_type();
		break;
	case 2:
		break;
	}

	return LV_RES_INV;
}

static lv_res_t _create_mbox_emusd_create(lv_obj_t *btn){
	if(!nyx_emmc_check_battery_enough()){
		return LV_RES_OK;
	}

	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char * mbox_btn_map[] = { "\222File based", "\222Partition Based", "\222Cancel", "" };
	lv_obj_t * mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 6);

	lv_mbox_set_text(mbox,
		"Welcome to #C7EA46 emuSD# creation tool!\n\n"
		"Please choose what type of emuSD you want to create.\n"
		"#FF8000 File based# is saved as files in\nthe SD or eMMC FAT partition.\n"
		"#FF8000 Partition based# is saved as raw image in an\navailable SD or eMMC partition.");

	lv_mbox_add_btns(mbox, mbox_btn_map, _create_emusd_select_type_action);

	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);

	return LV_RES_OK;
}

typedef struct _emusd_images_t
{
	dirlist_t *dirlist;

	u32 emmc_part_sector[3];
	u32 emmc_part_end[3];
	u32 emmc_part_idx[3];
	char emmc_part_path[3][128];

	u32 part_sector[3];
	u32 part_end[3];
	u32 part_idx[3];
	char part_path[3][128];
	lv_obj_t *win;
} emusd_images_t;

static emusd_images_t *emusd_img;

static lv_res_t _save_emusd_cfg_mbox_action(lv_obj_t *btns, const char *txt)
{
	// Free components, delete main emuMMC and popup windows and relaunch main emuMMC window.
	free(emusd_img->dirlist);
	lv_obj_del(emusd_img->win);
	free(emusd_img);

	_close_mbox_and_reload(btns, txt);
	return LV_RES_INV;
}

static void _create_emusd_saved_mbox()
{
	lv_obj_t *dark_bg = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_style(dark_bg, &mbox_darken);
	lv_obj_set_size(dark_bg, LV_HOR_RES, LV_VER_RES);

	static const char *mbox_btn_map[] = { "\251", "OK", "\251", "" };
	lv_obj_t * mbox = lv_mbox_create(dark_bg, NULL);
	lv_mbox_set_recolor_text(mbox, true);
	lv_obj_set_width(mbox, LV_HOR_RES / 9 * 4);

	lv_mbox_set_text(mbox,
		"#FF8000 emuSD Configuration#\n\n"
		"#96FF00 The emuSD configuration#\n#96FF00 was saved!#");

	lv_mbox_add_btns(mbox, mbox_btn_map, _save_emusd_cfg_mbox_action);

	lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_top(mbox, true);
}

static lv_res_t _save_disable_emusd_cfg_action(lv_obj_t * btn)
{
	save_emusd_cfg(0, 0, NULL, 0);
	_create_emusd_saved_mbox();
	sd_unmount();
	boot_storage_unmount();

	return LV_RES_INV;
}

static lv_res_t _save_file_emusd_emmc_cfg_action(lv_obj_t *btn)
{
	const char *btn_txt = lv_list_get_btn_text(btn);
	gfx_printf("save emu %s\n", btn_txt);
	save_emusd_cfg(0, 0, btn_txt, DRIVE_EMMC);
	_create_emusd_saved_mbox();
	sd_unmount();
	boot_storage_unmount();

	return LV_RES_INV;
}

static lv_res_t _save_file_emusd_cfg_action(lv_obj_t *btn)
{
	const char *btn_txt = lv_list_get_btn_text(btn);
	gfx_printf("save sd %s\n", btn_txt);
	save_emusd_cfg(0, 0, btn_txt, DRIVE_SD);
	_create_emusd_saved_mbox();
	sd_unmount();
	boot_storage_unmount();

	return LV_RES_INV;
}

static lv_res_t _save_raw_emusd_cfg_action(lv_obj_t * btn)
{
	lv_btn_ext_t *ext = lv_obj_get_ext_attr(btn);
	switch (ext->idx)
	{
	case 0:
		save_emusd_cfg(1, emusd_img->part_sector[0], &emusd_img->part_path[0][0], DRIVE_SD);
		break;
	case 1:
		save_emusd_cfg(2, emusd_img->part_sector[1], &emusd_img->part_path[1][0], DRIVE_SD);
		break;
	case 2:
		save_emusd_cfg(3, emusd_img->part_sector[2], &emusd_img->part_path[2][0], DRIVE_SD);
		break;
	}

	_create_emusd_saved_mbox();
	sd_unmount();

	return LV_RES_INV;
}

static lv_res_t _save_emmc_raw_emusd_cfg_action(lv_obj_t * btn)
{
	lv_btn_ext_t *ext = lv_obj_get_ext_attr(btn);
	switch (ext->idx)
	{
	case 0:
		save_emusd_cfg(1, emusd_img->emmc_part_sector[0], &emusd_img->emmc_part_path[0][0], DRIVE_EMMC);
		break;
	case 1:
		save_emusd_cfg(2, emusd_img->emmc_part_sector[1], &emusd_img->emmc_part_path[1][0], DRIVE_EMMC);
		break;
	case 2:
		save_emusd_cfg(3, emusd_img->emmc_part_sector[2], &emusd_img->emmc_part_path[2][0], DRIVE_EMMC);
		break;
	}

	_create_emusd_saved_mbox();
	sd_unmount();

	return LV_RES_INV;
}

static lv_res_t _create_change_emusd_window(lv_obj_t *btn_caller)
{
	lv_obj_t *win = nyx_create_standard_window(SYMBOL_SETTINGS"  Change emuSD", NULL);
	lv_win_add_btn(win, NULL, SYMBOL_POWER"  Disable", _save_disable_emusd_cfg_action);

	boot_storage_mount();
	sd_mount();
	emmc_mount();

	emusd_img = zalloc(sizeof(*emusd_img));
	emusd_img->win = win;

	mbr_t *mbr = (mbr_t *)malloc(sizeof(mbr_t));
	gpt_t *gpt = (gpt_t*)malloc(sizeof(gpt_t));
	char *path = malloc(512);

	sdmmc_storage_read(&sd_storage, 0, 1, mbr);

	memset(emusd_img->part_path, 0, 3 * 128);

	emusd_img->dirlist = dirlist("emuSD", NULL, DIR_SHOW_DIRS);

	if(!emusd_img->dirlist){
		goto out0;
	}

	u32 emusd_idx = 0;
	FIL fp;

	// Look for emuSD partitions on SD
	u32 emmc_cnt = 0;
	sdmmc_storage_read(&sd_storage, 0, 1, mbr);
	if(mbr_has_gpt(mbr)){
		sdmmc_storage_read(&sd_storage, 1, sizeof(gpt_t) / 0x200, gpt);
		s32 gpt_idx = gpt_get_part_by_name(gpt, "emusd_mbr", -1);
		while(gpt_idx != -1 && emmc_cnt < 3){
			emusd_img->part_sector[emmc_cnt] = gpt->entries[gpt_idx].lba_start;
			emusd_img->part_end[emmc_cnt] = gpt->entries[gpt_idx + 1].lba_end;
			emusd_img->part_idx[emmc_cnt] = gpt_idx;

			emmc_cnt++;
			gpt_idx = gpt_get_part_by_name(gpt, "emusd_mbr", gpt_idx);
		}
	}

	emusd_idx = 0;

	// Check for eMMC raw partitions, based on the folders in /emuMMC
	while(emusd_img->dirlist->name[emusd_idx]){
		s_printf(path, "emuSD/%s/raw_based", emusd_img->dirlist->name[emusd_idx]);

		if(!f_stat(path, NULL)){
			f_open(&fp, path, FA_READ);
			u32 sector = 0;
			f_read(&fp, &sector, 4, NULL);
			f_close(&fp);

			for(u32 i = 0; i < emmc_cnt; i++){
				if(sector && sector >= emusd_img->part_sector[i] && sector <= emusd_img->part_end[i]){
					emusd_img->part_sector[i] = sector;
					emusd_img->part_end[i] = 0;
					s_printf((char*)&emusd_img->part_path[i], "emuSD/%s", emusd_img->dirlist->name[emusd_idx]);
				}
			}
		}
		emusd_idx++;
	}


	// Look for emuSD partitions on eMMC
	emmc_cnt = 0;
	sdmmc_storage_read(&emmc_storage, 0, 1, mbr);
	if(mbr_has_gpt(mbr)){
		sdmmc_storage_read(&emmc_storage, 1, sizeof(gpt_t) / 0x200, gpt);

		s32 gpt_idx = gpt_get_part_by_name(gpt, "emusd_mbr", -1);
		while(gpt_idx != -1 && emmc_cnt < 3){
			emusd_img->emmc_part_sector[emmc_cnt] = gpt->entries[gpt_idx].lba_start;
			emusd_img->emmc_part_end[emmc_cnt] = gpt->entries[gpt_idx + 1].lba_end;
			emusd_img->emmc_part_idx[emmc_cnt] = gpt_idx;

			emmc_cnt++;
			gpt_idx = gpt_get_part_by_name(gpt, "emusd_mbr", gpt_idx);
		}
	}

	free(gpt);
	free(mbr);

	emusd_idx = 0;

	// Check for eMMC raw partitions, based on the folders in /emuMMC
	while(emusd_img->dirlist->name[emusd_idx]){
		s_printf(path, "emuSD/%s/raw_emmc_based", emusd_img->dirlist->name[emusd_idx]);

		if(!f_stat(path, NULL)){
			f_open(&fp, path, FA_READ);
			u32 sector = 0;
			f_read(&fp, &sector, 4, NULL);
			f_close(&fp);

			for(u32 i = 0; i < emmc_cnt; i++){
				if(sector && sector >= emusd_img->emmc_part_sector[i] && sector <= emusd_img->emmc_part_end[i]){
					emusd_img->emmc_part_sector[i] = sector;
					emusd_img->emmc_part_end[i] = 0;
					s_printf((char*)&emusd_img->emmc_part_path[i], "emuSD/%s", emusd_img->dirlist->name[emusd_idx]);
				}
			}
		}
		emusd_idx++;
	}

	u32 file_based_idx = 0;
	emusd_idx = 0;

	// Sanitize the directory list with sd file based ones.
	u8 file_based_drives[DIR_MAX_ENTRIES];
	while (emusd_img->dirlist->name[emusd_idx])
	{
		s_printf(path, "emuSD/%s/file_based", emusd_img->dirlist->name[emusd_idx]);

		if (!f_stat(path, NULL))
		{
			char *tmp = emusd_img->dirlist->name[emusd_idx];
			memcpy(emusd_img->dirlist->name[file_based_idx], tmp, strlen(tmp) + 1);
			file_based_drives[file_based_idx] = DRIVE_SD;
			file_based_idx++;
		}

		s_printf(path, "emuSD/%s/file_emmc_based", emusd_img->dirlist->name[emusd_idx]);
		if (!f_stat(path, NULL))
		{
			char *tmp = emusd_img->dirlist->name[emusd_idx];
			memcpy(emusd_img->dirlist->name[file_based_idx], tmp, strlen(tmp) + 1);
			file_based_drives[file_based_idx] = DRIVE_EMMC;
			file_based_idx++;
		}
		emusd_idx++;
	}
	emusd_img->dirlist->name[file_based_idx] = NULL;

out0:
	static lv_style_t h_style;
	lv_style_copy(&h_style, &lv_style_transp);
	h_style.body.padding.inner = 0;
	h_style.body.padding.hor = LV_DPI - (LV_DPI / 4);
	h_style.body.padding.ver = LV_DPI / 6;

	// Create SD Raw Partitions container.
	lv_obj_t *h1 = lv_cont_create(win, NULL);
	lv_cont_set_style(h1, &h_style);
	lv_cont_set_fit(h1, false, true);
	lv_obj_set_width(h1, (LV_HOR_RES / 17) * 4);
	lv_obj_set_click(h1, false);
	lv_cont_set_layout(h1, LV_LAYOUT_OFF);

	lv_obj_t *label_sep = lv_label_create(h1, NULL);
	lv_label_set_static_text(label_sep, "");

	lv_obj_t *label_txt = lv_label_create(h1, NULL);
	lv_label_set_static_text(label_txt, "SD Raw Partitions");
	lv_obj_set_style(label_txt, lv_theme_get_current()->label.prim);
	lv_obj_align(label_txt, label_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, -(LV_DPI / 2));

	lv_obj_t *line_sep = lv_line_create(h1, NULL);
	static const lv_point_t line_pp[] = { {0, 0}, { LV_HOR_RES - (LV_DPI - (LV_DPI / 2)) * 2, 0} };
	lv_line_set_points(line_sep, line_pp, 2);
	lv_line_set_style(line_sep, lv_theme_get_current()->line.decor);
	lv_obj_align(line_sep, label_txt, LV_ALIGN_OUT_BOTTOM_LEFT, -(LV_DPI / 4), LV_DPI / 8);

	lv_obj_t *btn = NULL;
	lv_btn_ext_t *ext;
	lv_obj_t *btn_label = NULL;
	lv_obj_t *lv_desc = NULL;
	char *txt_buf = malloc(SZ_16K);

	// Create RAW buttons.
	for (u32 i = 0; i < 3; i++)
	{
		btn = lv_btn_create(h1, btn);
		ext = lv_obj_get_ext_attr(btn);
		ext->idx = i;
		btn_label = lv_label_create(btn, btn_label);

		lv_btn_set_state(btn, LV_BTN_STATE_REL);
		lv_obj_set_click(btn, true);

		s_printf(txt_buf, "SD RAW %d", i + 1);
		lv_label_set_text(btn_label, txt_buf);

		if (!emusd_img->part_sector[i] || !emusd_img->part_path[i][0])
		{
			lv_btn_set_state(btn, LV_BTN_STATE_INA);
			lv_obj_set_click(btn, false);
		}

		if (!i)
		{
			lv_btn_set_fit(btn, false, true);
			lv_obj_set_width(btn, LV_DPI * 2 + LV_DPI / 2);
			lv_obj_align(btn, line_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 3, LV_DPI / 5);
		}
		else
			lv_obj_align(btn, lv_desc, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);

		lv_btn_set_action(btn, LV_BTN_ACTION_CLICK, _save_raw_emusd_cfg_action);

		lv_desc = lv_label_create(h1, lv_desc);
		lv_label_set_recolor(lv_desc, true);
		lv_obj_set_style(lv_desc, &hint_small_style);

		s_printf(txt_buf, "Sector start: 0x%08X\nFolder: %s", emusd_img->part_sector[i], &emusd_img->part_path[i][0]);
		lv_label_set_text(lv_desc, txt_buf);
		lv_obj_align(lv_desc, btn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 5);
	}


	// Create eMMC Raw container
	lv_obj_t *h3 = lv_cont_create(win, NULL);
	lv_cont_set_style(h3, &h_style);
	lv_cont_set_fit(h3, false, true);
	lv_obj_set_width(h3, (LV_HOR_RES / 17) * 4);
	lv_obj_set_click(h3, false);
	lv_cont_set_layout(h3, LV_LAYOUT_OFF);

	label_sep = lv_label_create(h3, NULL);
	lv_label_set_static_text(label_sep, "");

	lv_obj_t *label_txt2 = lv_label_create(h3, NULL);
	lv_label_set_static_text(label_txt2, "eMMC Raw Partitions");
	lv_obj_set_style(label_txt2, lv_theme_get_current()->label.prim);
	lv_obj_align(label_txt2, label_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI * 2 / 9, -(LV_DPI / 2));

	line_sep = lv_line_create(h3, line_sep);
	lv_obj_align(line_sep, label_txt2, LV_ALIGN_OUT_BOTTOM_LEFT, -(LV_DPI / 4), LV_DPI / 8);
	lv_line_set_style(line_sep, lv_theme_get_current()->line.decor);

	for (u32 i = 0; i < 3; i++){
		btn = lv_btn_create(h3, btn);
		ext = lv_obj_get_ext_attr(btn);
		ext->idx = i;
		btn_label = lv_label_create(btn, btn_label);

		lv_btn_set_state(btn, LV_BTN_STATE_REL);
		lv_obj_set_click(btn, true);

		s_printf(txt_buf, "eMMC RAW %d", i + 1);
		lv_label_set_text(btn_label, txt_buf);

		if (!emusd_img->emmc_part_sector[i] || !emusd_img->emmc_part_path[i][0])
		{
			lv_btn_set_state(btn, LV_BTN_STATE_INA);
			lv_obj_set_click(btn, false);
		}

		if (!i)
		{
			lv_btn_set_fit(btn, false, true);
			lv_obj_set_width(btn, LV_DPI * 2 + LV_DPI / 2);
			lv_obj_align(btn, line_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 3, LV_DPI / 5);
		}
		else
			lv_obj_align(btn, lv_desc, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);

		lv_btn_set_action(btn, LV_BTN_ACTION_CLICK, _save_emmc_raw_emusd_cfg_action);

		lv_desc = lv_label_create(h3, lv_desc);
		lv_label_set_recolor(lv_desc, true);
		lv_obj_set_style(lv_desc, &hint_small_style);

		s_printf(txt_buf, "Sector start: 0x%08X\nFolder: %s", emusd_img->emmc_part_sector[i], (char*)&emusd_img->emmc_part_path[i]);
		lv_label_set_text(lv_desc, txt_buf);
		lv_obj_align(lv_desc, btn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 5);
	}

	lv_obj_align(h3, h1, LV_ALIGN_OUT_RIGHT_TOP, LV_DPI / 2, 0);

	free(txt_buf);

	// Create SD File Based container.
	lv_obj_t *h2 = lv_cont_create(win, NULL);
	lv_cont_set_style(h2, &h_style);
	lv_cont_set_fit(h2, false, true);
	lv_obj_set_width(h2, LV_HOR_RES * 2 / 5);
	lv_obj_set_click(h2, false);
	lv_cont_set_layout(h2, LV_LAYOUT_OFF);
	lv_obj_align(h2, h3, LV_ALIGN_OUT_RIGHT_TOP, LV_DPI / 2, 0);

	label_sep = lv_label_create(h2, NULL);
	lv_label_set_static_text(label_sep, "");

	lv_obj_t *label_txt3 = lv_label_create(h2, NULL);
	lv_label_set_static_text(label_txt3, "SD/eMMC File Based");
	lv_obj_set_style(label_txt3, lv_theme_get_current()->label.prim);
	lv_obj_align(label_txt3, label_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI * 2 / 9, -LV_DPI / 7);

	line_sep = lv_line_create(h2, line_sep);
	lv_obj_align(line_sep, label_txt3, LV_ALIGN_OUT_BOTTOM_LEFT, -(LV_DPI / 2), LV_DPI / 8);
	lv_line_set_style(line_sep, lv_theme_get_current()->line.decor);

	lv_obj_t *list_sd_based = lv_list_create(h2, NULL);
	lv_obj_align(list_sd_based, line_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 2, LV_DPI / 4);

	lv_obj_set_size(list_sd_based, LV_HOR_RES * 4 / 10 - (LV_DPI / 2), LV_VER_RES * 6 / 10);
	lv_list_set_single_mode(list_sd_based, true);

	if (!emusd_img->dirlist)
		goto out1;

	emusd_idx = 0;

	// Add file based to the list.
	while (emusd_img->dirlist->name[emusd_idx])
	{
		s_printf(path, "emuSD/%s", emusd_img->dirlist->name[emusd_idx]);

		if(file_based_drives[emusd_idx] == DRIVE_SD){
			lv_list_add(list_sd_based, NULL, path, _save_file_emusd_cfg_action);
		}else{
			lv_list_add(list_sd_based, NULL, path, _save_file_emusd_emmc_cfg_action);
		}

		emusd_idx++;
	}

out1:
	free(path);
	sd_unmount();
	boot_storage_unmount();

	return LV_RES_OK;
}

lv_res_t create_tab_emusd_tools(lv_obj_t *parent)
{
	emusd_parent_cont = parent;
	emusd_tools = &create_tab_emusd_tools;

	boot_storage_mount();

	emusd_cfg_t emu_info;
	load_emusd_cfg(&emu_info);

	boot_storage_unmount();

	static lv_style_t h_style;
	lv_style_copy(&h_style, &lv_style_transp);
	h_style.body.padding.inner = 0;
	h_style.body.padding.hor = LV_DPI - (LV_DPI / 4);
	h_style.body.padding.ver = LV_DPI / 9;

	// Create emuSD Info & Selection container.
	lv_obj_t *h1 = lv_cont_create(parent, NULL);
	lv_cont_set_style(h1, &h_style);
	lv_cont_set_fit(h1, false, true);
	lv_obj_set_width(h1, (LV_HOR_RES / 9) * 4);
	lv_obj_set_click(h1, false);
	lv_cont_set_layout(h1, LV_LAYOUT_OFF);

	lv_obj_t *label_sep = lv_label_create(h1, NULL);
	lv_label_set_static_text(label_sep, "");

	lv_obj_t *label_txt = lv_label_create(h1, NULL);
	lv_label_set_static_text(label_txt, "emuSD Info & Selection");
	lv_obj_set_style(label_txt, lv_theme_get_current()->label.prim);
	lv_obj_align(label_txt, h1, LV_ALIGN_IN_TOP_LEFT, LV_DPI / 4, LV_DPI / 11);

	lv_obj_t *line_sep = lv_line_create(h1, NULL);
	static const lv_point_t line_pp[] = { {0, 0}, { LV_HOR_RES - (LV_DPI - (LV_DPI / 4)) * 2, 0} };
	lv_line_set_points(line_sep, line_pp, 2);
	lv_line_set_style(line_sep, lv_theme_get_current()->line.decor);
	lv_obj_align(line_sep, label_txt, LV_ALIGN_OUT_BOTTOM_LEFT, -(LV_DPI / 4), LV_DPI / 8);

	// Create emuSD info labels.
	lv_obj_t *label_btn = lv_label_create(h1, NULL);
	lv_label_set_recolor(label_btn, true);
	lv_label_set_static_text(label_btn, emu_info.enabled ? "#96FF00 "SYMBOL_OK"  Enabled!#" : "#FF8000 "SYMBOL_CLOSE"  Disabled!#");
	lv_obj_align(label_btn, line_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, LV_DPI / 4);

	lv_obj_t *label_txt2 = lv_label_create(h1, NULL);
	lv_label_set_recolor(label_txt2, true);
	char *txt_buf = (char *)malloc(SZ_16K);

	if (emu_info.enabled)
	{
		if(emu_info.enabled == 4){
			if(emu_info.sector){
				s_printf(txt_buf, "#00DDFF Type:# eMMC Raw Partition\n#00DDFF Sector:# 0x%08X",
					emu_info.sector);
			}else{
				s_printf(txt_buf, "#00DDFF Type:# eMMC File\n#00DDFF Base folder:# %s",
					emu_info.path ? emu_info.path : "");
			}
		}else{
			if (emu_info.sector){
				s_printf(txt_buf, "#00DDFF Type:# SD Raw Partition\n#00DDFF Sector:# 0x%08X",
					     emu_info.sector);
			}else{
				s_printf(txt_buf, "#00DDFF Type:# SD File\n#00DDFF Base folder:# %s",
					emu_info.path ? emu_info.path : "");
			}
		}

		lv_label_set_text(label_txt2, txt_buf);
	}
	else
	{
		lv_label_set_static_text(label_txt2, "emuSD is disabled, SD will be used for boot.\n\n");
	}

	if(emu_info.path){
		free(emu_info.path);
	}

	free(txt_buf);

	lv_obj_set_style(label_txt2, &hint_small_style);
	lv_obj_align(label_txt2, label_btn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);

	// Create Change emuMMC button.
	lv_obj_t *btn2 = lv_btn_create(h1, NULL);
	lv_btn_set_fit(btn2, true, true);
	label_btn = lv_label_create(btn2, NULL);
	lv_label_set_static_text(label_btn, SYMBOL_SETTINGS"  Change emuSD");
	lv_obj_align(btn2, label_txt2, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI * 6 / 10);
	lv_btn_set_action(btn2, LV_BTN_ACTION_CLICK, _create_change_emusd_window);

	label_txt2 = lv_label_create(h1, NULL);
	lv_label_set_recolor(label_txt2, true);
	lv_label_set_static_text(label_txt2,
		"Choose an emuSD partition\n"
		"You can at most have 3 emuSD partitions.");

	lv_obj_set_style(label_txt2, &hint_small_style);
	lv_obj_align(label_txt2, btn2, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);

	// Create emuMMC Tools container.
	lv_obj_t *h2 = lv_cont_create(parent, NULL);
	lv_cont_set_style(h2, &h_style);
	lv_cont_set_fit(h2, false, true);
	lv_obj_set_width(h2, (LV_HOR_RES / 9) * 4);
	lv_obj_set_click(h2, false);
	lv_cont_set_layout(h2, LV_LAYOUT_OFF);
	lv_obj_align(h2, h1, LV_ALIGN_OUT_RIGHT_TOP, LV_DPI * 17 / 29, 0);

	label_sep = lv_label_create(h2, NULL);
	lv_label_set_static_text(label_sep, "");

	lv_obj_t *label_txt3 = lv_label_create(h2, NULL);
	lv_label_set_static_text(label_txt3, "emuSD Tools");
	lv_obj_set_style(label_txt3, lv_theme_get_current()->label.prim);
	lv_obj_align(label_txt3, h2, LV_ALIGN_IN_TOP_LEFT, LV_DPI / 4, LV_DPI / 5);

	line_sep = lv_line_create(h2, line_sep);
	lv_obj_align(line_sep, label_txt3, LV_ALIGN_OUT_BOTTOM_LEFT, -(LV_DPI / 4), LV_DPI / 8);

	// Create Create emuMMC button.
	lv_obj_t *btn3 = lv_btn_create(h2, btn2);
	label_btn = lv_label_create(btn3, NULL);
	lv_btn_set_fit(btn3, true, true);
	lv_label_set_static_text(label_btn, SYMBOL_DRIVE"  Create emuSD");
	lv_obj_align(btn3, line_sep, LV_ALIGN_OUT_BOTTOM_LEFT, LV_DPI / 4, LV_DPI / 4);
	lv_btn_set_action(btn3, LV_BTN_ACTION_CLICK, _create_mbox_emusd_create);

	lv_obj_t *label_txt4 = lv_label_create(h2, NULL);
	lv_label_set_recolor(label_txt4, true);
	lv_label_set_static_text(label_txt4,
		"Allows you to create a new emuSD on a suitable #C7EA46 eMMC# partition");

	lv_obj_set_style(label_txt4, &hint_small_style);
	lv_obj_align(label_txt4, btn3, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LV_DPI / 3);

	lv_obj_align(h1, parent, LV_ALIGN_IN_TOP_LEFT, 0, 0);
	lv_obj_align(h2, h1, LV_ALIGN_OUT_RIGHT_TOP, LV_DPI * 17 / 29, 0);

	return LV_RES_OK;
}