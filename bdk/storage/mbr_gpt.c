#include "mbr_gpt.h"
#include <utils/types.h>
#include <string.h>

bool mbr_has_gpt(const mbr_t *mbr){
	for(u32 i = 0; i < 4; i++){
		if(mbr->partitions[i].type == 0xee){
			return true;
		}
	}
	return false;
}

void wctombs(const u16 *src, char *dest, u32 len_max){
	const u16 *cur = src;
	do{
		*dest++ = *cur & 0xff;
		len_max--;
	}while(*cur++ && len_max);
}

void ctowcs(const char *src, u16 *dest, u32 len_max){
	const char *cur = src;
	do{
		*dest++ = *cur;
		len_max--; 
	}while(*cur++ && len_max);
}

s32 gpt_get_part_by_name(gpt_t *gpt, const char* name, s32 prev){
	u16 wc_name[36];
	ctowcs(name, wc_name, 36);
	for(s32 i = prev + 1; i < (s32)gpt->header.num_part_ents && i < 128; i++){
		if(!memcmp(wc_name, gpt->entries[i].name, strlen(name) * 2)){
			return i;
		}
	}
	return -1;
}