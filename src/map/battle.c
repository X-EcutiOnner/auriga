/*
 * Copyright (C) 2002-2020  Auriga
 *
 * This file is part of Auriga.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "db.h"
#include "timer.h"
#include "nullpo.h"
#include "malloc.h"
#include "utils.h"

#include "battle.h"
#include "map.h"
#include "path.h"
#include "pc.h"
#include "bonus.h"
#include "skill.h"
#include "mob.h"
#include "homun.h"
#include "itemdb.h"
#include "clif.h"
#include "pet.h"
#include "guild.h"
#include "status.h"
#include "party.h"
#include "unit.h"
#include "ranking.h"
#include "merc.h"
#include "elem.h"

struct Battle_Config battle_config;

struct battle_delay_damage_ {
	struct block_list *src;
	int target;
	atn_bignumber damage;
	short skillid;
	short skilllv;
	int flag;
	int dist;
};

static int battle_attack_drain(struct block_list *bl,atn_bignumber damage,atn_bignumber damage2,int flag);
static struct Damage battle_calc_attack(int attack_type,struct block_list *bl,struct block_list *target,int skill_num,int skill_lv,int flag);

/*==========================================
 * ダメージの遅延
 *------------------------------------------
 */
static int battle_delay_damage_sub(int tid,unsigned int tick,int id,void *data)
{
	struct battle_delay_damage_ *dat = (struct battle_delay_damage_ *)data;

	if(dat) {
		struct block_list *target = map_id2bl(dat->target);

		if(map_id2bl(id) == dat->src && target && target->prev != NULL) {
			if(dat->src->m == target->m && dat->dist + 10 >= unit_distance(dat->src, target))
				battle_damage(dat->src,target,dat->damage,dat->skillid,dat->skilllv,dat->flag);
		}
		aFree(dat);
	}
	return 0;
}

int battle_delay_damage(unsigned int tick,struct block_list *src,struct block_list *target,atn_bignumber damage,int skillid,int skilllv,int flag)
{
	struct battle_delay_damage_ *dat;

	nullpo_retr(0, src);
	nullpo_retr(0, target);

	dat = (struct battle_delay_damage_*)aCalloc(1,sizeof(struct battle_delay_damage_));
	dat->src     = src;
	dat->target  = target->id;
	dat->damage  = damage;
	dat->skillid = skillid;
	dat->skilllv = skilllv;
	dat->flag    = flag;
	dat->dist    = unit_distance(src, target);
	add_timer2(tick,battle_delay_damage_sub,src->id,dat);

	return 0;
}

/*==========================================
 * 周辺にダメージ
 *------------------------------------------
 */
int battle_damage_area(struct block_list *bl,va_list ap)
{
	struct block_list *src;
	atn_bignumber damage;
	int skillid,skilllv,flag;
	unsigned int tick;

	nullpo_retr(0, bl);
	nullpo_retr(0, ap);

	if((src = va_arg(ap,struct block_list*)) == NULL)
		return 0;

	damage  = va_arg(ap,atn_bignumber);
	skillid = va_arg(ap,int);
	skilllv = va_arg(ap,int);
	flag    = va_arg(ap,int);
	tick    = va_arg(ap,unsigned int);

	if(battle_check_target(src,bl,flag) > 0) {
		clif_damage(bl,bl,tick,status_get_amotion(bl),status_get_dmotion(bl),damage,0,9,0,0);
		battle_damage(src,bl,damage,skillid,skilllv,flag);

		if(skillid == LG_REFLECTDAMAGE) {
			// スキルの反射ダメージのオートスペル
			if(battle_config.weapon_reflect_autospell && src->type == BL_PC && atn_rand()%2)
				bonus_autospell_start(src,bl,EAS_ATTACK,tick,0);

			if(battle_config.weapon_reflect_drain && src != bl)
				battle_attack_drain(src,damage,0,battle_config.weapon_reflect_drain_enable_type);

			// スキルの反射ダメージのアクティブアイテム
			if(battle_config.weapon_reflect_autospell && src->type == BL_PC)
				bonus_activeitem_start((struct map_session_data *)src,EAS_ATTACK,tick);
		}
	}

	return 0;
}

/*==========================================
 * 実際にHPを操作
 *------------------------------------------
 */
int battle_damage(struct block_list *bl,struct block_list *target,atn_bignumber damage,int skillid,int skilllv,int flag)
{
	struct map_session_data *sd = NULL, *tsd = NULL;
	struct mob_data   *tmd  = NULL;
	struct homun_data *thd  = NULL;
	struct merc_data  *tmcd = NULL;
	struct elem_data  *teld = NULL;

	nullpo_retr(0, target);	// blはNULLで呼ばれることがあるので他でチェック

	if(damage == 0 || target->type == BL_PET)
		return 0;

	if(target->prev == NULL)
		return 0;

	if(bl) {
		if(bl->prev == NULL)
			return 0;
		if(bl->type == BL_PC)
			sd = (struct map_session_data *)bl;
	}

	if(damage < 0)
		return battle_heal(bl,target,BIGNUM2INT(-damage),0,flag);

	map_freeblock_lock();

	if(target->type == BL_SKILL) {
		skill_unit_ondamaged((struct skill_unit *)target,bl,BIGNUM2INT(damage),gettick());
		map_freeblock_unlock();
		return 0;
	}

	tsd  = BL_DOWNCAST( BL_PC,   target );
	tmd  = BL_DOWNCAST( BL_MOB,  target );
	thd  = BL_DOWNCAST( BL_HOM,  target );
	tmcd = BL_DOWNCAST( BL_MERC, target );
	teld = BL_DOWNCAST( BL_ELEM, target );

	if(tsd) {
		// ディボーションをかけられている
		if( tsd->sc.data[SC_DEVOTION].timer != -1 &&
		    tsd->sc.data[SC_DEVOTION].val1 &&
		    skillid != PA_PRESSURE &&
		    skillid != SA_COMA &&
		    skillid != NPC_DARKBLESSING &&
		    (skillid != CR_GRANDCROSS || bl == NULL || bl != target) )
		{
			struct map_session_data *msd = map_id2sd(tsd->sc.data[SC_DEVOTION].val1);

			if(msd && skill_devotion3(msd,tsd->bl.id)) {
				skill_devotion(msd);
			} else if(msd && bl) {
				int i;
				unsigned int tick = gettick();
				for(i=0; i<5; i++) {
					if(msd->dev.val1[i] != target->id)
						continue;
					if(msd->sc.data[SC_REBOUND_S].timer != -1) {		// リバウンドシールド
						damage -= damage * (25 + msd->sc.data[SC_REBOUND_S].val1 * 5) / 100;
					}
					// ダメージモーション付きでダメージ表示
					clif_damage(&msd->bl,&msd->bl,tick,0,status_get_dmotion(&msd->bl),damage,0,0,0,0);
					battle_damage(bl,&msd->bl,damage,skillid,skilllv,flag);
					map_freeblock_unlock();
					return 0;
				}
			}
		}
		// シャドウフォームをかけられている
		if( tsd->sc.data[SC__SHADOWFORM].timer != -1 &&
		    tsd->sc.data[SC__SHADOWFORM].val2 &&
		    skillid != PA_PRESSURE &&
		    skillid != SA_COMA &&
		    skillid != NPC_DARKBLESSING &&
		    (skillid != CR_GRANDCROSS || bl == NULL || bl != target) )
		{
			struct map_session_data *msd = map_id2sd(tsd->sc.data[SC__SHADOWFORM].val2);

			if(msd && bl && tsd->shadowform_id != msd->bl.id && skill_shadowform(msd,tsd->bl.id) == 0) {
				// 回数を超えたらシャドウフォーム解除
				if(--tsd->sc.data[SC__SHADOWFORM].val3 <= 0) {
					status_change_end(&tsd->bl,SC__SHADOWFORM,-1);
				}
				// ダメージモーション付きでダメージ表示
				clif_damage(&msd->bl,&msd->bl,gettick(),0,status_get_dmotion(&msd->bl),damage,0,0,0,0);
				battle_damage(bl,&msd->bl,damage,skillid,skilllv,flag);
				map_freeblock_unlock();
				return 0;
			}
		}
		// ウォータースクリーンをかけられている
		if( tsd->eld &&
		    tsd->sc.data[SC_WATER_SCREEN].timer != -1 &&
		    skillid != PA_PRESSURE &&
		    skillid != SA_COMA &&
		    skillid != NPC_DARKBLESSING &&
		    (skillid != CR_GRANDCROSS || bl == NULL || bl != target) )
		{
			struct elem_data *eld = tsd->eld;

			// ダメージモーション付きでダメージ表示
			clif_damage(&eld->bl,&eld->bl,gettick(),0,status_get_dmotion(&eld->bl),damage,0,0,0,0);
			battle_damage(bl,&eld->bl,damage,skillid,skilllv,flag);
			map_freeblock_unlock();
			return 0;
		}
	}

	status_change_attacked_end(target);	// 凍結・石化・睡眠を消去
	unit_skillcastcancel(target,2);		// 詠唱妨害

	if(tsd)       pc_damage(bl,tsd,BIGNUM2INT(damage));
	else if(tmd)  mob_damage(bl,tmd,BIGNUM2INT(damage),0);
	else if(thd)  homun_damage(bl,thd,BIGNUM2INT(damage));
	else if(tmcd) merc_damage(bl,tmcd,BIGNUM2INT(damage));
	else if(teld) elem_damage(bl,teld,BIGNUM2INT(damage));

	// カード効果のコーマ・即死
	if(sd && target && target->prev && !unit_isdead(target) && flag&(BF_WEAPON|BF_NORMAL) && status_get_class(target) != MOBID_EMPERIUM)
	{
		int race = status_get_race(target);
		int ele  = status_get_elem_type(target);
		int mode = status_get_mode(target);

		if((!(mode&MD_BOSS) && atn_rand()%10000 < sd->weapon_coma_ele[ele]) ||
		   (!(mode&MD_BOSS) && atn_rand()%10000 < sd->weapon_coma_race[race]) ||
		   (mode&MD_BOSS && atn_rand()%10000 < sd->weapon_coma_race[RCT_BOSS]) ||
		   (!(mode&MD_BOSS) && atn_rand()%10000 < sd->weapon_coma_race[RCT_NONBOSS]))
		{
			int hp = status_get_hp(target);
			if(tsd)       pc_damage(bl,tsd,hp);
			else if(tmd)  mob_damage(bl,tmd,hp,0);
			else if(thd)  homun_damage(bl,thd,hp);
			else if(tmcd) merc_damage(bl,tmcd,hp);
			else if(teld) elem_damage(bl,teld,hp);
		}
		else if((!(mode&MD_BOSS) && atn_rand()%10000 < sd->weapon_coma_ele2[ele]) ||
			(!(mode&MD_BOSS) && atn_rand()%10000 < sd->weapon_coma_race2[race]) ||
			(mode&MD_BOSS && atn_rand()%10000 < sd->weapon_coma_race2[RCT_BOSS]) ||
			(!(mode&MD_BOSS) && atn_rand()%10000 < sd->weapon_coma_race2[RCT_NONBOSS]))
		{
			int hp = status_get_hp(target) - 1;
			if(tsd)       pc_damage(bl,tsd,hp);
			else if(tmd)  mob_damage(bl,tmd,hp,0);
			else if(thd)  homun_damage(bl,thd,hp);
			else if(tmcd) merc_damage(bl,tmcd,hp);
			else if(teld) elem_damage(bl,teld,hp);
		}
	}

	/* ソウルドレイン */
	if(sd && tmd && unit_isdead(&tmd->bl) && flag&BF_MAGIC)
	{
		int level = pc_checkskill(sd,HW_SOULDRAIN);
		if(level > 0 && (skill_get_inf(skillid) & INF_ATTACK) && sd->ud.skilltarget == target->id) {
			int sp = 0;
			clif_skill_nodamage(bl,target,HW_SOULDRAIN,level,1);
			sp = (status_get_lv(target))*(95+15*level)/100;
			if(sd->status.sp + sp > sd->status.max_sp)
				sp = sd->status.max_sp - sd->status.sp;
			sd->status.sp += sp;
			clif_heal(sd->fd,SP_SP,sp);
		}
	}

	map_freeblock_unlock();

	return 0;
}

/*==========================================
 * HP/SP回復
 *------------------------------------------
 */
int battle_heal(struct block_list *bl,struct block_list *target,int hp,int sp,int flag)
{
	nullpo_retr(0, target);	// blはNULLで呼ばれることがあるので他でチェック

	if(target->type == BL_PET)
		return 0;
	if(unit_isdead(target))
		return 0;
	if(hp == 0 && sp == 0)
		return 0;

	if(hp < 0)
		return battle_damage(bl,target,-hp,0,0,flag);

	if(target->type == BL_MOB)
		return mob_heal((struct mob_data *)target,hp);
	else if(target->type == BL_PC)
		return pc_heal((struct map_session_data *)target,hp,sp,0,0);
	else if(target->type == BL_HOM)
		return homun_heal((struct homun_data *)target,hp,sp);
	else if(target->type == BL_MERC)
		return merc_heal((struct merc_data *)target,hp,sp);
	else if(target->type == BL_ELEM)
		return elem_heal((struct elem_data *)target,hp,sp);

	return 0;
}

/*==========================================
 * ダメージの属性修正
 *------------------------------------------
 */
atn_bignumber battle_attr_fix(atn_bignumber damage,int atk_elem,int def_elem)
{
	int def_type, def_lv, rate;

	// 属性無し(!=無属性)
	if (atk_elem == ELE_NONE)
		return damage;

	def_type = def_elem%20;
	def_lv   = def_elem/20;

	if( atk_elem == ELE_MAX )
		atk_elem = atn_rand()%ELE_MAX;	// 武器属性ランダムで付加

	if( atk_elem < ELE_NEUTRAL || atk_elem >= ELE_MAX ||
	    def_type < ELE_NEUTRAL || def_type >= ELE_MAX ||
	    def_lv <= 0 || def_lv > MAX_ELE_LEVEL )
	{
		// 属性値がおかしいのでとりあえずそのまま返す
		if(battle_config.error_log)
			printf("battle_attr_fix: unknown attr type: atk=%d def_type=%d def_lv=%d\n",atk_elem,def_type,def_lv);
		return damage;
	}

	rate = attr_fix_table[def_lv-1][atk_elem][def_type];
	if(damage < 0 && rate < 0)	// 負×負の場合は結果を負にする
		rate = -rate;

#ifdef PRE_RENEWAL
	return damage * rate / 100;
#else
	return damage + (damage * (rate - 100) / 100);
#endif
}

/*==========================================
 * ダメージ最終計算
 *------------------------------------------
 */
static atn_bignumber battle_calc_damage(struct block_list *src, struct block_list *bl, atn_bignumber damage, int div_, int skill_num, int skill_lv, int flag)
{
	struct map_session_data *tsd = NULL;
	struct mob_data         *tmd = NULL;
	struct unit_data *ud;
	struct status_change *sc, *src_sc;
	unsigned int tick = gettick();

	static const int manuk_mob[] = { 1986,1987,1988,1989,1990,1997,1998,1999 };
	static const int splendide_mob[] = { 1991,1992,1993,1994,1995 };

	nullpo_retr(0, src);
	nullpo_retr(0, bl);

	tsd    = BL_DOWNCAST( BL_PC,  bl );
	tmd    = BL_DOWNCAST( BL_MOB, bl );

	ud = unit_bl2ud(bl);
	sc = status_get_sc(bl);
	src_sc = status_get_sc(src);

	// スキルダメージ補正
	if(damage > 0 && skill_num > 0) {
		int damage_rate = 100;
		if(map[bl->m].flag.normal)
			damage_rate = skill_get_damage_rate(skill_num,0);
		else if(map[bl->m].flag.gvg)
			damage_rate = skill_get_damage_rate(skill_num,2);
		else if(map[bl->m].flag.pvp)
			damage_rate = skill_get_damage_rate(skill_num,1);
		else if(map[bl->m].flag.pk)
			damage_rate = skill_get_damage_rate(skill_num,3);
		if(damage_rate != 100)
			damage = damage*damage_rate/100;
		// カード効果による特定スキルのダメージ耐性(暫定)
		if(tsd && tsd->sub_skill_damage.count > 0) {
			int i;
			for(i=0; i<tsd->sub_skill_damage.count; i++) {
				if(skill_num == tsd->sub_skill_damage.id[i]) {
					damage = damage*(100-tsd->sub_skill_damage.rate[i])/100;
					break;
				}
			}
		}
	}

	if(tmd && ((tmd->mode&MD_SKILLIMMUNITY && (flag&BF_SKILL)) ||
				(tmd->mode&MD_IGNORE_MELEE && (flag&BF_WEAPON)) ||
				(tmd->mode&MD_IGNORE_MAGIC && (flag&BF_MAGIC)) ||
				(tmd->mode&MD_IGNORE_RANGED && (flag&BF_LONG)) ||
				(tmd->mode&MD_IGNORE_MISC && (flag&BF_MISC)) ))
		damage = 0;

	if(sc && sc->count > 0) {
		if(sc->data[SC_INVINCIBLE].timer != -1 && sc->data[SC_INVINCIBLEOFF].timer == -1) {
			if(skill_num == PR_SANCTUARY)
				damage = 1;
			else
				damage = 0;
		}

		if(sc->data[SC_MAXPAIN].timer != -1)
			return damage;

		if(sc->data[SC_WHITEIMPRISON].timer != -1) {
			// ホワイトインプリズン状態は念属性以外はダメージを受けない
			if( (flag&BF_SKILL && skill_get_pl(skill_num) != ELE_GHOST) ||
				(!(flag&BF_SKILL) && status_get_attack_element(src) != ELE_GHOST) )
			damage = 0;
		}

		if( sc->data[SC_GRAVITYCONTROL].timer != -1 )
			damage = 0;

		if(damage > 0) {
			int ele = (flag&BF_SKILL)? skill_get_pl(skill_num): status_get_attack_element(src);
			switch(ele) {
			case ELE_NEUTRAL:	if(sc->data[SC_IMMUNE_PROPERTY_NOTHING].timer != -1) damage = 0; break;
			case ELE_WATER:	if(sc->data[SC_IMMUNE_PROPERTY_WATER].timer != -1)	damage = 0; break;
			case ELE_EARTH:		if(sc->data[SC_IMMUNE_PROPERTY_GROUND].timer != -1)	damage = 0; break;
			case ELE_FIRE:		if(sc->data[SC_IMMUNE_PROPERTY_FIRE].timer != -1)		damage = 0; break;
			case ELE_WIND:		if(sc->data[SC_IMMUNE_PROPERTY_WIND].timer != -1)	damage = 0; break;
			case ELE_DARK:		if(sc->data[SC_IMMUNE_PROPERTY_DARKNESS].timer != -1)	damage = 0; break;
			case ELE_HOLY:		if(sc->data[SC_IMMUNE_PROPERTY_SAINT].timer != -1)		damage = 0; break;
			case ELE_POISON:	if(sc->data[SC_IMMUNE_PROPERTY_POISON].timer != -1)		damage = 0; break;
			case ELE_GHOST:	if(sc->data[SC_IMMUNE_PROPERTY_TELEKINESIS].timer != -1)	damage = 0; break;
			case ELE_UNDEAD:	if(sc->data[SC_IMMUNE_PROPERTY_UNDEAD].timer != -1)		damage = 0; break;
			}
		}
	}

	if(src_sc && src_sc->count > 0) {
#ifdef PRE_RENEWAL
		// 属性場のダメージ増加
		if(src_sc->data[SC_VOLCANO].timer != -1 && damage > 0) {	// ボルケーノ
			if( flag&BF_SKILL && skill_get_pl(skill_num) == ELE_FIRE ) {
				damage += damage * src_sc->data[SC_VOLCANO].val4 / 100;
			}
			else if( !(flag&BF_SKILL) && status_get_attack_element(src) == ELE_FIRE ) {
				damage += damage * src_sc->data[SC_VOLCANO].val4 / 100;
			}
		}
		if(src_sc->data[SC_VIOLENTGALE].timer != -1 && damage > 0) {	// バイオレントゲイル
			if( flag&BF_SKILL && skill_get_pl(skill_num) == ELE_WIND )
				damage += damage * src_sc->data[SC_VIOLENTGALE].val4 / 100;
			else if( !(flag&BF_SKILL) && status_get_attack_element(src) == ELE_WIND )
				damage += damage * src_sc->data[SC_VIOLENTGALE].val4 / 100;
		}
		if(src_sc->data[SC_DELUGE].timer != -1 && damage > 0) {	// デリュージ
			if( flag&BF_SKILL && skill_get_pl(skill_num) == ELE_WATER )
				damage += damage * src_sc->data[SC_DELUGE].val4 / 100;
			else if( !(flag&BF_SKILL) && status_get_attack_element(src) == ELE_WATER )
				damage += damage * src_sc->data[SC_DELUGE].val4 / 100;
		}
#endif
		if(tmd) {
			int i;
			if(src_sc->data[SC_MANU_ATK].timer != -1 && damage > 0 && flag&BF_WEAPON) {	// マヌクフィールドMOB物理ダメージ増加
				for(i = 0; i < (sizeof(manuk_mob) / sizeof(manuk_mob[0])); i++) {
					if(manuk_mob[i] == tmd->class_) {
						damage = damage * src_sc->data[SC_MANU_ATK].val1 / 100;
						break;
					}
				}
			}
			if(src_sc->data[SC_SPL_ATK].timer != -1 && damage > 0 && flag&BF_WEAPON) {	// スプレンティッドフィールドMOB物理ダメージ増加
				for(i = 0; i < (sizeof(splendide_mob) / sizeof(splendide_mob[0])); i++) {
					if(splendide_mob[i] == tmd->class_) {
						damage = damage * src_sc->data[SC_SPL_ATK].val1 / 100;
						break;
					}
				}
			}
			if(src_sc->data[SC_MANU_MATK].timer != -1 && damage > 0 && flag&BF_MAGIC) {	// マヌクフィールドMOB魔法ダメージ増加
				for(i = 0; i < (sizeof(manuk_mob) / sizeof(manuk_mob[0])); i++) {
					if(manuk_mob[i] == tmd->class_) {
						damage = damage * src_sc->data[SC_MANU_MATK].val1 / 100;
						break;
					}
				}
			}
			if(src_sc->data[SC_SPL_MATK].timer != -1 && damage > 0 && flag&BF_MAGIC) {	// スプレンティッドフィールドMOB魔法ダメージ増加
				for(i = 0; i < (sizeof(splendide_mob) / sizeof(splendide_mob[0])); i++) {
					if(splendide_mob[i] == tmd->class_) {
						damage = damage * src_sc->data[SC_SPL_MATK].val1 / 100;
						break;
					}
				}
			}
		}

		if(src_sc->data[SC_INVINCIBLE].timer != -1 && src_sc->data[SC_INVINCIBLEOFF].timer == -1)
			damage += damage * 75 / 100;
		if(src_sc->data[SC_JP_EVENT01].timer != -1 && damage > 0 && flag&BF_WEAPON && status_get_race(bl) == RCT_FISH)
			damage += damage * src_sc->data[SC_JP_EVENT01].val1 / 100;
		if(src_sc->data[SC_JP_EVENT02].timer != -1 && damage > 0 && flag&BF_MAGIC && status_get_race(bl) == RCT_FISH)
			damage += damage * src_sc->data[SC_JP_EVENT02].val1 / 100;
		// 古代精霊のお守り
		if(src_sc->data[SC_PHI_DEMON].timer != -1 && damage > 0 && flag&(BF_WEAPON|BF_MAGIC) && status_get_race(bl) == RCT_DEMON)
			damage += damage * 10 / 100;
		// 術式 -展開-の属性ダメージ増加
		if(src_sc->data[SC_KO_ZENKAI].timer != -1 && damage > 0) {
			// val3に属性値が入ってるので一致すればダメージ増加
			if( (flag&BF_SKILL && skill_get_pl(skill_num) == src_sc->data[SC_KO_ZENKAI].val3) ||
				(!(flag&BF_SKILL) && status_get_attack_element(src) == src_sc->data[SC_KO_ZENKAI].val3) )
					damage += damage * src_sc->data[SC_KO_ZENKAI].val4 / 100;
		}
		/* テレキネシスインテンス */
		if(src_sc->data[SC_TELEKINESIS_INTENSE].timer != -1 && skill_get_pl(skill_num) == ELE_GHOST)
			damage += damage * src_sc->data[SC_TELEKINESIS_INTENSE].val2 / 100;
		// スタイルチェンジ(ファイタースタイル)
		if(src_sc->data[SC_STYLE_CHANGE].timer != -1 && src_sc->data[SC_STYLE_CHANGE].val1 == 1 && atn_rand()%100 < 30 && damage > 0) {
			struct homun_data *hd = (struct homun_data *)src;
			if(hd && hd->spiritball < 10)
				homun_addspiritball(hd,10);
		}
		// エンチャンティングシャドウ
		if(src_sc->data[SC_SHADOW_WEAPON].timer != -1 && atn_rand()%100 < src_sc->data[SC_SHADOW_WEAPON].val2) {
			if(sc && sc->data[SC_SHADOW_SCAR].timer != -1) {
				if(sc->data[SC_SHADOW_SCAR].val1 < 20) {
					status_change_start(bl,SC_SHADOW_SCAR,sc->data[SC_SHADOW_SCAR].val1+1,0,0,0,skill_get_time2(SHC_ENCHANTING_SHADOW,1),0);
				}
			} else {
				status_change_start(bl,SC_SHADOW_SCAR,1,0,0,0,skill_get_time2(SHC_ENCHANTING_SHADOW,1),0);
			}
		}
	}

	if(sc && sc->count > 0 && skill_num != PA_PRESSURE && skill_num != HW_GRAVITATION) {
		// アスムプティオ
		if( (
#ifdef PRE_RENEWAL
			sc->data[SC_ASSUMPTIO].timer != -1 ||
#endif
			sc->data[SC_ASSUMPTIO2].timer != -1) && damage > 0) {
			if(map[bl->m].flag.pvp || map[bl->m].flag.gvg)
				damage = damage * 2 / 3;
			else
				damage = damage / 2;
		}

		// ゴスペルの特殊状態異常
		if(sc->data[SC_INCDAMAGE].timer != -1 && damage > 0)
			damage += damage * sc->data[SC_INCDAMAGE].val1/100;

		// バジリカ
		if(sc->data[SC_BASILICA].timer != -1 && damage > 0 && !(status_get_mode(src)&MD_BOSS))
			damage = 0;

		// ウォールオブフォグ
		if((sc->data[SC_FOGWALL].timer != -1 || sc->data[SC_FOGWALLPENALTY].timer != -1)
			&& damage > 0 && flag&BF_LONG && skill_num!=PA_PRESSURE)
		{
			if(skill_num == 0) {	// 遠距離通常攻撃75%OFF
				damage = damage * 25 / 100;
			} else {		// 遠距離スキル25%OFF
				damage = damage * 75 / 100;
			}
		}

#ifdef PRE_RENEWAL
		// セイフティウォール
		if(sc->data[SC_SAFETYWALL].timer != -1 && flag&BF_SHORT) {
			struct skill_unit *unit = map_id2su(sc->data[SC_SAFETYWALL].val2);
			if(unit && unit->group) {
				if((--unit->group->val2) <= 0)
					skill_delunit(unit);
				damage = 0;
			} else {
				status_change_end(bl,SC_SAFETYWALL,-1);
			}
		}
#else
		// キリエエレイソン
		if(sc->data[SC_KYRIE].timer != -1 && damage > 0) {
			struct status_change_data *scd = &sc->data[SC_KYRIE];
			scd->val2 -= BIGNUM2INT(damage);
			if(flag&BF_WEAPON) {
				if(scd->val2 >= 0)
					damage = 0;
				else
					damage = -scd->val2;
			}
			if(--scd->val3 <= 0 || scd->val2 <= 0 || skill_num == AL_HOLYLIGHT)
				status_change_end(bl, SC_KYRIE, -1);
		}
		// プラチナムアルター
		if(sc->data[SC_P_ALTER].timer != -1 && damage > 0) {
			struct status_change_data *scd = &sc->data[SC_P_ALTER];
			clif_misceffect2(bl,336);
			if(scd->val3 > 0 && scd->val4 > 0) {
				scd->val3 -= BIGNUM2INT(damage);
				if(flag&BF_WEAPON) {
					if(scd->val3 >= 0)
						damage = 0;
					else
						damage = -scd->val3;
				}
				scd->val4--;
			}
		}
		// セイフティウォール
		if(sc->data[SC_SAFETYWALL].timer != -1 && damage > 0 && flag&BF_SHORT) {
			struct skill_unit *unit = map_id2su(sc->data[SC_SAFETYWALL].val2);
			if(unit && unit->group) {
				unit->group->val1 -= BIGNUM2INT(damage);
				unit->group->val2--;
				if(unit->group->val1 > 0 && unit->group->val2 > 0)
					return 0;
				skill_delunit(unit);
				damage = 0;
			} else {
				status_change_end(bl,SC_SAFETYWALL,-1);
			}
		}
#endif

		// カウプ
		if(sc->data[SC_KAUPE].timer != -1 && atn_rand()%100 < sc->data[SC_KAUPE].val2) {
			status_change_end(bl,SC_KAUPE,-1);
			damage = 0;
		}

		// ニューマ・畳返し・ニュートラルバリアー
		if((sc->data[SC_PNEUMA].timer != -1 || sc->data[SC_TATAMIGAESHI].timer != -1 || sc->data[SC_NEUTRALBARRIER].timer != -1) &&
			damage > 0 && flag&(BF_WEAPON|BF_MISC) && flag&BF_LONG) {
			switch(skill_num) {
				case AC_SHOWER:
				case SN_SHARPSHOOTING:
					if(src->type != BL_MOB) {
						damage = 0;
					}
					break;
				default:
					damage = 0;
					break;
			}
		}

		// マグロシールド
		if(sc->data[SC_TUNAPARTY].timer != -1 && damage > 0) {
			struct status_change_data *scd = &sc->data[SC_TUNAPARTY];
			scd->val2 -= BIGNUM2INT(damage);
			if(scd->val2 >= 0)
				damage = 0;
			else
				damage = -scd->val2;
			if(scd->val2 <= 0)
				status_change_end(bl, SC_TUNAPARTY, -1);
		}

		// 次元の書(星帝降臨)
		if(sc->data[SC_DIMENSION2].timer != -1 && damage > 0) {
			struct status_change_data *scd = &sc->data[SC_DIMENSION2];

			if(damage >= scd->val3) {
				scd->val3 = scd->val4;
				scd->val2--;
				if(tsd)
					clif_mshield(tsd,scd->val2);
			} else {
				scd->val3 -= BIGNUM2INT(damage);
			}
			damage = 0;
			if(scd->val2 <= 0)
				status_change_end(bl, SC_DIMENSION2, -1);
		}

		// ダーククロー
		if(sc->data[SC_DARKCROW].timer != -1 && flag&(BF_WEAPON|BF_SHORT))
			damage += damage * sc->data[SC_DARKCROW].val2 / 100;

		// シャドウペイン
		if(sc->data[SC_SHADOW_SCAR].timer != -1 && flag&(BF_WEAPON|BF_SHORT)) {
			damage += damage * sc->data[SC_SHADOW_SCAR].val2 / 100;
		}

		// レックスエーテルナ
		if(sc->data[SC_AETERNA].timer != -1 && damage > 0) {
			damage <<= 1;
			status_change_end(bl,SC_AETERNA,-1);
		}

		// エナジーコート
#ifdef PRE_RENEWAL
		if(sc->data[SC_ENERGYCOAT].timer != -1 && damage > 0 && flag&BF_WEAPON && skill_num != CR_ACIDDEMONSTRATION && skill_num != GN_FIRE_EXPANSION_ACID && (src != bl && skill_num == NPC_EARTHQUAKE))
#else
		if(sc->data[SC_ENERGYCOAT].timer != -1 && damage > 0 && flag&(BF_WEAPON|BF_MAGIC) && skill_num != CR_ACIDDEMONSTRATION && skill_num != GN_FIRE_EXPANSION_ACID && (src != bl && skill_num == NPC_EARTHQUAKE))
#endif
		{
			if(tsd) {
				if(tsd->status.sp > 0) {
					int per = tsd->status.sp * 5 / (tsd->status.max_sp + 1);
					tsd->status.sp -= tsd->status.sp * (per * 5 + 10) / 1000;
					if(tsd->status.sp < 0)
						tsd->status.sp = 0;
					damage -= damage * ((per + 1) * 6) / 100;
					clif_updatestatus(tsd,SP_SP);
				}
				if(tsd->status.sp <= 0)
					status_change_end(bl,SC_ENERGYCOAT,-1);
			} else {
				damage -= damage * (sc->data[SC_ENERGYCOAT].val1 * 6) / 100;
			}
		}

#ifdef PRE_RENEWAL
		// キリエエレイソン
		if(sc->data[SC_KYRIE].timer != -1 && damage > 0) {
			struct status_change_data *scd = &sc->data[SC_KYRIE];
			scd->val2 -= BIGNUM2INT(damage);
			if(flag&BF_WEAPON) {
				if(scd->val2 >= 0)
					damage = 0;
				else
					damage = -scd->val2;
			}
			if(--scd->val3 <= 0 || scd->val2 <= 0 || skill_num == AL_HOLYLIGHT)
				status_change_end(bl, SC_KYRIE, -1);
		}
		// プラチナムアルター
		if(sc->data[SC_P_ALTER].timer != -1 && damage > 0) {
			struct status_change_data *scd = &sc->data[SC_P_ALTER];
			clif_misceffect2(bl,336);
			if(scd->val3 > 0 && scd->val4 > 0) {
				scd->val3 -= BIGNUM2INT(damage);
				if(flag&BF_WEAPON) {
					if(scd->val3 >= 0)
						damage = 0;
					else
						damage = -scd->val3;
				}
				scd->val4--;
			}
		}
#endif

		// インデュア
		if(sc->data[SC_ENDURE].timer != -1 && damage > 0 && flag&BF_WEAPON && src->type != BL_PC) {
			if((--sc->data[SC_ENDURE].val2) <= 0)
				status_change_end(bl, SC_ENDURE, -1);
		}

		// オートガード
		if(sc->data[SC_AUTOGUARD].timer != -1 && damage > 0 && flag&BF_WEAPON && skill_num != WS_CARTTERMINATION) {
			if(atn_rand()%100 < sc->data[SC_AUTOGUARD].val2) {
				int delay;
				damage = 0;
				clif_skill_nodamage(bl,bl,CR_AUTOGUARD,sc->data[SC_AUTOGUARD].val1,1);
				if(sc->data[SC_AUTOGUARD].val1 <= 5)
					delay = 300;
				else if (sc->data[SC_AUTOGUARD].val1 > 5 && sc->data[SC_AUTOGUARD].val1 <= 9)
					delay = 200;
				else
					delay = 100;
				if(ud) {
					ud->canmove_tick = tick + delay;
					if(sc->data[SC_SHRINK].timer != -1 && atn_rand()%100 < 5*sc->data[SC_AUTOGUARD].val1)
						skill_blown(bl,src,2);
				}
			}
		}

		// パリイング
		if(sc->data[SC_PARRYING].timer != -1 && damage > 0 && flag&BF_WEAPON && skill_num != WS_CARTTERMINATION) {
			if(atn_rand()%100 < sc->data[SC_PARRYING].val2)
			{
				int dir = path_calc_dir(bl,src->x,src->y);
				damage = 0;
				clif_skill_nodamage(bl,bl,sc->data[SC_PARRYING].val4,sc->data[SC_PARRYING].val1,1);	// val4はスキルID
				clif_changedir(bl,dir,dir);
				if(ud)
					ud->attackabletime = tick + 500;	// 値適当
			}
		}

		// リジェクトソード
		if(sc->data[SC_REJECTSWORD].timer != -1 && damage > 0 && flag&BF_WEAPON && atn_rand()%100 < 15*sc->data[SC_REJECTSWORD].val1) {
			short weapon = WT_FIST;
			if(src->type == BL_PC)
				weapon = ((struct map_session_data *)src)->status.weapon;
			if(src->type == BL_MOB || weapon == WT_DAGGER || weapon == WT_1HSWORD || weapon == WT_2HSWORD) {
				damage = damage*50/100;
				battle_damage(bl,src,damage,ST_REJECTSWORD,sc->data[SC_REJECTSWORD].val1,0);
				clif_skill_nodamage(bl,bl,ST_REJECTSWORD,sc->data[SC_REJECTSWORD].val1,1);
				if((--sc->data[SC_REJECTSWORD].val2) <= 0)
					status_change_end(bl, SC_REJECTSWORD, -1);
			}
		}

#ifdef PRE_RENEWAL
		// スパイダーウェブ
		if(sc->data[SC_SPIDERWEB].timer != -1 && damage > 0) {
			if( (flag&BF_SKILL && skill_get_pl(skill_num) == ELE_FIRE) ||
			    (!(flag&BF_SKILL) && status_get_attack_element(src) == ELE_FIRE) )
			{
				damage <<= 1;
				status_change_end(bl, SC_SPIDERWEB, -1);
			}
		}
#endif

		// ミレニアムシールド
		if(sc->data[SC_MILLENNIUMSHIELD].timer != -1 && damage > 0) {
			struct status_change_data *scd = &sc->data[SC_MILLENNIUMSHIELD];

			if(damage >= scd->val3) {
				scd->val3 = 1000;
				scd->val2--;
				if(tsd)
					clif_mshield(tsd,scd->val2);
			} else {
				scd->val3 -= BIGNUM2INT(damage);
			}
			damage = 0;
			if(scd->val2 <= 0)
				status_change_end(bl, SC_MILLENNIUMSHIELD, -1);
		}

		// ウェポンブロッキング
		if(sc->data[SC_WEAPONBLOCKING].timer != -1 && flag&BF_WEAPON && flag&BF_SHORT && skill_num != WS_CARTTERMINATION) {
			if(atn_rand()%100 < sc->data[SC_WEAPONBLOCKING].val2) {
				int lv = sc->data[SC_WEAPONBLOCKING].val1;
				damage = 0;
				clif_skill_nodamage(bl,bl,GC_WEAPONBLOCKING,lv,1);
				status_change_start(bl,SC_WEAPONBLOCKING_POSTDELAY,lv,src->id,0,0,skill_get_time2(GC_WEAPONBLOCKING,lv),0);
				status_change_start(bl,SC_WEAPONBLOCK_ON,lv,src->id,0,0,skill_get_time(GC_COUNTERSLASH,lv),0);
			}
		}

		// クローキングエクシード
		if(sc->data[SC_CLOAKINGEXCEED].timer != -1 && damage > 0) {
			if((--sc->data[SC_CLOAKINGEXCEED].val2) <= 0)
				status_change_end(bl, SC_CLOAKINGEXCEED, -1);
		}

		// ハルシネーションウォーク
		if(sc->data[SC_HALLUCINATIONWALK].timer != -1 && damage > 0 && flag&BF_MAGIC) {
			if(atn_rand()%100 < sc->data[SC_HALLUCINATIONWALK].val1 * 10)
				damage = 0;
		}

		// フォースオブバンガード
		if(sc->data[SC_FORCEOFVANGUARD].timer != -1 && damage > 0) {
			struct status_change_data *scd = &sc->data[SC_FORCEOFVANGUARD];
			int max = 5 + scd->val1 * 2;

			if(atn_rand()%100 < scd->val3) {
				if(scd->val4 < max) {
					scd->val4++;
					if(tsd)
						clif_mshield(tsd,scd->val4);
				}
			}
		}

		// プレスティージ
		if(sc->data[SC_PRESTIGE].timer != -1 && damage > 0 && flag&BF_MAGIC) {
			if(atn_rand()%100 < sc->data[SC_PRESTIGE].val3)
				damage = 0;
		}

		// 閃電歩
		if(sc->data[SC_LIGHTNINGWALK].timer != -1 && damage > 0 && flag&(BF_LONG|BF_MAGIC) && skill_get_inf(skill_num)&INF_ATTACK && atn_rand()%100 < sc->data[SC_LIGHTNINGWALK].val2) {
			damage = 0;
			clif_skill_poseffect(bl,SR_LIGHTNINGWALK,sc->data[SC_LIGHTNINGWALK].val1,src->x,src->y,tick);
			clif_move(bl);
			status_change_end(bl, SC_LIGHTNINGWALK, -1);
			unit_movepos(bl,src->x,src->y,0);
		}

		// 点穴 -球-
		if(tsd && sc->data[SC_GENTLETOUCH_ENERGYGAIN].timer != -1 && atn_rand()%100 < sc->data[SC_GENTLETOUCH_ENERGYGAIN].val2 && flag&BF_SHORT && damage > 0) {
			int max = (tsd->s_class.job == PC_JOB_MO || tsd->s_class.job == PC_JOB_SR)? pc_checkskill(tsd,MO_CALLSPIRITS): skill_get_max(MO_CALLSPIRITS);
			if(sc->data[SC_RAISINGDRAGON].timer != -1)
				max += sc->data[SC_RAISINGDRAGON].val1;
			if(tsd->spiritball.num < max)
				pc_addspiritball(tsd,skill_get_time2(SR_GENTLETOUCH_ENERGYGAIN,sc->data[SC_GENTLETOUCH_ENERGYGAIN].val1),1);
		}

		// ダイヤモンドダスト
		if(sc->data[SC_DIAMONDDUST].timer != -1 && damage > 0) {

			if( (flag&BF_SKILL && skill_get_pl(skill_num) == ELE_FIRE) ||
			    (!(flag&BF_SKILL) && status_get_attack_element(src) == ELE_FIRE) )
			{
				status_change_end(bl, SC_DIAMONDDUST, -1);
			}
		}

		// ソーントラップ
		if(sc->data[SC_THORNS_TRAP].timer != -1 && damage > 0) {
			if( (flag&BF_SKILL && skill_get_pl(skill_num) == ELE_FIRE) ||
			    (!(flag&BF_SKILL) && status_get_attack_element(src) == ELE_FIRE) )
			{
				status_change_end(bl, SC_THORNS_TRAP, -1);
			}
		}

		// ファイアーエクスパンション(煙幕)
		if(sc->data[SC_FIRE_EXPANSION_SMOKE_POWDER].timer != -1 && damage > 0 && flag&(BF_SHORT|BF_LONG) && skill_num != PA_PRESSURE) {
			damage -= damage * sc->data[SC_FIRE_EXPANSION_SMOKE_POWDER].val2 / 100;
		}

#ifndef PRE_RENEWAL
		// サプライズアタック
		if(sc->data[SC_RAID].timer != -1 && sc->data[SC_RAID].val1 > 0) {
			sc->data[SC_RAID].val1--;
			if(status_get_mode(bl)&MD_BOSS)
				damage += damage * 10 / 100;
			else
				damage += damage * 20 / 100;
		}
		// ストーンスキン
		if(sc->data[SC_STONESKIN].timer != -1 && damage > 0) {
			if(flag&BF_WEAPON) {
				// うずくまる
				if(sc->data[SC_SU_STOOP].timer != -1 && sc->data[SC_STONESKIN].val1 < 4) {
					damage = damage * 20 / 100;
				}
				else
					damage = damage * (100 - 20 * sc->data[SC_STONESKIN].val1) / 100;
			} else if(flag&BF_MAGIC) {
				damage = damage * (100 + 20 * sc->data[SC_STONESKIN].val1) / 100;
			}
		}
		// アンチマジック
		if(sc->data[SC_ANTIMAGIC].timer != -1 && damage > 0) {
			if(flag&BF_MAGIC) {
				// うずくまる
				if(sc->data[SC_SU_STOOP].timer != -1 && sc->data[SC_STONESKIN].val1 < 4) {
					damage = damage * 20 / 100;
				}
				else
					damage = damage * (100 - 20 * sc->data[SC_ANTIMAGIC].val1) / 100;
			} else if(flag&BF_WEAPON) {
				damage = damage * (100 + 20 * sc->data[SC_ANTIMAGIC].val1) / 100;
			}
		}
#endif
		// 朔月脚
		if(sc->data[SC_NEWMOON].timer != -1 && damage > 0) {
			if((--sc->data[SC_NEWMOON].val2) <= 0)
				status_change_end(bl, SC_NEWMOON, -1);
		}
		// うずくまる
		if(sc->data[SC_SU_STOOP].timer != -1) {
			// ストーンスキン、アンチマジックと競合しない
			if(sc->data[SC_STONESKIN].timer == -1 && sc->data[SC_ANTIMAGIC].timer == -1)
				damage = damage * 20 / 100;
		}
		// ガーディアンシールド
		if(sc->data[SC_GUARDIAN_S].timer != -1 && damage > 0 && flag&BF_WEAPON) {
			if(damage >= sc->data[SC_GUARDIAN_S].val2) {
				damage -= sc->data[SC_GUARDIAN_S].val2;
				status_change_end(bl, SC_GUARDIAN_S, -1);
			} else {
				clif_misceffect2(bl,336);
				sc->data[SC_GUARDIAN_S].val2 -= BIGNUM2INT(damage);
				damage = 0;
			}
		}
		if(src->type == BL_MOB) {
			int i;
			if(sc->data[SC_MANU_DEF].timer != -1 && damage > 0) {	// マヌクフィールドMOBダメージ減少
				struct mob_data *md = (struct mob_data *)src;
				for(i = 0; i < (sizeof(manuk_mob) / sizeof(manuk_mob[0])); i++) {
					if(manuk_mob[i] == md->class_) {
						damage = damage * sc->data[SC_MANU_DEF].val1 / 100;
						break;
					}
				}
			}
			if(sc->data[SC_SPL_DEF].timer != -1 && damage > 0) {	// スプレンティッドフィールドMOBダメージ減少
				struct mob_data *md = (struct mob_data *)src;
				for(i = 0; i < (sizeof(splendide_mob) / sizeof(splendide_mob[0])); i++) {
					if(splendide_mob[i] == md->class_) {
						damage = damage * sc->data[SC_SPL_DEF].val1 / 100;
						break;
					}
				}
			}
		}

		if(sc->data[SC_JP_EVENT03].timer != -1 && damage > 0 && status_get_race(src) == RCT_FISH) {
			damage -= damage * sc->data[SC_JP_EVENT03].val1 / 100;
		}

		// グラニティックアーマー
		if(sc->data[SC_GRANITIC_ARMOR].timer != -1 && damage > 0) {
			damage -= damage * sc->data[SC_GRANITIC_ARMOR].val2 / 100;
		}

		// 火山灰
		if(sc->data[SC_VOLCANIC_ASH].timer != -1 && damage > 0) {
			if( (flag&BF_SKILL && skill_get_pl(skill_num) == ELE_FIRE) ||
			    (!(flag&BF_SKILL) && status_get_attack_element(src) == ELE_FIRE) )
			{
				damage += damage * sc->data[SC_VOLCANIC_ASH].val2 / 100;
			}
		}

		// スタイルチェンジ(グラップラースタイル)
		if(sc->data[SC_STYLE_CHANGE].timer != -1 && sc->data[SC_STYLE_CHANGE].val1 == 2 && atn_rand()%100 < 30 && damage > 0) {
			struct homun_data *t_hd = (struct homun_data *)bl;
			if(t_hd && t_hd->spiritball < 10)
				homun_addspiritball(t_hd,10);
		}

		// 明鏡止水(確率暫定)
		if(sc->data[SC_MEIKYOUSISUI].timer != -1 && atn_rand()%100 < sc->data[SC_MEIKYOUSISUI].val1 * 2)
			damage = 0;

		// ダメージヒール
		if(sc->data[SC_DAMAGE_HEAL].timer != -1 && damage > 0) {
			if(flag & sc->data[SC_DAMAGE_HEAL].val2) {
				int heal = BIGNUM2INT(damage);
				if(flag & BF_WEAPON) {
					clif_misceffect_value(bl, 312, heal);
				} else {
					clif_misceffect_value(bl, 1143, heal);
				}
				clif_misceffect_value(bl, 657, heal);
				unit_heal(bl, BIGNUM2INT(damage), 0, 0, 0);
				damage = 0;
			}
		}
	}

	if(damage > 0) {
		struct guild_castle *gc = NULL;
		int noflag = 0;

		if(tmd && tmd->guild_id) {
			if(tmd->class_ == MOBID_EMPERIUM) {
				// エンペリウム
				if(flag&BF_SKILL && skill_num != HW_GRAVITATION)
					return 0;
				if(src->type == BL_PC) {
					struct guild *g = guild_search(((struct map_session_data *)src)->status.guild_id);

					if(g == NULL)
						return 0;		// ギルド未加入ならダメージ無し
					if(guild_checkskill(g,GD_APPROVAL) <= 0)
						return 0;		// 正規ギルド承認がないとダメージ無し
					if((gc = guild_mapid2gc(tmd->bl.m)) != NULL) {
						if(g->guild_id == gc->guild_id)
							return 0;	// 自占領ギルドのエンペならダメージ無し
						if(guild_check_alliance(gc->guild_id, g->guild_id, 0))
							return 0;	// 同盟ならダメージ無し
					} else {
						noflag = 1;
					}
				} else {
					return 0;
				}
			} else if(!tmd->master_id && tmd->state.special_mob_ai == MOB_AI_NONE) {
				// その他のGv関連のMOB
				if(src->type == BL_PC) {
					struct guild *g = guild_search(((struct map_session_data *)src)->status.guild_id);
					if(g) {
						if((gc = guild_mapid2gc(tmd->bl.m)) != NULL) {
							if(g->guild_id == gc->guild_id)
								return 0;	// 自占領ギルドならダメージ無し
							if(guild_check_alliance(gc->guild_id, g->guild_id, 0))
								return 0;	// 同盟ならダメージ無し
						} else {
							noflag = 1;
						}
					}
				} else {
					return 0;
				}
			}
		}

		// GvG
		if(map[bl->m].flag.gvg && src->type != BL_HOM && skill_num != PA_PRESSURE && skill_num != HW_GRAVITATION) {
			if(tmd && tmd->guild_id) {
				if(gc == NULL && !noflag)	// エンペリウム、その他のGv関連のMOBの項で既に検索してNULLなら再度検索しない
					gc = guild_mapid2gc(tmd->bl.m);
				if(gc) {	// defenseがあればダメージが減るらしい？
#ifndef PRE_RENEWAL
					if(tmd->class_ != MOBID_EMPERIUM)
#endif
						damage -= damage * gc->defense / 100 * battle_config.castle_defense_rate / 100;
				}
			}
			if(skill_num != NPC_SELFDESTRUCTION && skill_num != NPC_SELFDESTRUCTION2) {
				if(flag&BF_SKILL) {	// スキル攻撃
					if(flag&BF_WEAPON)
						damage = damage * battle_config.gvg_normalskill_damage_rate / 100;
					if(flag&BF_MAGIC)
						damage = damage * battle_config.gvg_magic_damage_rate / 100;
					if(flag&BF_MISC)
						damage = damage * battle_config.gvg_misc_damage_rate / 100;
				} else {	// 通常攻撃
					if(flag&BF_SHORT)
						damage = damage * battle_config.gvg_short_damage_rate / 100;
					if(flag&BF_LONG)
						damage = damage * battle_config.gvg_long_damage_rate / 100;
				}
			}
			if(damage < 1)
				damage = (!battle_config.skill_min_damage && flag&BF_MAGIC && src->type == BL_PC)? 0: 1;
		}

		// PK
		if(map[bl->m].flag.pk && bl->type == BL_PC && skill_num != PA_PRESSURE && skill_num != HW_GRAVITATION) {
			if(flag&BF_SKILL) {	// スキル攻撃
				if(flag&BF_WEAPON)
					damage = damage * battle_config.pk_normalskill_damage_rate / 100;
				if(flag&BF_MAGIC)
					damage = damage * battle_config.pk_magic_damage_rate / 100;
				if(flag&BF_MISC)
					damage = damage * battle_config.pk_misc_damage_rate / 100;
			} else {	// 通常攻撃
				if(flag&BF_SHORT)
					damage = damage * battle_config.pk_short_damage_rate / 100;
				if(flag&BF_LONG)
					damage = damage * battle_config.pk_long_damage_rate / 100;
			}
			if(damage < 1)
				damage = (!battle_config.skill_min_damage && flag&BF_MAGIC && src->type == BL_PC)? 0: 1;
		}

		// mapflag
		if(map[bl->m].flag.damage_rate && src->type != BL_MOB) {
			damage = (map[bl->m].flag.damage_rate < 0)? 0: damage * map[bl->m].flag.damage_rate / 100;
			if(damage < 1)
				damage = (!battle_config.skill_min_damage && flag&BF_MAGIC && src->type == BL_PC)? 0: 1;
		}

		// mob mode
		if(bl->type == BL_MOB) {
			// 本来は重複するが排他扱いで実装しておく
			if(status_get_mode(bl)&MD_DAMAGEREDUCTION_1000)
				damage = damage / 1000;
			else if(status_get_mode(bl)&MD_DAMAGEREDUCTION_100)
				damage = damage / 100;
			else if(status_get_mode(bl)&MD_DAMAGEREDUCTION_10)
				damage = damage / 10;
		}
	}

	// 魔導ギアの加熱値上昇
	if(tsd && src != &tsd->bl && tsd->status.hp > 0 && pc_isgear(tsd) && damage > 0) {
		int heat = 1;

		if(((flag&BF_SKILL)? skill_get_pl(skill_num): status_get_attack_element(src)) == ELE_FIRE)
			heat += 2;	// 火属性攻撃の場合は加算値+2

		if(sc && sc->data[SC_OVERHEAT_LIMITPOINT].timer != -1) {	// 既に加熱状態
			heat += sc->data[SC_OVERHEAT_LIMITPOINT].val1;	// 現在の加熱値を取得
			if(heat > 1000)
				heat = 1000;
		}
		status_change_start(bl,SC_OVERHEAT_LIMITPOINT,heat,10,0,0,1000,0);

		// オーバーヒート判定
		if(sc && sc->data[SC_OVERHEAT].timer != -1) {	// 既にオーバーヒート状態
			// 何もしない
		}
		else {
			int limit = 150;
			int skill = pc_checkskill(tsd,NC_MAINFRAME);

			if(skill)
			limit += 120 + 80 * skill + ((skill >= 4)? 10: 0);
			if(heat >= limit)
				status_change_start(bl,SC_OVERHEAT,heat,0,0,0,1000,0);
		}
	}

	if((battle_config.skill_min_damage || flag&BF_MISC) && damage > 0) {
		if(div_ == 255) {
			if(damage < 3)
				damage = 3;
		} else {
			if(damage < div_)
				damage = div_;
		}
	}

	if(tmd && tmd->hp > 0 && damage > 0)	// 反撃などのMOBスキル判定
	{
		int mtg = tmd->target_id;
		if (battle_config.mob_changetarget_byskill != 0 || mtg == 0)
		{
			if(src->type & (BL_PC | BL_MOB | BL_MERC | BL_ELEM))
				tmd->target_id = src->id;
		}
		mobskill_event(tmd,flag);
		tmd->target_id = mtg;	// ターゲットを戻す
	}

	// PCの反撃オートスペル
	if(tsd && src != &tsd->bl && !unit_isdead(src) && tsd->status.hp > 0 && damage > 0)
	{
		unsigned int asflag = EAS_REVENGE;

		if(skill_num == AM_DEMONSTRATION)
			flag = (flag&~BF_WEAPONMASK)|BF_MISC;

		if(flag&BF_WEAPON) {
			if(flag&BF_SKILL) {
				if(battle_config.weapon_attack_autospell)
					asflag += EAS_NORMAL;
				else
					asflag += EAS_SKILL;
			} else {
				asflag += EAS_NORMAL;
			}
			if(flag&BF_SHORT)
				asflag += EAS_SHORT;
			if(flag&BF_LONG)
				asflag += EAS_LONG;
		}
		if(flag&BF_MAGIC) {
			if(battle_config.magic_attack_autospell)
				asflag += EAS_SHORT|EAS_LONG;
			else
				asflag += EAS_MAGIC;
		}
		if(flag&BF_MISC) {
			if(battle_config.misc_attack_autospell)
				asflag += EAS_SHORT|EAS_LONG;
			else
				asflag += EAS_MISC;
		}
		bonus_autospell_start(&tsd->bl,src,asflag,tick,0);
		bonus_activeitem_start(tsd,asflag,tick);
	}

	// PCの状態異常反撃
	if(tsd && src != &tsd->bl && tsd->addreveff_flag && !unit_isdead(src) && tsd->status.hp > 0 && damage > 0 && flag&BF_WEAPON)
	{
		int i;

		for(i = 0; i < MAX_EFF_TYPE; i++) {
			int rate = tsd->addreveff[i];
			if(battle_config.reveff_plus_addeff)
				rate += tsd->addeff[i] + tsd->arrow_addeff[i];

			status_change_addeff_start(bl,src,i,rate,0,tick);
		}
	}

	return damage;
}

/*==========================================
 * HP/SPの％吸収計算
 *------------------------------------------
 */
static atn_bignumber battle_calc_drain_per(atn_bignumber damage, int rate, int per)
{
	atn_bignumber diff = 0;

	if (damage <= 0 || rate <= 0)
		return 0;

	if (per && atn_rand()%100 < rate) {
		diff = damage * per / 100;
		if (diff == 0)
			diff = (per > 0)? 1: -1;
	}

	return diff;
}

/*==========================================
 * HP/SPの一定吸収計算
 *------------------------------------------
 */
static atn_bignumber battle_calc_drain_value(atn_bignumber damage, int rate, int value)
{
	atn_bignumber diff = 0;

	if (damage <= 0 || rate <= 0)
		return 0;

	if (value && atn_rand()%100 < rate) {
		diff = value;
	}

	return diff;
}

/*==========================================
 * 攻撃によるHP/SP吸収
 *------------------------------------------
 */
static int battle_attack_drain(struct block_list *bl,atn_bignumber damage,atn_bignumber damage2,int flag)
{
	int hp = 0,sp = 0;
	struct map_session_data* sd = NULL;

	nullpo_retr(0, bl);

	if(damage <= 0 && damage2 <= 0)
		return 0;

	if(bl->type != BL_PC || (sd = (struct map_session_data *)bl) == NULL)
		return 0;

	if(flag & 1) {	// ％吸収
		if(!battle_config.left_cardfix_to_right) {
			// 二刀流左手カードの吸収系効果を右手に追加しない場合
			hp += BIGNUM2INT(battle_calc_drain_per(damage,  sd->hp_drain.p_rate,  sd->hp_drain.per ));
			hp += BIGNUM2INT(battle_calc_drain_per(damage2, sd->hp_drain_.p_rate, sd->hp_drain_.per));
			sp += BIGNUM2INT(battle_calc_drain_per(damage,  sd->sp_drain.p_rate,  sd->sp_drain.per ));
			sp += BIGNUM2INT(battle_calc_drain_per(damage2, sd->sp_drain_.p_rate, sd->sp_drain_.per));
		} else {
			// 二刀流左手カードの吸収系効果を右手に追加する場合
			int hp_rate = sd->hp_drain.p_rate + sd->hp_drain_.p_rate;
			int hp_per  = sd->hp_drain.per + sd->hp_drain_.per;
			int sp_rate = sd->sp_drain.p_rate + sd->sp_drain_.p_rate;
			int sp_per  = sd->sp_drain.per + sd->sp_drain_.per;
			hp += BIGNUM2INT(battle_calc_drain_per(damage, hp_rate, hp_per));
			sp += BIGNUM2INT(battle_calc_drain_per(damage, sp_rate, sp_per));
		}
	}
	if(flag & 2) {	// 一定吸収
		if(!battle_config.left_cardfix_to_right) {
			// 二刀流左手カードの吸収系効果を右手に追加しない場合
			hp += BIGNUM2INT(battle_calc_drain_value(damage,  sd->hp_drain.v_rate,  sd->hp_drain.value ));
			hp += BIGNUM2INT(battle_calc_drain_value(damage2, sd->hp_drain_.v_rate, sd->hp_drain_.value));
			sp += BIGNUM2INT(battle_calc_drain_value(damage,  sd->sp_drain.v_rate,  sd->sp_drain.value ));
			sp += BIGNUM2INT(battle_calc_drain_value(damage2, sd->sp_drain_.v_rate, sd->sp_drain_.value));
		} else {
			// 二刀流左手カードの吸収系効果を右手に追加する場合
			int hp_rate  = sd->hp_drain.v_rate + sd->hp_drain_.v_rate;
			int hp_value = sd->hp_drain.value + sd->hp_drain_.value;
			int sp_rate  = sd->sp_drain.v_rate + sd->sp_drain_.v_rate;
			int sp_value = sd->sp_drain.value + sd->sp_drain_.value;
			hp += BIGNUM2INT(battle_calc_drain_value(damage, hp_rate, hp_value));
			sp += BIGNUM2INT(battle_calc_drain_value(damage, sp_rate, sp_value));
		}
	}
	if(hp || sp)
		pc_heal(sd, hp, sp, 0, 0);

	return 1;
}

/*==========================================
 * 修練ダメージ
 *------------------------------------------
 */
static atn_bignumber battle_addmastery(struct map_session_data *sd,struct block_list *target,atn_bignumber dmg,int type)
{
	atn_bignumber damage = 0;
	int race, element, skill, weapon;

	nullpo_retr(0, sd);
	nullpo_retr(0, target);

	race = status_get_race(target);
	element = status_get_element(target)%20;

	// デーモンベイン vs 不死 or 悪魔 (死人は含めない？)
	if((skill = pc_checkskill(sd,AL_DEMONBANE)) > 0 && (battle_check_undead(race,status_get_elem_type(target)) || race == RCT_DEMON) ) {
		damage += (300 + 5 * sd->status.base_level) * skill / 100;
	}

	// ビーストベイン(+4 〜 +40) vs 動物 or 昆虫
	if((skill = pc_checkskill(sd,HT_BEASTBANE)) > 0 && (race == RCT_BRUTE || race == RCT_INSECT) )
	{
		damage += (skill * 4);

		if(sd->sc.data[SC_HUNTER].timer != -1)
			damage += status_get_str(&sd->bl);
	}

#ifndef PRE_RENEWAL
	// ヒルトバインディング(+4)
	if((skill = pc_checkskill(sd,BS_HILTBINDING)) > 0) {
		damage += 4;
	}

	// 武器研究(+2 〜 +20)
	if((skill = pc_checkskill(sd,BS_WEAPONRESEARCH)) > 0) {
		damage += (skill * 2);
	}
#endif

	// レンジャーメイン(+5 〜 +50) vs 動物 or 植物 or 魚類
	if((skill = pc_checkskill(sd,RA_RANGERMAIN)) > 0 && (race == RCT_BRUTE || race == RCT_PLANT || race == RCT_FISH) )
	{
		damage += (skill * 5);
	}

	// 魔導ギアライセンス(+20 〜 +100)
	if(pc_isgear(sd) && (skill = pc_checkskill(sd,NC_MADOLICENCE)) > 0)
	{
		damage += (skill * 20);
	}

	// 火と大地の研究(+10 〜 +50) vs 火属性 or 地属性
	if((skill = pc_checkskill(sd,NC_RESEARCHFE)) > 0 && (element == ELE_FIRE || element == ELE_EARTH))
	{
		damage += (skill * 10);
	}

	weapon = (type == 0)? sd->weapontype1: sd->weapontype2;

	switch(weapon)
	{
		case WT_1HSWORD:
#ifndef PRE_RENEWAL
			// アルケミスト斧修練(+3 〜 +30)
			if((skill = pc_checkskill(sd,AM_AXEMASTERY)) > 0) {
				damage += (skill * 3);
			}
#endif
			// fall through
		case WT_DAGGER:
			// 剣修練(+4 〜 +40) 片手剣 短剣含む
			if((skill = pc_checkskill(sd,SM_SWORD)) > 0) {
				damage += (skill * 4);
			}
			// ジェネティック剣修練(+10 〜 +50)
			if((skill = pc_checkskill(sd,GN_TRAINING_SWORD)) > 0) {
				damage += (skill * 10);
			}
			break;
		case WT_2HSWORD:
			// 両手剣修練(+4 〜 +40) 両手剣
			if((skill = pc_checkskill(sd,SM_TWOHAND)) > 0) {
				damage += (skill * 4);
			}
			break;
		case WT_1HSPEAR:
			// 槍修練(+4 〜 +40,+5 〜 +50,+10 〜 +100) 槍
			if((skill = pc_checkskill(sd,KN_SPEARMASTERY)) > 0) {
				if(pc_isdragon(sd) && pc_checkskill(sd,RK_DRAGONTRAINING) > 0)
					damage += (skill * 10);	// ドラゴンに乗っててトレーニング習得済み
				else if(!pc_isriding(sd) && !pc_isdragon(sd))
					damage += (skill * 4);	// ペコに乗ってない
				else
					damage += (skill * 5);	// ペコに乗ってる
			}
			break;
		case WT_2HSPEAR:
			// 槍修練(+4 〜 +40,+5 〜 +50,+10 〜 +100) 槍
			if((skill = pc_checkskill(sd,KN_SPEARMASTERY)) > 0) {
				if(pc_isdragon(sd) && pc_checkskill(sd,RK_DRAGONTRAINING) > 0)
					damage += (skill * 10);	// ドラゴンに乗っててトレーニング習得済み
				else if(!pc_isriding(sd) && !pc_isdragon(sd))
					damage += (skill * 4);	// ペコに乗ってない
				else
					damage += (skill * 5);	// ペコに乗ってる
			}
			break;
		case WT_1HAXE:
			if((skill = pc_checkskill(sd,AM_AXEMASTERY)) > 0) {
				damage += (skill * 3);
			}
			if((skill = pc_checkskill(sd,NC_TRAININGAXE)) > 0) {
				damage += (skill * 5);
			}
			break;
		case WT_2HAXE:
			if((skill = pc_checkskill(sd,AM_AXEMASTERY)) > 0) {
				damage += (skill * 3);
			}
			if((skill = pc_checkskill(sd,NC_TRAININGAXE)) > 0) {
				damage += (skill * 5);
			}
			break;
		case WT_MACE:
			// メイス修練(+3 〜 +30) メイス
			if((skill = pc_checkskill(sd,PR_MACEMASTERY)) > 0) {
				damage += (skill * 3);
			}
			if((skill = pc_checkskill(sd,NC_TRAININGAXE)) > 0) {
				damage += (skill * 4);
			}
			break;
		case WT_2HMACE:
		case WT_STAFF:
		case WT_2HSTAFF:
		case WT_BOW:
			break;
		case WT_FIST:
			// タイリギ(+10 〜 +100) 素手
			if((skill = pc_checkskill(sd,TK_RUN)) > 0) {
				damage += (skill * 10);
			}
			// fall through
		case WT_KNUCKLE:
			// 鉄拳(+3 〜 +30) 素手,ナックル
			if((skill = pc_checkskill(sd,MO_IRONHAND)) > 0) {
				damage += (skill * 3);
			}
			break;
		case WT_MUSICAL:
			// 楽器の練習(+3 〜 +30) 楽器
			if((skill = pc_checkskill(sd,BA_MUSICALLESSON)) > 0) {
				damage += (skill * 3);
			}
			break;
		case WT_WHIP:
			// ダンスの練習(+3 〜 +30) 鞭
			if((skill = pc_checkskill(sd,DC_DANCINGLESSON)) > 0) {
				damage += (skill * 3);
			}
			break;
		case WT_BOOK:
			// アドバンスドブック(+3 〜 +30) {
			if((skill = pc_checkskill(sd,SA_ADVANCEDBOOK)) > 0) {
				damage += (skill * 3);
			}
			break;
		case WT_KATAR:
			// カタール修練(+3 〜 +30) カタール
			if((skill = pc_checkskill(sd,AS_KATAR)) > 0) {
				// ソニックブロー時は別処理（1撃に付き1/8適応)
				damage += (skill * 3);
			}
			break;
		case WT_HANDGUN:
		case WT_RIFLE:
		case WT_SHOTGUN:
		case WT_GATLING:
		case WT_GRENADE:
		case WT_HUUMA:
			break;
	}
	return dmg+damage;
}

/*==========================================
 * 基本武器ダメージ計算
 *------------------------------------------
 */
static atn_bignumber battle_calc_base_damage(struct block_list *src,struct block_list *target,int skill_num,int type,int lh)
{
	atn_bignumber damage = 0;
	int atkmin, atkmax;
#ifdef PRE_RENEWAL
	int weapon;
#endif
	struct map_session_data *sd   = NULL;
	struct status_change *sc = NULL;

	nullpo_retr(0, src);
	nullpo_retr(0, target);

	sc = status_get_sc(src);
	if(src->type == BL_PC)
		sd = (struct map_session_data *)src;

	if(sd) {
		int watk   = (lh == 0)? status_get_atk(src): status_get_atk_(src);
		int dex    = status_get_dex(src);
		int idx    = (lh == 0)? sd->equip_index[EQUIP_INDEX_RARM]: sd->equip_index[EQUIP_INDEX_LARM];
#ifdef PRE_RENEWAL
		int t_size = status_get_size(target);
#else
		short wlv  = 0;
		int cost   = 0;
		int str    = status_get_str(src);

		// Dex依存武器はDexを基本値とする
		if( sd->status.weapon == WT_BOW ||
			sd->status.weapon == WT_MUSICAL ||
			sd->status.weapon == WT_WHIP ||
			(sd->status.weapon >= WT_HANDGUN && sd->status.weapon <= WT_GRENADE) )
			str = dex;
#endif

		if(skill_num == HW_MAGICCRASHER || (skill_num == 0 && sc && sc->data[SC_CHANGE].timer != -1)) {
			// マジッククラッシャーまたはメンタルチェンジ中の通常攻撃ならMATKで殴る
			damage = status_get_matk1(src);
#ifdef PRE_RENEWAL
		} else {
			damage = status_get_baseatk(src);
#endif
		}

		atkmin = dex;	// 最低ATKはDEXで初期化

#ifdef PRE_RENEWAL
		if(idx >= 0 && sd->inventory_data[idx])
			atkmin = atkmin * (80 + sd->inventory_data[idx]->wlv * 20) / 100;
		if(sd->state.arrow_atk)						// 武器が弓矢の場合
			atkmin = watk * ((atkmin < watk)? atkmin: watk) / 100;	// 弓用最低ATK計算
		if(sc && sc->data[SC_IMPOSITIO].timer != -1)	// IMがかかっていたら最小加算攻撃力に加算
			atkmin += sc->data[SC_IMPOSITIO].val1*5;

		/* サイズ修正 */
		if(skill_num == MO_EXTREMITYFIST) {
			// 阿修羅
			atkmax = watk;
		} else if(pc_isriding(sd) && (sd->status.weapon == WT_1HSPEAR || sd->status.weapon == WT_2HSPEAR) && t_size == 1) {
			// ペコ騎乗していて、槍で中型を攻撃した場合はサイズ修正を100にする
			atkmax = watk;
		} else if(pc_isdragon(sd) && (sd->status.weapon == WT_1HSPEAR || sd->status.weapon == WT_2HSPEAR)) {
			// ドラゴン騎乗中の槍はサイズ修正を100にする
			atkmax = watk;
		} else {
			int rate = (lh == 0)? sd->atkmods[t_size]: sd->atkmods_[t_size];
			atkmax = (watk   * rate) / 100;
			atkmin = (atkmin * rate) / 100;
		}
		if(sc && sc->data[SC_WEAPONPERFECTION].timer != -1) {
			// ウェポンパーフェクション
			atkmax = watk;
		} else if(sd->special_state.no_sizefix) {
			// ドレイクカード
			atkmax = watk;
		}
		if(!sd->state.arrow_atk && atkmin > atkmax)
			atkmin = atkmax;	// 弓は最低が上回る場合あり
		if(lh && atkmin > atkmax)
			atkmin = atkmax;

		/* 過剰精錬ボーナス */
		if(!lh && sd->overrefine > 0)
			damage += (atn_rand() % sd->overrefine ) + 1;
		if(lh && sd->overrefine_ > 0)
			damage += (atn_rand() % sd->overrefine_) + 1;
#else
		// 武器があるなら武器Lvとコスト計算
		if(idx >= 0 && sd->inventory_data[idx]) {
			int dstr = str/10;
			damage = str + dstr*dstr/3;	// Strボーナス計算
			wlv = sd->inventory_data[idx]->wlv;
			cost = (watk*2/3) - (dstr*dstr) * 4 / wlv;
			if(cost < 0)	// コストは0以下にならない
				cost = 0;
		}
		// 最大武器Atkと最低武器Atk計算
		atkmax = watk * (100 + wlv * 10) / 100 - cost;
		atkmin = (watk * 5 + atkmin * (wlv*3+18) + 14) / 15 - cost;
		if(atkmin > atkmax)
			atkmin = atkmax;
#endif
	} else {
#ifdef PRE_RENEWAL
		if(battle_config.enemy_str)
			damage = status_get_baseatk(src);
		else
			damage = 0;
#endif
		if(skill_num == HW_MAGICCRASHER || (skill_num == 0 && sc && sc->data[SC_CHANGE].timer != -1)) {
			// マジッククラッシャーまたはメンタルチェンジ中の通常攻撃ならMATKで殴る
			atkmin = status_get_matk1(src);
			atkmax = status_get_matk2(src);
		} else {
			atkmin = status_get_atk(src);
			atkmax = status_get_atk2(src);
		}
		if(atkmin > atkmax)
			atkmin = atkmax;
	}

	if(sc && sc->data[SC_MAXIMIZEPOWER].timer != -1) {
		// マキシマイズパワー
		atkmin = atkmax;
	}
	if(skill_num == NPC_EARTHQUAKE)
		// アースクエイク
		atkmin = atkmax;

	if(type == 0x0a) {
		/* クリティカル攻撃 */
		damage += atkmax;

		if(sc) {
			// 点穴 -反-
			if(sc->data[SC_GENTLETOUCH_CHANGE].timer != -1) {
				damage += sc->data[SC_GENTLETOUCH_CHANGE].val2;
			}
			// ストライキング
			if(sc->data[SC_STRIKING].timer != -1) {
				damage += sc->data[SC_STRIKING].val3;
			}
		}
#ifdef PRE_RENEWAL
		if(sd) {
			int trans_bonus = 0;

			weapon = (lh == 0)? sd->weapontype1: sd->weapontype2;
			if(sd->sc.data[SC_MONSTER_TRANSFORM].timer != -1 && (sd->sc.data[SC_MONSTER_TRANSFORM].val1 == 1276 || sd->sc.data[SC_MONSTER_TRANSFORM].val1 == 1884))
				trans_bonus = 25;
			if(weapon < WT_MAX) {
				if(sd->atk_rate != 100 || sd->weapon_atk_rate[weapon] != 0)
					damage = (damage * (sd->atk_rate + sd->weapon_atk_rate[weapon] + trans_bonus)) / 100;
			}

			// クリティカル時ダメージ増加
			if(sd->sc.data[SC_MONSTER_TRANSFORM].timer != -1 && sd->sc.data[SC_MONSTER_TRANSFORM].val1 == 1002)
				trans_bonus = 5;
			damage += damage * (sd->critical_damage + sd->crate + trans_bonus) / 100;

			if(sd->state.arrow_atk)
				damage += sd->arrow_atk;
		}
#endif
	} else {
		/* 通常攻撃・スキル攻撃 */
		if(atkmax > atkmin)
			damage += atkmin + atn_rand() % (atkmax - atkmin + 1);
		else
			damage += atkmin;

		if(sc) {
			// 点穴 -反-
			if(sc->data[SC_GENTLETOUCH_CHANGE].timer != -1) {
				damage += sc->data[SC_GENTLETOUCH_CHANGE].val2;
			}
			// ストライキング
			if(sc->data[SC_STRIKING].timer != -1) {
				damage += sc->data[SC_STRIKING].val3;
			}
			// 風車に向かって突撃
			if(sc->data[SC_RUSH_WINDMILL].timer != -1) {
				damage += sc->data[SC_RUSH_WINDMILL].val4;
			}
		}
#ifdef PRE_RENEWAL
		if(sd) {
			int trans_bonus = 0;

			weapon = (lh == 0)? sd->weapontype1: sd->weapontype2;
			if(sd->sc.data[SC_MONSTER_TRANSFORM].timer != -1 && (sd->sc.data[SC_MONSTER_TRANSFORM].val1 == 1276 || sd->sc.data[SC_MONSTER_TRANSFORM].val1 == 1884))
				trans_bonus = 25;
			if(weapon < WT_MAX) {
				if(sd->atk_rate != 100 || sd->weapon_atk_rate[weapon] != 0) {
					damage = (damage * (sd->atk_rate + sd->weapon_atk_rate[weapon] + trans_bonus)) / 100;
				}
			}
		}
#endif
	}

#ifndef PRE_RENEWAL
	if(sd) {
		int t_size = status_get_size(target);

		switch(skill_num) {
		case NJ_ZENYNAGE:
		case KO_MUCHANAGE:
			break;
		default:
			if(!lh)
				damage += status_get_atk2(src);
			if(lh)
				damage += status_get_atk_2(src);
			break;
		}

		/* 過剰精錬ボーナス */
		if( sd->status.weapon != WT_BOW &&	// 弓と銃には過剰精錬ボーナスがない
			(sd->status.weapon < WT_HANDGUN || sd->status.weapon > WT_GRENADE) ) {
			if(!lh && sd->overrefine > 0)
				damage += (atn_rand() % sd->overrefine ) + 1;
			if(lh && sd->overrefine_ > 0)
				damage += (atn_rand() % sd->overrefine_) + 1;
		}

		/* サイズ修正 */
		if(pc_isriding(sd) && (sd->status.weapon == WT_1HSPEAR || sd->status.weapon == WT_2HSPEAR) && t_size == 1) {
			// ペコ騎乗していて、槍で中型を攻撃した場合はサイズ修正を100にする
		} else if(pc_isdragon(sd) && (sd->status.weapon == WT_1HSPEAR || sd->status.weapon == WT_2HSPEAR)) {
			// ドラゴン騎乗中の槍はサイズ修正を100にする
		} else if(sc && sc->data[SC_WEAPONPERFECTION].timer != -1) {
			// ウェポンパーフェクション
		} else if(sd->special_state.no_sizefix) {
			// ドレイクカード
		} else {
			int rate = (lh == 0)? sd->atkmods[t_size]: sd->atkmods_[t_size];
			damage = (damage * rate) / 100;
		}
	}
#endif

	return damage;
}

// 左手判定があるときだけdamage2を計算する
#define DMG_FIX( a,b ) { wd.damage = wd.damage*(a+(add_rate*b/100))/(b); if(calc_flag.lh) wd.damage2 = wd.damage2*(a+(add_rate*b/100))/(b); }
#define DMG_ADD( a )   { wd.damage += (a); if(calc_flag.lh) wd.damage2 += (a); }
#define DMG_SET( a )   { wd.damage = (a); if(calc_flag.lh) wd.damage2 = (a); }

/*==========================================
 * 武器ダメージ計算
 *------------------------------------------
 */
static struct Damage battle_calc_weapon_attack(struct block_list *src,struct block_list *target,int skill_num,int skill_lv,int wflag)
{
	struct Damage wd = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	struct map_session_data *src_sd  = NULL, *target_sd  = NULL;
	struct mob_data         *src_md  = NULL, *target_md  = NULL;
	struct pet_data         *src_pd  = NULL;
	struct homun_data       *src_hd  = NULL;
	struct merc_data        *src_mcd = NULL;
	struct elem_data        *src_eld = NULL;
	struct status_change    *sc      = NULL, *t_sc       = NULL;
	int s_ele, s_ele_, s_str;
	int t_vit, t_race, t_ele, t_enemy, t_size, t_mode, t_group, t_class;
	int t_flee, t_def1, t_def2, t_res;
	int cardfix, skill, damage_sbr = 0;
	int ignored_rate = 100, ignored_rate_ = 100;
	int i;
	int add_rate = 0;
	struct {
		int rh;			// 右手
		int lh;			// 左手
		int hitrate;		// ヒット確率
		int autocounter;	// オートカウンターON
		int da;			// 連撃判定（0〜2）
		int idef;		// DEF無視
		int idef_;		// DEf無視（左手）
		int nocardfix;		// カード補正なし
		int critical;	// クリティカル判定（0:なし、1:判定中、2:クリティカル確定）
	} calc_flag;

	memset(&calc_flag, 0, sizeof(calc_flag));

	// return前の処理があるので情報出力部のみ変更
	if(src == NULL || target == NULL) {
		nullpo_info(NLP_MARK);
		return wd;
	}

	src_sd  = BL_DOWNCAST( BL_PC,   src );
	src_md  = BL_DOWNCAST( BL_MOB,  src );
	src_pd  = BL_DOWNCAST( BL_PET,  src );
	src_hd  = BL_DOWNCAST( BL_HOM,  src );
	src_mcd = BL_DOWNCAST( BL_MERC, src );
	src_eld = BL_DOWNCAST( BL_ELEM, src );

	target_sd  = BL_DOWNCAST( BL_PC,   target );
	target_md  = BL_DOWNCAST( BL_MOB,  target );

	// アタッカー
	s_ele   = status_get_attack_element(src);	// 属性
	s_ele_  = status_get_attack_element2(src);	// 左手属性
	s_str   = status_get_str(src);			// STR
	sc      = status_get_sc(src);		// ステータス異常

	// ターゲット
	t_vit   = status_get_vit(target);
	t_race  = status_get_race(target);		// 対象の種族
	t_ele   = status_get_elem_type(target);	// 対象の属性
	t_enemy = status_get_enemy_type(target);	// 対象の敵タイプ
	t_size  = status_get_size(target);		// 対象のサイズ
	t_mode  = status_get_mode(target);		// 対象のMode
	t_group = status_get_group(target);
	t_class = status_get_class(target);
	t_flee  = status_get_flee(target);
	t_def1  = status_get_def(target);
	t_def2  = status_get_def2(target);
	t_res   = status_get_res(target);		// 対象のRes
	t_sc    = status_get_sc(target);		// 対象のステータス異常

	if(src_sd && skill_num != CR_GRANDCROSS && skill_num != NPC_GRANDDARKNESS && skill_num != RA_CLUSTERBOMB && skill_num != RA_FIRINGTRAP && skill_num != RA_ICEBOUNDTRAP)
		src_sd->state.attack_type = BF_WEAPON;	// 攻撃タイプは武器攻撃

	/* １．オートカウンター処理 */
	if(skill_lv >= 0 && (skill_num == 0 || (target_sd && battle_config.pc_auto_counter_type&2) ||
		(target_md && battle_config.monster_auto_counter_type&2))
	) {
		if( skill_num != CR_GRANDCROSS &&
		    skill_num != NPC_GRANDDARKNESS &&
		    skill_num != NPC_EARTHQUAKE &&
		    skill_num != RA_CLUSTERBOMB &&
		    skill_num != RA_FIRINGTRAP &&
		    skill_num != RA_ICEBOUNDTRAP &&
		    t_sc &&
		    t_sc->data[SC_AUTOCOUNTER].timer != -1 )
		{
			// グランドクロスでなく、対象がオートカウンター状態の場合
			int dir   = path_calc_dir(src,target->x,target->y);
			int t_dir = status_get_dir(target);
			int dist  = unit_distance(src,target);

			if(dist <= 0 || path_check_dir(dir,t_dir) ) {
				// 対象との距離が0以下、または対象の正面？
				t_sc->data[SC_AUTOCOUNTER].val3 = 0;
				t_sc->data[SC_AUTOCOUNTER].val4 = 1;
				if(sc && sc->data[SC_AUTOCOUNTER].timer == -1) {
					int range = status_get_range(target);
					// 自分がオートカウンター状態
					if( target_sd &&
						(target_sd->status.weapon != WT_BOW && !(target_sd->status.weapon >= WT_HANDGUN && target_sd->status.weapon <= WT_GRENADE))
						&& dist <= range+1)
					{
						// 対象がPCで武器が弓矢でなく射程内
						t_sc->data[SC_AUTOCOUNTER].val3 = src->id;
					}
					if( target_md && range <= 3 && dist <= range+1 ) {
						// または対象がMobで射程が3以下で射程内
						t_sc->data[SC_AUTOCOUNTER].val3 = src->id;
					}
				}
				return wd; // ダメージ構造体を返して終了
			}
			calc_flag.autocounter = 1;
		}
	}

	// キングスグレイス
	if(t_sc && t_sc->data[SC_KINGS_GRACE].timer != -1)
		return wd;

	/* ２．初期化補正 */
	if( (src_sd && battle_config.pc_attack_attr_none) ||
	    (src_md && battle_config.mob_attack_attr_none) ||
	    (src_pd && battle_config.pet_attack_attr_none) ||
	     src_hd || src_mcd || src_eld )
	{
		// 属性無し(!=無属性)
		if (s_ele == ELE_NEUTRAL)
			s_ele  = ELE_NONE;
		if (s_ele_ == ELE_NEUTRAL)
			s_ele_ = ELE_NONE;
	}

#ifdef PRE_RENEWAL
	calc_flag.hitrate = status_get_hit(src) - t_flee + 80;	// pre命中率計算
#else
	calc_flag.hitrate = status_get_hit(src) - t_flee;	// RE命中率計算
#endif

	if(t_sc && t_sc->data[SC_FOGWALL].timer != -1) {
		// 霧のHIT補正
		calc_flag.hitrate -= 50;
	}
	if(src_sd) {
		// 斧鍛錬
		if((skill = pc_checkskill(src_sd,NC_TRAININGAXE)) > 0) {
			if(src_sd->status.weapon == WT_1HAXE || src_sd->status.weapon == WT_2HAXE)
				calc_flag.hitrate += skill*3;
			if(src_sd->status.weapon == WT_MACE)
				calc_flag.hitrate += skill*2;
		}
		// 武器研究
		calc_flag.hitrate += calc_flag.hitrate * 2 * pc_checkskill(src_sd, BS_WEAPONRESEARCH) / 100;
	}

	/* ３．wd構造体の初期設定 */
	wd.type      = 0;
	wd.div_      = skill_get_num(skill_num,skill_lv);
	wd.blewcount = skill_get_blewcount(skill_num,skill_lv);
	wd.flag      = BF_SHORT | BF_WEAPON | BF_NORMAL;	// 攻撃の種類の設定

	if(skill_num == GS_DESPERADO)
		wd.div_ = 1;
	else if(wd.div_ <= 0)
		wd.div_ = 1;

	if(src_sd) {
		if(src_sd->status.weapon == WT_BOW || (src_sd->status.weapon >= WT_HANDGUN && src_sd->status.weapon <= WT_GRENADE)) {	// 武器が弓矢の場合
			wd.flag = (wd.flag&~BF_RANGEMASK)|BF_LONG;	// 遠距離攻撃フラグを有効
			if(src_sd->arrow_ele > ELE_NEUTRAL)	// 属性矢なら属性を矢の属性に変更
				s_ele = src_sd->arrow_ele;
			src_sd->state.arrow_atk = 1;	// 有効化
		} else {
			src_sd->state.arrow_atk = 0;	// 初期化
		}
		if(pc_checkskill(src_sd,SU_SOULATTACK) > 0 && skill_num == 0) 	// ソウルアタック
			wd.flag = (wd.flag&~BF_RANGEMASK)|BF_LONG;	// 遠距離攻撃フラグを有効
	} else if(src_md || src_pd || src_mcd || src_eld) {
		if(status_get_range(src) > 3)
			wd.flag = (wd.flag&~BF_RANGEMASK)|BF_LONG;
	}

	/* ４．右手・左手判定 */
	calc_flag.rh = 1;		// 基本は右手のみ
	if(src_sd && skill_num == 0) {	// スキル攻撃は常に右手を参照
		if((src_sd->weapontype1 == WT_FIST && src_sd->weapontype2 > WT_FIST) || (src_sd->status.weapon >= WT_DOUBLE_DD || src_sd->status.weapon == WT_KATAR))
			calc_flag.lh = 1;	// 左手も計算
	}

	/* ５．連撃判定 */
	if(src_sd && skill_num == 0 && skill_lv >= 0) {
		do {
			// ダブルアタック
			if((skill = pc_checkskill(src_sd,TF_DOUBLE)) > 0 && src_sd->weapontype1 == WT_DAGGER && atn_rand()%100 < skill*5) {
				calc_flag.da = 1;
				calc_flag.hitrate = calc_flag.hitrate*(100+skill)/100;
				break;
			}
			// 影武者
			if(sc && sc->data[SC_KAGEMUSYA].timer != -1) {
				if((skill = sc->data[SC_KAGEMUSYA].val1) > 0 && atn_rand()%100 < skill*5) {
					calc_flag.da = 1;
					calc_flag.hitrate = calc_flag.hitrate*(100+skill)/100;
				}
				break;
			}
			// エターナルチェーン
			if(sc && sc->data[SC_E_CHAIN].timer != -1) {
				if((skill = sc->data[SC_E_CHAIN].val1) > 0 && src_sd->weapontype1 != WT_FIST && atn_rand()%100 < skill*5) {
					calc_flag.da = 1;
					status_change_start(src,SC_QD_SHOT_READY,skill,target->id,0,0,skill_get_time2(RL_QD_SHOT,skill),0);
					break;
				}
			}
			// チェーンアクション
			else {
				if((skill = pc_checkskill(src_sd,GS_CHAINACTION)) > 0 && src_sd->weapontype1 == WT_HANDGUN && atn_rand()%100 < skill*5) {
					calc_flag.da = 1;
					if(pc_checkskill(src_sd,RL_QD_SHOT) > 0)
						status_change_start(src,SC_QD_SHOT_READY,skill,target->id,0,0,skill_get_time2(RL_QD_SHOT,skill),0);
					break;
				}
			}
			// サイドワインダー等
			if(src_sd->double_rate > 0 && atn_rand()%100 < src_sd->double_rate) {
				calc_flag.da = 1;
				break;
			}
		} while(0);
	} else if (src_md && skill_num == 0) {
		do {
			// ダブルアタック
			if (mobdb_search(src_md->class_)->mode_opt[MDOPT_DOUBLE] && atn_rand() % 100 < mobdb_search(src_md->class_)->mode_opt[MDOPT_DOUBLE]*5)
			{
				calc_flag.da = 1;
				calc_flag.hitrate = calc_flag.hitrate * 110 / 100;
				break;
			}
		} while (0);
	}

	/* ６．クリティカル計算 */
	switch(skill_num) {
	case 0:						// 通常攻撃
	case KN_AUTOCOUNTER:		// オートカウンター
	case SN_SHARPSHOOTING:		// シャープシューティング
	case NJ_KIRIKAGE:			// 影斬り
	case LG_PINPOINTATTACK:		// ピンポイントアタック
	case DK_SERVANTWEAPON_ATK:	// サーヴァントウェポン(攻撃)
	case DK_SERVANT_W_PHANTOM:	// サーヴァントウェポン：ファントム
	case DK_SERVANT_W_DEMOL:	// サーヴァントウェポン：デモリッション
	case DK_HACKANDSLASHER:		// ハックアンドスラッシャー
	case DK_HACKANDSLASHER_ATK:	// ハックアンドスラッシャー(攻撃)
	case DK_STORMSLASH:			// ストームスラッシュ
	case IQ_MASSIVE_F_BLASTER:	// 炎火滅魔神弾
	case IQ_EXPOSION_BLASTER:	// 爆火神弾
	case IQ_THIRD_PUNISH:		// 第三撃：断罪
	case CD_EFFLIGO:			// エフィリゴ
	case CD_PETITIO:			// ペティティオ
	case SHC_SAVAGE_IMPACT:		// サベージインパクト
	case SHC_ETERNAL_SLASH:		// エターナルスラッシュ
	case SHC_IMPACT_CRATER:		// インパクトクレーター
	case SHC_FATAL_SHADOW_CROW:	// フェイタルシャドウクロー
	case ABC_FRENZY_SHOT:		// フレンジショット
	case WH_HAWKRUSH:			// ホークラッシュ
	case WH_HAWKBOOMERANG:		// ホークブーメラン
	case SH_HOGOGONG_STRIKE:	// タイガーストライク
	case HN_MEGA_SONIC_BLOW:	// メガソニックブロー
	case SKE_ALL_IN_THE_SKY:	// 天羅万象
	case SS_KAGEGISSEN:			// 影一閃/
	case MT_TRIPLE_LASER:		// トリプルレーザー
	case BO_MAYHEMIC_THORNS:	// メイヘミックソーンズ
	case MA_SHARPSHOOTING:		// シャープシューティング（傭兵）
		calc_flag.critical = 1;		// クリティカル判定を判定中に
		break;
	case WH_GALESTORM:			// ゲイルストーム
		if(sc && sc->data[SC_CALAMITYGALE].timer != -1)
			calc_flag.critical = 1;		// カラミティゲイル中のみクリティカル判定
		break;
	case NW_ONLY_ONE_BULLET:	// オンリーワンバレット
	case NW_SPIRAL_SHOOTING:	// スパイラルシューティング
		if(src_sd && src_sd->status.weapon == WT_RIFLE)
			calc_flag.critical = 1;
		break;
	case NW_MAGAZINE_FOR_ONE:	// マガジンフォーワン
		if(src_sd && src_sd->status.weapon == WT_HANDGUN)
			calc_flag.critical = 1;
		break;
	case SH_CHUL_HO_SONIC_CLAW:	// タイガースラッシュ
		if((src_sd && pc_checkskill(src_sd,SH_COMMUNE_WITH_CHUL_HO)) || (sc && sc->data[SC_TEMPORARY_COMMUNION].timer != -1))
			calc_flag.critical = 1;
		break;
	case SKE_NOON_BLAST:		// 太天一陽
		if(sc && (sc->data[SC_NOON_SUN].timer != -1 || sc->data[SC_SKY_ENCHANT].timer != -1))
			calc_flag.critical = 1;
		break;
	case SKE_SUNSET_BLAST:		// 天陽
		if(sc && (sc->data[SC_SUNSET_SUN].timer != -1 || sc->data[SC_SKY_ENCHANT].timer != -1))
			calc_flag.critical = 1;
		break;

	}
	if( calc_flag.critical != 0 && (!src_md || battle_config.enemy_critical || mobdb_search(src_md->class_)->mode_opt[MDOPT_CRITICAL]) &&
#ifdef PRE_RENEWAL
		calc_flag.da == 0 &&		// PREは連撃が発動していない場合
#endif
		skill_lv >= 0 )
	{
		int cri = status_get_critical(src);
		if(src_sd) {
			if(target_sd && !pc_isdoram(target_sd))
				cri += src_sd->critical_race[RCT_DEMIHUMAN];
			else if(t_race < RCT_ALL)
				cri += src_sd->critical_race[t_race];
			if(src_sd->state.arrow_atk)
				cri += src_sd->arrow_cri;
			if(src_sd->status.weapon == WT_KATAR)
				cri <<= 1;	// カタールの場合、クリティカルを倍に
		}
#ifdef PRE_RENEWAL
		cri -= status_get_luk(target) * 3;	// preクリティカル率計算
#else
		cri -= status_get_luk(target) / 5 * 10;	// REクリティカル率計算
#endif
		if(src_md && battle_config.enemy_critical_rate != 100) {
			cri = cri * battle_config.enemy_critical_rate / 100;
			if(cri < 1) cri = 1;
		}
		if(src_sd) {
			if(target_sd && !pc_isdoram(target_sd))
				cri += cri * (100 + src_sd->critical_race_rate[RCT_DEMIHUMAN]) / 100;
			else if(t_race < RCT_ALL)
				cri += cri * (100 + src_sd->critical_race_rate[t_race]) / 100;
			if(cri < 1) cri = 1;
		}
		if(t_sc && t_sc->data[SC_SLEEP].timer != -1)
			cri <<= 1;		// 睡眠中はクリティカルが倍に
		if(sc && sc->data[SC_CAMOUFLAGE].timer != -1)	// カモフラージュ
			cri += 1000 - (10 - sc->data[SC_CAMOUFLAGE].val2) * 100;
		if(calc_flag.autocounter)
			cri = 1000;

		if(skill_num == KN_AUTOCOUNTER) {
			if(!(battle_config.pc_auto_counter_type&1))
				cri = 1000;
			else
				cri <<= 1;
		} else if(skill_num == SN_SHARPSHOOTING || skill_num == MA_SHARPSHOOTING) {
			cri += 200;
		} else if(skill_num == NJ_KIRIKAGE) {
			cri += (250+skill_lv*50);
		} else if(skill_num == LG_PINPOINTATTACK || skill_num == SHC_FATAL_SHADOW_CROW) {
			cri = 1000;
		}

		if(target_sd && target_sd->critical_def) {
			if(target_sd->critical_def > 100)
				cri = 0;
			else
				cri = cri * (100 - target_sd->critical_def) / 100;
		}

		// 確率判定
		if(atn_rand() % 1000 < cri) {
			if(skill_num != 0 && skill_num != KN_AUTOCOUNTER) {
				// DEF無視フラグ
				calc_flag.idef = calc_flag.idef_ = 1;
			} else {
				wd.type = 0x0a;	// クリティカル攻撃
			}
			calc_flag.critical = 2;		// クリティカル判定を確定に
		}
	}

	/* ７．ヒット・属性・レンジ・ヒット回数修正 */
	if(wd.type == 0) {
		int e = ELE_NONE;

		// 矢があるならヒットを加算
		if(src_sd && src_sd->state.arrow_atk) {
			calc_flag.hitrate += src_sd->arrow_hit;
		}

		// 属性変更
		if(skill_num > 0) {
			wd.flag = (wd.flag&~BF_SKILLMASK)|BF_SKILL;
			e = skill_get_pl(skill_num);
		}
		if(e != ELE_NONE && e != ELE_NEUTRAL) {
			s_ele = s_ele_ = e;
		} else {
			e = status_get_attack_element_nw(src);
			if(e != ELE_NEUTRAL) {
				// 武器に属性が付与されているなら、付与属性に変更
				s_ele = s_ele_ = e;
			}
		}

		// ここから距離による判定
		switch(skill_get_range_type(skill_num)) {
			case 0:	// 近距離
				if(skill_num != 0)
					wd.flag = (wd.flag&~BF_RANGEMASK)|BF_SHORT;
				break;
			case 1:	// 通常遠距離
				if(battle_config.calc_dist_flag&1 && (src->type != BL_PC || target->type != BL_PC)) {	// PC vs PCは強制無視
					int target_dist = unit_distance(src,target);	// 距離を取得
					if(target_dist < battle_config.allow_sw_dist) {	// 設定した距離より小さい＝近距離からの攻撃
						if(src->type == BL_PC && battle_config.sw_def_type & 1) {	// 人間からのを判定するか +1でする
							wd.flag = (wd.flag&~BF_RANGEMASK)|BF_SHORT;
							break;
						} else if(src->type == BL_MOB && battle_config.sw_def_type & 2) {	// モンスターからのを判定するか +2でする
							wd.flag = (wd.flag&~BF_RANGEMASK)|BF_SHORT;
							break;
						}
					}
				}
				wd.flag = (wd.flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case 2:	// 強制遠距離
				wd.flag = (wd.flag&~BF_RANGEMASK)|BF_LONG;
				break;
		}

		switch( skill_num ) {
		case SM_BASH:			// バッシュ
		case MS_BASH:
			calc_flag.hitrate = calc_flag.hitrate*(100+5*skill_lv)/100;
			break;
		case KN_PIERCE:			// ピアース
		case ML_PIERCE:
			calc_flag.hitrate = calc_flag.hitrate*(100+5*skill_lv)/100;
			wd.div_ = t_size + 1;
			break;
		case SM_MAGNUM:			// マグナムブレイク
		case MS_MAGNUM:
			if(src_md && battle_config.monster_skill_over && skill_lv >= battle_config.monster_skill_over)
				calc_flag.hitrate = calc_flag.hitrate*10;
			else
				calc_flag.hitrate = calc_flag.hitrate*(100+10*skill_lv)/100;
			break;
		case KN_AUTOCOUNTER:		// オートカウンター
			wd.flag = (wd.flag&~BF_SKILLMASK)|BF_NORMAL;
			if(battle_config.pc_auto_counter_type&1)
				calc_flag.hitrate += 20;
			else
				calc_flag.hitrate = 1000000;
			break;
		case AS_SONICBLOW:		// ソニックブロー
			if(src_sd && pc_checkskill(src_sd,AS_SONICACCEL) > 0)
				calc_flag.hitrate = calc_flag.hitrate*150/100;
			break;
		case CR_SHIELDBOOMERANG:	// シールドブーメラン
			if(sc && sc->data[SC_CRUSADER].timer != -1)
				calc_flag.hitrate = 1000000;
			s_ele = s_ele_ = ELE_NEUTRAL;
			break;
		case CR_ACIDDEMONSTRATION:	// アシッドデモンストレーション
		case GN_FIRE_EXPANSION_ACID:	// ファイアーエクスパンション(塩酸)
		case NPC_CRITICALSLASH:		// 防御無視攻撃
		case NPC_GUIDEDATTACK:		// ガイデッドアタック
#ifdef PRE_RENEWAL
		case AM_ACIDTERROR:		// アシッドテラー
		case MO_INVESTIGATE:		// 発勁
#endif
		case KO_BAKURETSU:		// 爆裂苦無
			calc_flag.hitrate = 1000000;
			s_ele = s_ele_ = ELE_NEUTRAL;
			break;
		case MO_EXTREMITYFIST:		// 阿修羅覇鳳拳
		case NJ_ISSEN:			// 一閃
			calc_flag.hitrate = 1000000;
#ifdef PRE_RENEWAL
			s_ele = s_ele_ = ELE_NEUTRAL;
#endif
			break;
		case HVAN_EXPLOSION:		// バイオエクスプロージョン
		case RG_BACKSTAP:		// バックスタブ
#ifdef PRE_RENEWAL
		case CR_GRANDCROSS:		// グランドクロス
		case NPC_GRANDDARKNESS:		// グランドダークネス
		case AM_DEMONSTRATION:		// デモンストレーション
#endif
		case TK_COUNTER:		// アプチャオルリギ
		case AS_SPLASHER:		// ベナムスプラッシャー
#ifndef PRE_RENEWAL
		case MO_INVESTIGATE:		// 発勁
		case GS_MAGICALBULLET:	// マジカルバレット
		case ASC_BREAKER:		// ソウルブレイカー
#endif
		case NPC_EARTHQUAKE:	// アースクエイク
		case NPC_EXPULSION:		// エクスパルシオン
		case NPC_VENOMFOG:		// ベナムフォグ
		case RK_DRAGONBREATH:	// ファイアードラゴンブレス
		case RK_DRAGONBREATH_WATER:	// ウォータードラゴンブレス
		case GC_PHANTOMMENACE:		// ファントムメナス
		case RA_SENSITIVEKEEN:		// 鋭敏な嗅覚
		case NC_SELFDESTRUCTION:	// セルフディストラクション
			calc_flag.hitrate = 1000000;
			break;
#ifndef PRE_RENEWAL
		case CR_GRANDCROSS:		// グランドクロス
		case NPC_GRANDDARKNESS:		// グランドダークネス
		case AM_DEMONSTRATION:		// デモンストレーション
		case AM_ACIDTERROR:		// アシッドテラー
			if(src_sd) s_ele = s_ele_ = src_sd->atk_ele;
			e = status_get_attack_element_nw(src);
			if(e != ELE_NEUTRAL) {
				// 武器に属性が付与されているなら、付与属性に変更
				s_ele = s_ele_ = e;
			}
			calc_flag.hitrate = 1000000;
			break;
#endif
		case GS_TRACKING:		// トラッキング
			calc_flag.hitrate = calc_flag.hitrate*4+5;
			break;
		case MO_FINGEROFFENSIVE:	// 指弾
			if(src_sd && battle_config.finger_offensive_type == 0) {
				wd.div_ = src_sd->spiritball.old;
			} else {
				wd.div_ = 1;
			}
			break;
		case AS_VENOMKNIFE:		// ベナムナイフ
			if(src_sd && !src_sd->state.arrow_atk && src_sd->arrow_atk > 0) {
				if(src_sd->arrow_ele > ELE_NEUTRAL)	// 属性矢なら属性を矢の属性に変更
					s_ele = src_sd->arrow_ele;
			}
			break;
		case NPC_COMBOATTACK:		// 多段攻撃
		case NPC_RANDOMATTACK:		// ランダムATK攻撃
		case NPC_RANGEATTACK:		// 遠距離攻撃
		case NJ_ZENYNAGE:		// 銭投げ
		case NPC_CRITICALWOUND:		// 致命傷攻撃
		case KO_MUCHANAGE:			// 無茶投げ
		case RL_HAMMER_OF_GOD:	// ハンマーオブゴッド
		case RL_D_TAIL:			// ドラゴンテイル
			s_ele = s_ele_ = ELE_NEUTRAL;
			break;
		case PA_SHIELDCHAIN:		// シールドチェイン
			calc_flag.hitrate += 20;
#ifdef PRE_RENEWAL
			s_ele = s_ele_ = ELE_NEUTRAL;
#endif
			break;
		case NPC_PIERCINGATT:		// 突き刺し攻撃
		case CR_SHIELDCHARGE:		// シールドチャージ
			wd.flag = (wd.flag&~BF_RANGEMASK)|BF_SHORT;
			s_ele = s_ele_ = ELE_NEUTRAL;
			break;
		case BA_MUSICALSTRIKE:		// ミュージカルストライク
		case DC_THROWARROW:		// 矢撃ち
		case CG_ARROWVULCAN:		// アローバルカン
			if(src_sd)
				s_ele = src_sd->arrow_ele;
			break;
		case NJ_SYURIKEN:		// 手裏剣投げ
		case NJ_KUNAI:			// 苦無投げ
		case SS_FUUMASHOUAKU:	// 風魔手裏剣 -掌握-
		case SS_FUUMAKOUCHIKU:	// 風魔手裏剣 -構築-
		case SS_KUNAIWAIKYOKU:	// 苦無 -歪曲-
		case SS_KUNAIKAITEN:	// 苦無 -回転-
		case SS_KUNAIKUSSETSU:	// 苦無 -屈折-
			if(src_sd && src_sd->arrow_ele > ELE_NEUTRAL)	// 属性矢なら属性を矢の属性に変更
				s_ele = src_sd->arrow_ele;
			break;
#ifndef PRE_RENEWAL
		case NJ_TATAMIGAESHI:	// 畳返し
			wd.flag = (wd.flag&~BF_RANGEMASK)|BF_SHORT;
			break;
#endif
		case MH_STAHL_HORN:		// シュタールホーン
			if(sc && sc->data[SC_GOLDENE_FERSE].timer != -1) {
				if(atn_rand()%100 < sc->data[SC_GOLDENE_FERSE].val1 * 20)
					s_ele = s_ele_ = ELE_HOLY;
			}
			break;
		case MH_SONIC_CRAW:	// ソニッククロー
			if(src_hd) {
				wd.div_ = src_hd->spiritball;
			} else {
				wd.div_ = 1;
			}
			break;
		case NPC_DRAGONBREATH:	/* Mドラゴンブレス */
			calc_flag.hitrate = 1000000;
			if(skill_lv < 6)
				s_ele = s_ele_ = ELE_FIRE;
			else
				s_ele = s_ele_ = ELE_WATER;
			break;
		case RK_SONICWAVE:		// ソニックウェーブ
			calc_flag.hitrate += 7 * skill_lv;
			break;
		case GC_VENOMPRESSURE:	// ベナムプレッシャー
			calc_flag.hitrate += 10 + skill_lv * 4;
			break;
		case RA_WUGDASH:		// ウォーグダッシュ
		case RA_WUGSTRIKE:		// ウォーグストライク
		case RA_WUGBITE:		// ウォーグバイト
			s_ele = s_ele_ = ELE_NEUTRAL;
			break;
		case NC_ARMSCANNON:
		case GN_CARTCANNON:		// カートキャノン
		case KO_HAPPOKUNAI:		// 八方苦無
			calc_flag.hitrate = 1000000;
			if(src_sd && src_sd->arrow_ele > ELE_NEUTRAL)	// 属性矢なら属性を矢の属性に変更
				s_ele = src_sd->arrow_ele;
			break;
		case SC_FATALMENACE:	// フェイタルメナス
			if(skill_lv > 5)
				calc_flag.hitrate += (skill_lv - 6) * 5;
			else
				calc_flag.hitrate -= 5 + (6 - skill_lv) * 5;
			break;
		case LG_BANISHINGPOINT:		// バニシングポイント
			calc_flag.hitrate = calc_flag.hitrate*(100+3*skill_lv)/100;
			break;
		case LG_HESPERUSLIT:	// ヘスペルスリット
			if(sc) {
				if(sc->data[SC_BANDING].timer != -1 && sc->data[SC_BANDING].val2 > 0) {
					wd.div_ = sc->data[SC_BANDING].val2;
				}
				if(sc->data[SC_INSPIRATION].timer != -1) {
					wd.div_ += 2;
				}
			}
			if(wd.div_ == 5)
				s_ele = ELE_HOLY;
			break;
		case SR_GATEOFHELL:	// 羅刹破凰撃
			if(skill_lv >= 5)
				wd.flag = (wd.flag&~BF_RANGEMASK)|BF_LONG;
			break;
		case MC_CARTREVOLUTION:		// カートレボリューション
#ifndef PRE_RENEWAL
			calc_flag.hitrate = 1000000;
#endif
		case GN_CART_TORNADO:		// カートトルネード
			if(src_sd && (skill = pc_checkskill(src_sd,GN_REMODELING_CART)) > 0)
				calc_flag.hitrate = calc_flag.hitrate+skill*4;
			break;
		case RL_SLUGSHOT:		// スラッグショット
			{
				int dist = unit_distance(src,target)-1;
				if(dist > 3)
					calc_flag.hitrate = calc_flag.hitrate*(100-((11 - skill_lv) * (dist-3)))/100;
				s_ele = s_ele_ = ELE_NEUTRAL;
			}
			break;
		case RL_H_MINE:		// ハウリングマイン(追撃)
			if(wflag&1)
				s_ele = s_ele_ = ELE_FIRE;
			break;
		case DK_HACKANDSLASHER:			// ハックアンドスラッシャー
		case DK_HACKANDSLASHER_ATK:		// ハックアンドスラッシャー(攻撃)
			if(src_sd && src_sd->status.weapon == WT_2HSWORD) {
				wd.flag = (wd.flag&~BF_RANGEMASK)|BF_SHORT;
			} else if(src_sd && src_sd->status.weapon == WT_2HSPEAR) {
				wd.flag = (wd.flag&~BF_RANGEMASK)|BF_LONG;
			}
			break;
		case CD_PETITIO:	// ペティティオ
			if(src_sd && src_sd->status.weapon == WT_MACE) {
				wd.flag = (wd.flag&~BF_RANGEMASK)|BF_LONG;
			} else if(src_sd && src_sd->status.weapon == WT_BOOK) {
				wd.flag = (wd.flag&~BF_RANGEMASK)|BF_SHORT;
			}
			break;
		case NW_BASIC_GRENADE:			/* ベーシックグレネード */
		case NW_HASTY_FIRE_IN_THE_HOLE:	/* ヘイスティファイアインザホール */
		case NW_GRENADES_DROPPING:		/* グレネーズドロッピング */
			if(sc) {
				if(sc->data[SC_GRENADE_FRAGMENT_1].timer != -1)
					s_ele = s_ele_ = ELE_WATER;
				else if(sc->data[SC_GRENADE_FRAGMENT_2].timer != -1)
					s_ele = s_ele_ = ELE_WIND;
				else if(sc->data[SC_GRENADE_FRAGMENT_3].timer != -1)
					s_ele = s_ele_ = ELE_EARTH;
				else if(sc->data[SC_GRENADE_FRAGMENT_4].timer != -1)
					s_ele = s_ele_ = ELE_FIRE;
				else if(sc->data[SC_GRENADE_FRAGMENT_5].timer != -1)
					s_ele = s_ele_ = ELE_DARK;
				else if(sc->data[SC_GRENADE_FRAGMENT_6].timer != -1)
					s_ele = s_ele_ = ELE_HOLY;
			}
			break;
		}
	}

	// サクリファイス
	if(sc && sc->data[SC_SACRIFICE].timer != -1 && !skill_num && t_class != MOBID_EMPERIUM) {
		calc_flag.hitrate = 1000000;
		s_ele = s_ele_ = ELE_NEUTRAL;
	}
	// 太陽と月と星の融合
	if(sc && sc->data[SC_FUSION].timer != -1) {
		calc_flag.hitrate = 1000000;
	}
	// スペルフィスト
	if(sc && sc->data[SC_SPELLFIST].timer != -1 && !skill_num && t_class != MOBID_EMPERIUM) {
		calc_flag.hitrate = 1000000;
	}
	// カード効果による必中ボーナス
	if(src_sd && src_sd->perfect_hit > 0) {
		if(atn_rand()%100 < src_sd->perfect_hit)
			calc_flag.hitrate = 1000000;
	}
	// 対象が状態異常中の場合の必中ボーナス
	if(calc_flag.hitrate < 1000000 && t_sc) {
		if( t_sc->data[SC_SLEEP].timer != -1 ||
		    t_sc->data[SC_STUN].timer != -1 ||
		    t_sc->data[SC_FREEZE].timer != -1 ||
		    (t_sc->data[SC_STONE].timer != -1 && t_sc->data[SC_STONE].val2 == 0) ||
		    t_sc->data[SC_DEEP_SLEEP].timer != -1 ) {
			calc_flag.hitrate = 1000000;
		}
	}
	if(calc_flag.hitrate < battle_config.min_hitrate)
		calc_flag.hitrate = battle_config.min_hitrate;

	/* ８．回避判定 */
	if(wd.type == 0 && atn_rand()%100 >= calc_flag.hitrate) {
		wd.dmg_lv = ATK_FLEE;
	}
	else if(wd.type == 0 && t_sc && t_sc->data[SC_UTSUSEMI].timer != -1) {	// 空蝉
		wd.dmg_lv = ATK_FLEE;
		clif_misceffect2(target,463);
		if(--t_sc->data[SC_UTSUSEMI].val3 == 0)
			status_change_end(target,SC_UTSUSEMI,-1);
		if(t_sc->data[SC_ANKLE].timer == -1) {
			int dir = 0, head_dir = 0;
			int count = skill_get_blewcount(NJ_UTSUSEMI,t_sc->data[SC_UTSUSEMI].val1);
			if(target_sd) {
				dir = target_sd->dir;
				head_dir = target_sd->head_dir;
			}
			unit_stop_walking(target,1);
			skill_blown(src,target,count|SAB_NODAMAGE|SAB_NOPATHSTOP);
			if(target_sd)
				pc_setdir(target_sd, dir, head_dir);
			if(t_sc->data[SC_CLOSECONFINE].timer != -1)
				status_change_end(target,SC_CLOSECONFINE,-1);
		}
	}
	else if(wd.type == 0 && t_sc && t_sc->data[SC_BUNSINJYUTSU].timer != -1) {	// 影分身
		wd.dmg_lv = ATK_FLEE;
		if(--t_sc->data[SC_BUNSINJYUTSU].val3 == 0)
			status_change_end(target,SC_BUNSINJYUTSU,-1);
	}
	else if(target_sd && t_sc && t_sc->data[SC_DODGE].timer != -1 && (wd.flag&BF_LONG || t_sc->data[SC_SPURT].timer != -1) && atn_rand()%100 < 20) {	// 落法
		int slv = pc_checkskill(target_sd,TK_DODGE);
		wd.dmg_lv = ATK_FLEE;
		clif_skill_nodamage(&target_sd->bl,&target_sd->bl,TK_DODGE,slv,1);
		status_change_start(&target_sd->bl,SC_DODGE_DELAY,slv,src->id,0,0,skill_get_time(TK_DODGE,slv),0);
	}
	else if(t_sc && (t_sc->data[SC_FOGWALL].timer != -1 || t_sc->data[SC_FOGWALLPENALTY].timer != -1) && wd.flag&BF_LONG && !skill_num && atn_rand()%100 < 75) {	// ウォールオブフォグ　遠距離通常攻撃は75%の確率でミス
		wd.dmg_lv = ATK_FLEE;	// 通常回避
	}

	else {
		atn_bignumber damage_ot = 0, damage_ot2 = 0;
#ifdef PRE_RENEWAL
		atn_bignumber tk_power_damage = 0, tk_power_damage2 = 0;
#endif

		// 回避できなかったときのみstep9〜18のダメージ計算を行う
		wd.dmg_lv = ATK_DEF;

		/* ９．基本ダメージの算出 */
		wd.damage = battle_calc_base_damage(src, target, skill_num, wd.type, 0);
#ifdef PRE_RENEWAL
		if(calc_flag.lh)
			wd.damage2 = battle_calc_base_damage(src, target, skill_num, wd.type, 1);
#else
		if(calc_flag.lh)
			wd.damage2 = battle_calc_base_damage(src, target, skill_num, 0, 1);
#endif

		// ゴールデンペルジェ状態
		if(sc && sc->data[SC_GOLDENE_FERSE].timer != -1 && (!skill_num || skill_num == MH_STAHL_HORN)) {
			if(atn_rand()%100 < sc->data[SC_GOLDENE_FERSE].val1 * 20) {
				atn_bignumber bonus_damage;
				bonus_damage = battle_attr_fix(wd.damage, ELE_HOLY, status_get_element(target));
				if(bonus_damage > 0)
					wd.damage += bonus_damage * sc->data[SC_GOLDENE_FERSE].val4 / 100;
			}
		}
#ifdef PRE_RENEWAL
		if(wd.type == 0) {	// クリティカルでないとき矢のダメージを加算
			if(src_sd && src_sd->state.arrow_atk && src_sd->arrow_atk > 0)
				wd.damage += atn_rand()%(src_sd->arrow_atk+1);
		}
#else
		// （RE）マグナムブレイク状態
		if(sc && sc->data[SC_MAGNUM].timer != -1) {
			int rate = 0;
			if(sc->data[SC_MAGNUM].val2 == ELE_FIRE && skill_num != GN_CART_TORNADO) {
				rate = 20;
			} else if(sc->data[SC_MAGNUM].val2 == ELE_POISON) {
				switch(skill_num) {
				case 0:
				case SM_BASH:
				case SM_MAGNUM:
				case MS_MAGNUM:
				case TF_POISON:
				case AS_SONICBLOW:
				case AS_GRIMTOOTH:
				case ASC_BREAKER:
				case GC_CROSSIMPACT:
				case GC_DARKILLUSION:
				case GC_VENOMPRESSURE:
				case GC_COUNTERSLASH:
				case GC_PHANTOMMENACE:
				case GC_ROLLINGCUTTER:
				case GC_CROSSRIPPERSLASHER:
					rate = 25;
					break;
				}
			}
			if(rate > 0) {
				atn_bignumber bonus_damage;
				bonus_damage = battle_attr_fix(wd.damage, sc->data[SC_MAGNUM].val2, status_get_element(target));
				if(bonus_damage > 0)
					wd.damage += bonus_damage * rate / 100;
				if(calc_flag.lh) {
					bonus_damage = battle_attr_fix(wd.damage2, sc->data[SC_MAGNUM].val2, status_get_element(target));
					if(bonus_damage > 0)
						wd.damage2 += bonus_damage * rate / 100;
				}
			}
		}

		// （RE）土符：剛塊
		if(src_sd && src_sd->elementball.num && src_sd->elementball.ele == ELE_EARTH) {
			wd.damage += (wd.damage * src_sd->elementball.num * 10) / 100;
			if(calc_flag.lh)
				wd.damage2 += (wd.damage2 * src_sd->elementball.num * 10) / 100;
		}

		if(wd.flag&BF_LONG) {
			if(src_sd && pc_checkskill(src_sd, SU_POWEROFLIFE) > 0 && pc_checkskill_summoner(src_sd, SU_POWEROFLIFE) >= 20) {
				wd.damage += wd.damage * 10 / 100;
				if(calc_flag.lh)
					wd.damage2 += wd.damage2 * 10 / 100;
			}
		}

		/* （RE）防御無視判定および錐効果ダメージ計算 */
		switch (skill_num) {
		case MC_CARTREVOLUTION:
		case KN_AUTOCOUNTER:
		case CR_GRANDCROSS:
		case NPC_GRANDDARKNESS:
		case NPC_EARTHQUAKE:
		case MO_EXTREMITYFIST:
		case AM_DEMONSTRATION:
		case AM_ACIDTERROR:
		case CR_ACIDDEMONSTRATION:
		case ASC_BREAKER:
		case GS_FLING:
		case NJ_ZENYNAGE:
		case GN_FIRE_EXPANSION_ACID:
		case KO_MUCHANAGE:
			break;
		case MO_INVESTIGATE:
			wd.damage += t_def1 / 2;
			break;
		default:
			if( skill_num == WS_CARTTERMINATION && !battle_config.def_ratio_atk_to_carttermination )
				break;
			if( skill_num == PA_SHIELDCHAIN && !battle_config.def_ratio_atk_to_shieldchain )
				break;
			if(src_sd && t_def1 < 1000000)
			{
				// bIgnoreDef系判定
				ignored_rate  = ignored_rate  - src_sd->ignore_def_ele[t_ele]  - src_sd->ignore_def_race[t_race]  - src_sd->ignore_def_enemy[t_enemy];
				ignored_rate_ = ignored_rate_ - src_sd->ignore_def_ele_[t_ele] - src_sd->ignore_def_race_[t_race] - src_sd->ignore_def_enemy_[t_enemy];
				if(t_mode & MD_BOSS) {
					ignored_rate  -= src_sd->ignore_def_race[RCT_BOSS];
					ignored_rate_ -= src_sd->ignore_def_race_[RCT_BOSS];
				} else {
					ignored_rate  -= src_sd->ignore_def_race[RCT_NONBOSS];
					ignored_rate_ -= src_sd->ignore_def_race_[RCT_NONBOSS];
				}

				if(battle_config.left_cardfix_to_right) {
					// 左手カード補正設定あり
					ignored_rate -= 100 - ignored_rate_;
					ignored_rate_ = ignored_rate;
				}

				// bDefRatioATK系判定
				if(skill_num != AM_ACIDTERROR && ignored_rate > 0) {
					int def_ratio = 0;
					int mask = (1<<t_race) | ( (t_mode&MD_BOSS)? (1<<RCT_BOSS): (1<<RCT_NONBOSS));
					int def_fix = t_def1 * ignored_rate / 100;

					if(src_sd->def_ratio_atk_ele & (1<<t_ele) || src_sd->def_ratio_atk_race & mask || src_sd->def_ratio_atk_enemy & (1<<t_enemy) ||
					   src_sd->def_ratio_atk_ele_ & (1<<t_ele) || src_sd->def_ratio_atk_race_ & mask || src_sd->def_ratio_atk_enemy_ & (1<<t_enemy)
					)
						def_ratio = 1;

					// bIgnoreDef系分を加味して計算
					if(!calc_flag.idef && def_fix > 0 && def_ratio) {
						if(!calc_flag.idef_) {
							wd.damage  += def_fix / 2;
							wd.damage2 += def_fix / 2;
							calc_flag.idef  = 1;
							calc_flag.idef_ = 1;
						}
					}
					if(calc_flag.lh) {
						if(!calc_flag.idef_ && def_fix > 0 && def_ratio) {
							if(!calc_flag.idef) {
								wd.damage  += def_fix / 2;
								wd.damage2 += def_fix / 2;
								calc_flag.idef  = 1;
								calc_flag.idef_ = 1;
							}
						}
					}
				}
			}
			break;
		}

		// （RE）武器Atkの補正
		if(src_sd) {
			int trans_bonus = 0;

			wd.damage += src_sd->plus_atk;
			if(calc_flag.lh)
				wd.damage2 += src_sd->plus_atk;
			if(src_sd->state.arrow_atk) {
				wd.damage += src_sd->arrow_atk;
				if(calc_flag.lh)
					wd.damage2 += src_sd->arrow_atk;
			}

			if(src_sd->sc.data[SC_MONSTER_TRANSFORM].timer != -1 && (src_sd->sc.data[SC_MONSTER_TRANSFORM].val1 == 1276 || src_sd->sc.data[SC_MONSTER_TRANSFORM].val1 == 1884))
				trans_bonus += 25;
			if(src_sd->weapontype1 < WT_MAX && src_sd->weapontype2 < WT_MAX) {
				if(src_sd->atk_rate != 100 || src_sd->weapon_atk_rate[src_sd->weapontype1] != 0 || src_sd->weapon_atk_rate[src_sd->weapontype2] != 0 || trans_bonus) {
					wd.damage = (wd.damage * (src_sd->atk_rate + src_sd->weapon_atk_rate[src_sd->weapontype1] + trans_bonus)) / 100;
					if(calc_flag.lh)
						wd.damage2 = (wd.damage2 * (src_sd->atk_rate + src_sd->weapon_atk_rate[src_sd->weapontype2] + trans_bonus)) / 100;
				}
			}
		}
		if(skill_num == TF_POISON)
			wd.damage += 15*skill_lv;
#endif

#ifdef PRE_RENEWAL
		/* 10．ファイティング計算 */
		if(src_sd && (skill = pc_checkskill(src_sd,TK_POWER)) > 0 && src_sd->status.party_id > 0)
		{
			int member_num = party_check_same_map_member_count(src_sd);
			if(member_num > 0)
			{
				tk_power_damage = wd.damage*member_num * skill/50;
				if(calc_flag.lh)
					tk_power_damage2 = wd.damage2*member_num * skill/50;
			}
		}
#endif

		/* 11．オーバートラスト系のスキル倍率計算前の攻撃力確保 */
		damage_ot += wd.damage;
		if(calc_flag.lh)
			damage_ot2 += wd.damage2;

#ifndef PRE_RENEWAL
		/* （RE）カードによるダメージ追加処理 */
		if( src_sd && wd.damage > 0 && calc_flag.rh ) {
			if(!src_sd->state.arrow_atk) {	// 弓矢以外
				if(!battle_config.left_cardfix_to_right) {	// 左手カード補正設定無し
					wd.damage = wd.damage*(100+src_sd->addrace[t_race])/100;	// 種族によるダメージ修正
					wd.damage = wd.damage*(100+src_sd->addsize[t_size])/100;	// サイズによるダメージ修正
					wd.damage = wd.damage*(100+src_sd->addele[t_ele])/100;	// 属性によるダメージ修正
					wd.damage = wd.damage*(100+src_sd->addenemy[t_enemy])/100;	// 敵タイプによるダメージ修正
					wd.damage = wd.damage*(100+src_sd->addgroup[t_group])/100;	// グループによるダメージ修正
				} else {
					wd.damage = wd.damage*(100+src_sd->addrace[t_race]+src_sd->addrace_[t_race])/100;		// 種族によるダメージ修正(左手による追加あり)
					wd.damage = wd.damage*(100+src_sd->addsize[t_size]+src_sd->addsize_[t_size])/100;		// サイズによるダメージ修正(左手による追加あり)
					wd.damage = wd.damage*(100+src_sd->addele[t_ele]+src_sd->addele_[t_ele])/100;		// 属性によるダメージ修正(左手による追加あり)
					wd.damage = wd.damage*(100+src_sd->addenemy[t_enemy]+src_sd->addenemy_[t_enemy])/100;	// 敵タイプによるダメージ修正(左手による追加あり)
					wd.damage = wd.damage*(100+src_sd->addgroup[t_group]+src_sd->addgroup_[t_group])/100;	// グループによるダメージ修正(左手による追加あり)
				}
			} else { // 弓矢
				wd.damage = wd.damage*(100+src_sd->addrace[t_race]+src_sd->arrow_addrace[t_race])/100;	// 種族によるダメージ修正(弓矢による追加あり)
				wd.damage = wd.damage*(100+src_sd->addsize[t_size]+src_sd->arrow_addsize[t_size])/100;	// サイズによるダメージ修正(弓矢による追加あり)
				wd.damage = wd.damage*(100+src_sd->addele[t_ele]+src_sd->arrow_addele[t_ele])/100;		// 属性によるダメージ修正(弓矢による追加あり)
				wd.damage = wd.damage*(100+src_sd->addenemy[t_enemy]+src_sd->arrow_addenemy[t_enemy])/100;	// 敵タイプによるダメージ修正(弓矢による追加あり)
				wd.damage = wd.damage*(100+src_sd->addgroup[t_group]+src_sd->arrow_addgroup[t_group])/100;	// グループによるダメージ修正(弓矢による追加あり)
			}
			if(t_mode & MD_BOSS) {	// ボス
				if(!src_sd->state.arrow_atk) {	// 弓矢攻撃以外なら
					if(!battle_config.left_cardfix_to_right) {
						// 左手カード補正設定無し
						wd.damage = wd.damage*(100+src_sd->addrace[RCT_BOSS])/100;					// ボスモンスターに追加ダメージ
					} else {
						// 左手カード補正設定あり
						wd.damage = wd.damage*(100+src_sd->addrace[RCT_BOSS]+src_sd->addrace_[RCT_BOSS])/100;	// ボスモンスターに追加ダメージ(左手による追加あり)
					}
				} else {	// 弓矢攻撃
					wd.damage = wd.damage*(100+src_sd->addrace[RCT_BOSS]+src_sd->arrow_addrace[RCT_BOSS])/100;		// ボスモンスターに追加ダメージ(弓矢による追加あり)
				}
			} else {		// ボスじゃない
				if(!src_sd->state.arrow_atk) {	// 弓矢攻撃以外
					if(!battle_config.left_cardfix_to_right) {
						// 左手カード補正設定無し
						wd.damage = wd.damage*(100+src_sd->addrace[RCT_NONBOSS])/100;				// ボス以外モンスターに追加ダメージ
					} else {
						// 左手カード補正設定あり
						wd.damage = wd.damage*(100+src_sd->addrace[RCT_NONBOSS]+src_sd->addrace_[RCT_NONBOSS])/100;	// ボス以外モンスターに追加ダメージ(左手による追加あり)
					}
				} else {
					wd.damage = wd.damage*(100+src_sd->addrace[RCT_NONBOSS]+src_sd->arrow_addrace[RCT_NONBOSS])/100;	// ボス以外モンスターに追加ダメージ(弓矢による追加あり)
				}
			}
			// 特定Class用補正処理(少女の日記→ボンゴン用？)
			for(i=0; i<src_sd->add_damage_class_count; i++) {
				if(src_sd->add_damage_classid[i] == t_class) {
					wd.damage = wd.damage*(100+src_sd->add_damage_classrate[i])/100;
					break;
				}
			}
		}

		/* （RE）カードによる左手ダメージ追加処理 */
		if( src_sd && wd.damage2 > 0 && calc_flag.lh ) {
//			if(!battle_config.left_cardfix_to_right) {	// 左手カード補正設定無し
				wd.damage2 = wd.damage2*(100+src_sd->addrace[t_race]+src_sd->addrace_[t_race])/100;	// 種族によるダメージ修正左手
				wd.damage2 = wd.damage2*(100+src_sd->addsize[t_size]+src_sd->addsize_[t_size])/100;	// サイズによるダメージ修正左手
				wd.damage2 = wd.damage2*(100+src_sd->addele[t_ele]+src_sd->addele_[t_ele])/100;	// 属性によるダメージ修正左手
				wd.damage2 = wd.damage2*(100+src_sd->addenemy[t_enemy]+src_sd->addenemy_[t_enemy])/100;	// 敵タイプによるダメージ修正左手
				wd.damage2 = wd.damage2*(100+src_sd->addgroup[t_group]+src_sd->addgroup_[t_group])/100;	// グループによるダメージ修正左手
				if(t_mode & MD_BOSS)	// ボス
					wd.damage2 = wd.damage2*(100+src_sd->addrace[RCT_BOSS]+src_sd->addrace_[RCT_BOSS])/100;		// ボスモンスターに追加ダメージ左手
				else
					wd.damage2 = wd.damage2*(100+src_sd->addrace[RCT_NONBOSS]+src_sd->addrace_[RCT_NONBOSS])/100;	// ボス以外モンスターに追加ダメージ左手
//			}
			// 特定Class用補正処理左手(少女の日記→ボンゴン用？)
			for(i=0; i<src_sd->add_damage_class_count_; i++) {
				if(src_sd->add_damage_classid_[i] == t_class) {
					wd.damage2 = wd.damage2*(100+src_sd->add_damage_classrate_[i])/100;
					break;
				}
			}
		}

		/* （RE）カードによるダメージ減衰処理 */
		if( target_sd && (wd.damage > 0 || wd.damage2 > 0 || damage_sbr > 0) && skill_num != CR_GRANDCROSS && skill_num != NPC_GRANDDARKNESS && skill_num != NPC_CRITICALSLASH) {	// 対象がPCの場合
			int s_enemy = status_get_enemy_type(src);
			int s_size  = status_get_size(src);
			int s_group = status_get_group(src);
			int ele_type= status_get_elem_type(src);
			cardfix = 100;
			if (s_ele == ELE_NONE)
				cardfix = cardfix*(100-target_sd->subele[ELE_NEUTRAL])/100;	// 属性無しの耐性は無属性
			else
				cardfix = cardfix*(100-target_sd->subele[s_ele])/100;		// 属性によるダメージ耐性
			if (ele_type == ELE_NONE)
				cardfix = cardfix*(100-target_sd->def_eleenemy[ELE_NEUTRAL])/100;	// 属性無しの耐性は無属性
			else
				cardfix = cardfix*(100-target_sd->def_eleenemy[ele_type])/100;		// 敵属性によるダメージ耐性
			cardfix = cardfix*(100-target_sd->subenemy[s_enemy])/100;		// 敵タイプによるダメージ耐性
			cardfix = cardfix*(100-target_sd->subsize[s_size])/100;			// サイズによるダメージ耐性
			cardfix = cardfix*(100-target_sd->subgroup[s_group])/100;		// グループによるダメージ耐性

			if(status_get_mode(src) & MD_BOSS)
				cardfix = cardfix*(100-target_sd->subrace[RCT_BOSS])/100;	// ボスからの攻撃はダメージ減少
			else
				cardfix = cardfix*(100-target_sd->subrace[RCT_NONBOSS])/100;	// ボス以外からの攻撃はダメージ減少

			// 特定Class用補正処理左手(少女の日記→ボンゴン用？)
			for(i=0; i<target_sd->add_def_class_count; i++) {
				if(target_sd->add_def_classid[i] == status_get_class(src)) {
					cardfix = cardfix*(100-target_sd->add_def_classrate[i])/100;
					break;
				}
			}
			if(wd.flag&BF_LONG && !(src_md && (skill_num == AC_SHOWER || skill_num == SN_SHARPSHOOTING)) )
				cardfix = cardfix*(100-target_sd->long_attack_def_rate)/100;	// 遠距離攻撃はダメージ減少(ホルンCとか)
			if(wd.flag&BF_SHORT)
				cardfix = cardfix*(100-target_sd->near_attack_def_rate)/100;	// 近距離攻撃はダメージ減少(該当無し？)
			DMG_FIX( cardfix, 100 );	// カード補正によるダメージ減少

			damage_sbr = damage_sbr * cardfix / 100;	// カード補正によるソウルブレイカーの魔法ダメージ減少
		}

		// エウカリスティカ
		if(target_sd && (skill = pc_checkskill(target_sd,AB_EUCHARISTICA)) > 0) {
			if(s_ele == ELE_UNDEAD || s_ele == ELE_DARK)
				DMG_FIX( 100-skill, 100 );
		}

		// スパイダーウェブ
		if(t_sc && t_sc->data[SC_SPIDERWEB].timer != -1 && s_ele == ELE_FIRE) {
			wd.damage <<= 1;
			if(calc_flag.lh)
				wd.damage2 <<= 1;
			status_change_end(target, SC_SPIDERWEB, -1);
		}

		/* （RE）属性の適用 */
		if(skill_num != MO_EXTREMITYFIST && skill_num != NJ_ISSEN && skill_num != SJ_NOVAEXPLOSING) {
			wd.damage = battle_attr_fix(wd.damage, s_ele, status_get_element(target));
			if(calc_flag.lh)
				wd.damage2 = battle_attr_fix(wd.damage2, s_ele_, status_get_element(target));
		}

		/* （RE）属性補正 */
		if( (sc || t_sc) && (wd.damage > 0 || wd.damage2 > 0) && skill_num != MO_EXTREMITYFIST && skill_num != NJ_ISSEN && skill_num != SJ_NOVAEXPLOSING) {
			cardfix = 100;
			if(sc) {
				// ボルケーノ
				if(sc->data[SC_VOLCANO].timer != -1 && s_ele == ELE_FIRE)
					cardfix += sc->data[SC_VOLCANO].val4;
				// バイオレントゲイル
				if(sc->data[SC_VIOLENTGALE].timer != -1 && s_ele == ELE_WIND)
					cardfix += sc->data[SC_VIOLENTGALE].val4;
				// デリュージ
				if(sc->data[SC_DELUGE].timer != -1 && s_ele == ELE_FIRE)
					cardfix += sc->data[SC_DELUGE].val4;
			}
			if(t_sc) {
				// カイト
				if(t_sc->data[SC_KAITE].timer != -1)
					cardfix += 300;
				// ベナムインプレス
				if(t_sc->data[SC_VENOMIMPRESS].timer != -1 && s_ele == ELE_POISON)
					cardfix += t_sc->data[SC_VENOMIMPRESS].val2;
				// オラティオ
				if(t_sc->data[SC_ORATIO].timer != -1 && s_ele == ELE_HOLY)
					cardfix += t_sc->data[SC_ORATIO].val2;
				// 死霊憑依
				if(t_sc->data[SC_SOULCURSE].timer != -1 && s_ele == ELE_DARK)
					cardfix += t_sc->data[SC_SOULCURSE].val2;
				// 激流
				if(t_sc->data[SC_HANDICAPSTATE_LIGHTNINGSTRIKE].timer != -1 && s_ele == ELE_EARTH)
					cardfix += t_sc->data[SC_HANDICAPSTATE_LIGHTNINGSTRIKE].val2;
				// 強毒
				if(t_sc->data[SC_HANDICAPSTATE_DEADLYPOISON].timer != -1 && s_ele == ELE_POISON)
					cardfix += t_sc->data[SC_HANDICAPSTATE_DEADLYPOISON].val2;
				// クライマックスクエイク
				if(t_sc->data[SC_CLIMAX_EARTH].timer != -1 && s_ele == ELE_EARTH)
					cardfix += t_sc->data[SC_CLIMAX_EARTH].val2;
				// クライマックスブルーム
				if(t_sc->data[SC_CLIMAX_BLOOM].timer != -1 && s_ele == ELE_FIRE)
					cardfix += t_sc->data[SC_CLIMAX_BLOOM].val2;
				// ジャックフロストノヴァ
				if(t_sc->data[SC_MISTYFROST].timer != -1 && s_ele == ELE_WATER)
					cardfix += t_sc->data[SC_MISTYFROST].val2;
			}
			DMG_FIX( cardfix, 100 );
		}

		// エンチャントデッドリーポイズン
		if(sc && sc->data[SC_EDP].timer != -1) {
			int edp_rate;

			switch(skill_num) {
			case 0:
			case SM_BASH:
			case SM_MAGNUM:
			case MS_MAGNUM:
			case TF_POISON:
			case AS_SONICBLOW:
			case ASC_BREAKER:
			case NPC_EARTHQUAKE:
			case GC_CROSSIMPACT:
			case GC_DARKILLUSION:
			case GC_VENOMPRESSURE:
			case GC_COUNTERSLASH:
			case GC_PHANTOMMENACE:
			case GC_ROLLINGCUTTER:
			case GC_CROSSRIPPERSLASHER:
				if(map[src->m].flag.pk && target->type == BL_PC) {
					edp_rate = (150 + sc->data[SC_EDP].val1 * 50) * battle_config.pk_edp_down_rate / 100;
				} else if(map[src->m].flag.gvg) {
					edp_rate = (150 + sc->data[SC_EDP].val1 * 50) * battle_config.gvg_edp_down_rate / 100;
				} else if(map[src->m].flag.pvp) {
					edp_rate = (150 + sc->data[SC_EDP].val1 * 50) * battle_config.pvp_edp_down_rate / 100;
				} else {
					edp_rate = 150 + sc->data[SC_EDP].val1 * 50;
				}
				wd.damage = wd.damage * edp_rate / 100;
				if(calc_flag.lh)
					wd.damage2 = wd.damage2 * edp_rate / 100;
				break;
			}
		}

		// （RE）ステータスAtkを加算
		if(src_sd || (!src_sd && battle_config.enemy_str)) {
			int s_ele__ = ELE_NEUTRAL;		// 基本無属性
			if(sc && sc->data[SC_SEVENWIND].timer != -1) {	// 暖かい風はステータスAtkにもかかる
				s_ele__ = sc->data[SC_SEVENWIND].val3;
			}
			wd.damage += battle_attr_fix(status_get_baseatk(src), s_ele__, status_get_element(target));
			/* （RE）左手ダメージの計算処理 */
			if(calc_flag.lh) {
				wd.damage2 = wd.damage2 * 75 / 100;
				wd.damage2 += battle_attr_fix(status_get_baseatk(src)/2, s_ele__, status_get_element(target));
			}
		}

		if(src_sd) {
			// 星のかけら、気球の適用
			int hit_bonus  = (src_sd->spiritball.old>0? src_sd->spiritball.old * 3: src_sd->spiritball.num * 3) + src_sd->coin.num * 3 + src_sd->bonus_damage;
			int hit_damage = hit_bonus + src_sd->star + src_sd->ranker_weapon_bonus;

			if(skill_num == NJ_KUNAI || skill_num == KO_HAPPOKUNAI) {	// 苦無投げ、八方苦無
				if(src_sd->arrow_atk) {
					hit_damage += src_sd->arrow_atk;
				}
				wd.damage += hit_damage;
			} else if(skill_num != MO_INVESTIGATE && skill_num != MO_EXTREMITYFIST && skill_num != GS_MAGICALBULLET && skill_num != GS_PIERCINGSHOT && skill_num != NC_ARMSCANNON && skill_num != SR_RAMPAGEBLASTER) {
				wd.damage += hit_damage;
				if(calc_flag.lh)
					wd.damage2 += hit_bonus + src_sd->star_ + src_sd->ranker_weapon_bonus_;
			}

			switch(skill_num) {
			case MC_CARTREVOLUTION:
			case MO_INVESTIGATE:
			case MO_EXTREMITYFIST:
			case CR_GRANDCROSS:
			case NPC_GRANDDARKNESS:
			case NPC_EARTHQUAKE:
			case AM_DEMONSTRATION:
			case AM_ACIDTERROR:
			case PA_SHIELDCHAIN:
			case CR_ACIDDEMONSTRATION:
			case ASC_BREAKER:
			case GS_FLING:
			case NJ_ZENYNAGE:
			case RA_CLUSTERBOMB:
			case RA_FIRINGTRAP:
			case RA_ICEBOUNDTRAP:
			case GN_FIRE_EXPANSION_ACID:
			case KO_MUCHANAGE:
				break;
			case RA_WUGSTRIKE:
			case RA_WUGBITE:
				if((skill = pc_checkskill(src_sd,RA_TOOTHOFWUG)) > 0) {	// トゥースオブウォーグによるダメージ増加
					wd.damage += 10 * skill;
				}
				break;
			default:
				if(skill_lv < 0)
					break;
				wd.damage = battle_addmastery(src_sd,target,wd.damage,0);
				if(calc_flag.lh)
					wd.damage2 = battle_addmastery(src_sd,target,wd.damage2,1);
				break;
			}
		}

		/* （RE）修練系BUFF処理 */
		if(sc && skill_num != MO_EXTREMITYFIST) {
			// イムポシティオマヌス
			if(sc->data[SC_IMPOSITIO].timer != -1) {
				wd.damage += sc->data[SC_IMPOSITIO].val1*5;
				if(calc_flag.lh)
					wd.damage2 += sc->data[SC_IMPOSITIO].val1*5;
			}
			// 戦太鼓の響き
			if(sc->data[SC_DRUMBATTLE].timer != -1) {
				wd.damage += sc->data[SC_DRUMBATTLE].val2;
				if(calc_flag.lh)
					wd.damage2 += sc->data[SC_DRUMBATTLE].val2;
			}
			// ニーベルングの指輪
			if(sc->data[SC_NIBELUNGEN].timer != -1) {
				wd.damage += sc->data[SC_NIBELUNGEN].val2;
				if(calc_flag.lh)
					wd.damage2 += sc->data[SC_NIBELUNGEN].val2;
			}
			// カモフラージュ
			if(sc->data[SC_CAMOUFLAGE].timer != -1) {
				wd.damage += 300 - (10 - sc->data[SC_CAMOUFLAGE].val2) * 30;
				if(calc_flag.lh)
					wd.damage2 += 300 - (10 - sc->data[SC_CAMOUFLAGE].val2) * 30;
			}
			// カートブースト
			if(sc->data[SC_GN_CARTBOOST].timer != -1) {
				wd.damage += sc->data[SC_GN_CARTBOOST].val1 * 10;
				if(calc_flag.lh)
					wd.damage2 += sc->data[SC_GN_CARTBOOST].val1 * 10;
			}
			// プラチナムアルター
			if(sc->data[SC_P_ALTER].timer != -1) {
				wd.damage += sc->data[SC_P_ALTER].val2;
				if(calc_flag.lh)
					wd.damage2 += sc->data[SC_P_ALTER].val2;
			}
		}

		/* （RE）カードによるダメージ減衰処理２ */
		if( target_sd && (wd.damage > 0 || wd.damage2 > 0) && skill_num != CR_GRANDCROSS && skill_num != NPC_GRANDDARKNESS && skill_num != NPC_CRITICALSLASH && skill_num != NPC_EARTHQUAKE) {	// 対象がPCの場合
			int s_race  = status_get_race(src);
			cardfix = 100;
			if(t_sc && t_sc->data[SC_ANTI_M_BLAST].timer != -1)
				cardfix = cardfix*(100-target_sd->subrace[s_race]-t_sc->data[SC_ANTI_M_BLAST].val2)/100;			// 種族によるダメージ耐性
			else
				cardfix = cardfix*(100-target_sd->subrace[s_race])/100;			// 種族によるダメージ耐性
			DMG_FIX( cardfix, 100 );	// カード補正によるダメージ減少
		}

		// エウカリスティカ
		if(src_sd && (skill = pc_checkskill(src_sd,AB_EUCHARISTICA)) > 0) {
			if(t_ele == ELE_UNDEAD || t_ele == ELE_DARK) {
				wd.damage += wd.damage * skill / 100;
				if(calc_flag.lh)
					wd.damage2 += wd.damage2 * skill / 100;
			}
		}

		// （RE）クリティカルダメージ増加
		if(calc_flag.critical == 2) {
			if(src_sd) {
				int add_rate = src_sd->critical_damage;

				// クリティカル時ダメージ増加
				if(src_sd->sc.data[SC_MONSTER_TRANSFORM].timer != -1 && src_sd->sc.data[SC_MONSTER_TRANSFORM].val1 == 1002)
					add_rate += 5;

				if(skill_num == WH_HAWKRUSH) {			// ホークラッシュの場合はダメージ倍率を1/4に
					add_rate = add_rate/4;
				} else if(skill_num == WH_HAWKBOOMERANG) {		// ホークブーメランの場合はダメージ倍率を1.25倍に
					add_rate = add_rate * 125 / 100;
				} else if(skill_num != 0) {					// その他のスキル攻撃の場合はダメージ倍率を1/2に
					add_rate = add_rate/2;
				}
				wd.damage = wd.damage * (100 + add_rate) / 100;
			}
		}
#endif

		/* スキル倍率計算に加算 */
		if(sc && skill_num != NPC_EARTHQUAKE) {
#ifndef PRE_RENEWAL
			// オーバートラスト
			if(sc->data[SC_OVERTHRUST].timer != -1)
				add_rate += sc->data[SC_OVERTHRUST].val3;
			// オーバートラストマックス
			if(sc->data[SC_OVERTHRUSTMAX].timer != -1)
				add_rate += 20*sc->data[SC_OVERTHRUSTMAX].val1;
			// ブラッディラスト
			if(sc->data[SC__BLOODYLUST].timer != -1)
				add_rate += 32;
			// プロボック
			else if(sc->data[SC_PROVOKE].timer != -1)
				add_rate += 2 + 3 * sc->data[SC_PROVOKE].val1;
			// トゥルーサイト
			if(sc->data[SC_TRUESIGHT].timer != -1)
				add_rate += 2*sc->data[SC_TRUESIGHT].val1;
			// コンセントレーション
			if(sc->data[SC_CONCENTRATION].timer != -1)
				add_rate += 5*sc->data[SC_CONCENTRATION].val1;
			// ゴスペルのAtk2倍
			if(sc->data[SC_INCATK2].timer != -1)
				add_rate += sc->data[SC_INCATK2].val1;
			// バーサーク
			if(sc->data[SC_BERSERK].timer != -1)
				add_rate += 200;
			// 呪い
			if(sc->data[SC_CURSE].timer != -1)
				add_rate -= 25;
			// NPC爆裂波動
			if(sc->data[SC_EXPLOSIONSPIRITS].timer != -1 && !src_sd)
				add_rate += 200;
			// エスク
			if(sc->data[SC_SKE].timer != -1 && src_md)
				add_rate += 300;
#endif
			// ヒートバレル
			if(sc->data[SC_HEAT_BARREL].timer != -1)
				add_rate += sc->data[SC_HEAT_BARREL].val3;
		}
		// ドラゴノロジー
		if(src_sd && t_race == RCT_DRAGON && (skill = pc_checkskill(src_sd,SA_DRAGONOLOGY)) > 0)
			add_rate += skill*4;

#ifndef PRE_RENEWAL
		switch( skill_num ) {
		case NJ_ISSEN:		// 一閃
			{
				int maxhp = status_get_max_hp(src) / 100;
				int hp = status_get_hp(src);
				if(maxhp < hp) {
					wd.damage = wd.damage * skill_lv + hp;
					unit_heal(src,-(hp-maxhp),0,0,0);
					if(sc) {
						if(sc->data[SC_BUNSINJYUTSU].timer != -1 && sc->data[SC_BUNSINJYUTSU].val3 > 0) {
							wd.div_ = 2 + sc->data[SC_BUNSINJYUTSU].val3;
							wd.damage = wd.damage * (120+20*sc->data[SC_BUNSINJYUTSU].val3) / 100;
							status_change_end(src,SC_BUNSINJYUTSU,-1);
						}
						if(sc->data[SC_NEN].timer != -1)
							status_change_end(src,SC_NEN,-1);
					}
				}
				wd.damage = wd.damage - (t_def1 + t_def2);
			}
			break;
		case GS_MAGICALBULLET:	// マジカルバレット
			{
				static struct Damage mgd = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

				mgd = battle_calc_attack(BF_MAGIC,src,target,skill_num,skill_lv,wd.flag);
				wd.damage += mgd.damage;
				wd.damage = wd.damage - (t_def1 + t_def2);
			}
			break;
		case LK_SPIRALPIERCE:	// スパイラルピアース
		case ML_SPIRALPIERCE:
			{
				int weight = 0;
				if(src_sd && src_sd->equip_index[EQUIP_INDEX_RARM] >= 0) {
					int idx = src_sd->equip_index[EQUIP_INDEX_RARM];
					if(src_sd->inventory_data[idx] && itemdb_isweapon(src_sd->inventory_data[idx]->nameid)) {
						weight = src_sd->inventory_data[idx]->weight/10;
					}
				}
				DMG_FIX( 100+50*skill_lv, 100 );
				DMG_ADD( weight*(skill_lv*(skill_lv+2)*25)/100 );
				if(add_rate > 0)
					DMG_ADD( weight/2*skill_lv*add_rate/100 );
			}
			break;
		case PA_SHIELDCHAIN:	// シールドチェイン
			if(src_sd) {
				int idx  = src_sd->equip_index[EQUIP_INDEX_LARM];
				if(idx >= 0 && src_sd->inventory_data[idx] && itemdb_isarmor(src_sd->inventory_data[idx]->nameid)) {
					DMG_ADD( src_sd->inventory_data[idx]->weight/10 );
					if(src_sd->sc.data[SC_SHIELD_POWER].timer != -1) {
						DMG_FIX( 300+60*skill_lv, 100 );
						DMG_ADD( src_sd->status.inventory[idx].refine*(1200+skill_lv*240)/100 );
					} else {
						DMG_FIX( 100+30*skill_lv, 100 );
						DMG_ADD( src_sd->status.inventory[idx].refine*(400+skill_lv*120)/100 );
					}
				} else {
					DMG_FIX( 100+30*skill_lv, 100 );
				}
			} else {
				DMG_FIX( 100+30*skill_lv, 100 );
			}
			break;
		case AM_DEMONSTRATION:	// デモンストレーション
		case AM_ACIDTERROR:	// アシッドテラー
			{
				static struct Damage mgd = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
				int rate = 100+skill_lv*((skill_num==AM_ACIDTERROR)? 100: 20);

				mgd = battle_calc_attack(BF_MAGIC,src,target,skill_num,skill_lv,wd.flag);
				wd.damage += mgd.damage;
				wd.damage = wd.damage * rate / 100;
				wd.damage = wd.damage - (t_def1 + t_def2 + status_get_mdef(target) + status_get_mdef2(target));
				if(skill_num == AM_DEMONSTRATION) {
					int def_elem = status_get_element(target);
					int def_type = def_elem%20;
					int def_lv   = def_elem/20;

					if(def_type >= 0 && def_type < ELE_MAX && def_lv > 0 && def_lv <= MAX_ELE_LEVEL) {
						int r = attr_fix_table[def_lv-1][ELE_FIRE][def_type];
						wd.damage -= wd.damage * (100 - r) / 100;
					}
				} else if(t_mode&MD_BOSS) {
					wd.damage = wd.damage / 2;
				}
			}
			break;
		case ASC_BREAKER:	// ソウルブレイカー
			{
				static struct Damage sbr = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
				int rate = 300+50*skill_lv;

				if(sc && sc->data[SC_EDP].timer != -1)
					rate >>= 1;
				sbr = battle_calc_attack(BF_MAGIC,src,target,skill_num,skill_lv,wd.flag);
				wd.damage = wd.damage * (rate+add_rate) / 100;
				wd.damage += sbr.damage * rate / 100;
				wd.damage = wd.damage - (t_def1 + t_def2 + status_get_mdef(target) + status_get_mdef2(target));
			}
			break;
		case CR_ACIDDEMONSTRATION:	// アシッドデモンストレーション
		case GN_FIRE_EXPANSION_ACID:	// ファイアーエクスパンション(塩酸)
			{
				static struct Damage ads = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
				atn_bignumber dmg;
				ads = battle_calc_attack(BF_MAGIC,src,target,skill_num,skill_lv,wd.flag);
				wd.damage = wd.damage * (100+add_rate) / 100;
				wd.damage += ads.damage;
				dmg = (atn_bignumber)wd.damage * t_vit * 7 / 10;
				dmg = (dmg - ((t_def1 + t_def2)/2 + (status_get_mdef(target) + status_get_mdef2(target))/2))/10 * skill_lv;
				if(target->type != BL_MOB)
					dmg /= 2;
				DMG_SET( (int)dmg );
			}
			break;
		case NC_ARMSCANNON:	// アームズキャノン
			if(src_sd && src_sd->arrow_atk) {
				DMG_ADD( src_sd->arrow_atk );
			}
			DMG_FIX( (300 + (400 - 50 * t_size) * skill_lv) * status_get_lv(src) / 120, 100 );
			if(sc && sc->data[SC_ABR_DUAL_CANNON].timer != -1) {
				wd.div_ = 2;
			}
			break;
		case NPC_CRITICALSLASH:	// クリティカルスラッシュ
			if(src_md) {
				if(battle_config.monster_atk2_to_matk) {
					wd.damage = mobdb_search(src_md->class_)->atk1 + mobdb_search(src_md->class_)->atk2;
				}
				else {
					int int_ = mobdb_search(src_md->class_)->int_;

					wd.damage  = (mobdb_search(src_md->class_)->atk1 + mobdb_search(src_md->class_)->atk2) / 2;
					wd.damage += ((int_+(int_/5)*(int_/5)) + (int_+(int_/7)*(int_/7))) / 2;
				}
			}
		}

		if( src_sd && wd.damage > 0 && calc_flag.rh ) {
			// （RE）カード効果による特定レンジ攻撃のダメージ増幅
			if(wd.flag&BF_SHORT) {
				wd.damage = wd.damage * (100+src_sd->short_weapon_damege_rate) / 100;
			}
			if(wd.flag&BF_LONG) {
				wd.damage = wd.damage * (100+src_sd->long_weapon_damege_rate) / 100;
			}
		}
#endif

		// アンリミット
		if(sc && sc->data[SC_UNLIMIT].timer != -1 && wd.flag&BF_LONG && skill_num != HT_BLITZBEAT && skill_num != SN_FALCONASSAULT
		 && skill_num != RA_WUGDASH && skill_num != RA_WUGSTRIKE && skill_num != RA_WUGBITE)
			wd.damage += wd.damage * sc->data[SC_UNLIMIT].val2 / 100;

		/* 12．スキル修正１（攻撃力倍加系）*/
		switch( skill_num ) {
		case SM_BASH:		// バッシュ
		case MS_BASH:
			DMG_FIX( 100+30*skill_lv, 100 );
			break;
		case SM_MAGNUM:		// マグナムブレイク
		case MS_MAGNUM:
			if(src_md && battle_config.monster_skill_over && skill_lv >= battle_config.monster_skill_over) {
				DMG_FIX( 1000, 100 );
			} else {
				if(!wflag) {	// 内周
					DMG_FIX( 100+20*skill_lv, 100 );
				} else {	// 外周
					DMG_FIX( 100+10*skill_lv, 100 );
				}
			}
			break;
		case MC_MAMMONITE:	// メマーナイト
			DMG_FIX( 100+50*skill_lv, 100 );
			break;
		case AC_DOUBLE:		// ダブルストレイフィング
		case MA_DOUBLE:
			if(src_sd && !src_sd->state.arrow_atk && src_sd->arrow_atk > 0) {
				int arr = atn_rand()%(src_sd->arrow_atk+1);
				DMG_ADD( arr );
			}
			DMG_FIX( 90+10*skill_lv, 100 );
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
		case HT_FREEZINGTRAP:		// フリージングトラップ
			calc_flag.nocardfix = 1;
			break;
		case HT_POWER:		// ビーストストレイフィング
			if(src_sd && !src_sd->state.arrow_atk && src_sd->arrow_atk > 0) {
				int arr = atn_rand()%(src_sd->arrow_atk+1);
				DMG_ADD( arr );
			}
			DMG_FIX( 100+16*s_str, 100 );
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
		case AC_SHOWER:		// アローシャワー
		case MA_SHOWER:
			if(src_sd && !src_sd->state.arrow_atk && src_sd->arrow_atk > 0) {
				int arr = atn_rand()%(src_sd->arrow_atk+1);
				DMG_ADD( arr );
			}
#ifdef PRE_RENEWAL
			DMG_FIX( 75+5*skill_lv, 100 );
#else
			DMG_FIX( 150+10*skill_lv, 100 );
#endif
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			wd.blewcount = 0;
			break;
		case AC_CHARGEARROW:	// チャージアロー
		case MA_CHARGEARROW:
			if(src_sd && !src_sd->state.arrow_atk && src_sd->arrow_atk > 0) {
				int arr = atn_rand()%(src_sd->arrow_atk+1);
				DMG_ADD( arr );
			}
			DMG_FIX( 150, 100 );
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
		case HT_PHANTASMIC:	// ファンタズミックアロー
			if(src_sd && !src_sd->state.arrow_atk && src_sd->arrow_atk > 0) {
				int arr = atn_rand()%(src_sd->arrow_atk+1);
				DMG_ADD( arr );
			}
			DMG_FIX( 150, 100 );
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
		case KN_CHARGEATK:	// チャージアタック
			{
				int dist = unit_distance(src,target)-1;
				if(dist > 2)
					DMG_FIX( 100+100*(dist/3), 100 );
			}
			break;
		case AS_VENOMKNIFE:	// ベナムナイフ
			if(src_sd && !src_sd->state.arrow_atk && src_sd->arrow_atk > 0) {
				int arr = atn_rand()%(src_sd->arrow_atk+1);
				DMG_ADD( arr );
			}
			calc_flag.nocardfix = 1;
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
		case SG_SUN_WARM:	// 太陽の温もり
		case SG_MOON_WARM:	// 月の温もり
		case SG_STAR_WARM:	// 星の温もり
			if(src_sd) {
				if(src_sd->status.sp < 2) {
					status_change_end(src,SC_WARM,-1);
					break;
				}
				// 殴ったのでSP消費
				src_sd->status.sp -= 2;
				clif_updatestatus(src_sd,SP_SP);
			}
			wd.blewcount = 0;
			break;
		case KN_PIERCE:		// ピアース
		case ML_PIERCE:
			DMG_FIX( 100+10*skill_lv, 100 );
			break;
		case KN_SPEARSTAB:	// スピアスタブ
#ifdef PRE_RENEWAL
			DMG_FIX( 100+15*skill_lv, 100 );
#else
			DMG_FIX( 100+20*skill_lv, 100 );
#endif
			wd.blewcount = 0;
			break;
		case KN_SPEARBOOMERANG:	// スピアブーメラン
			DMG_FIX( 100+50*skill_lv, 100 );
			break;
		case KN_BRANDISHSPEAR:	// ブランディッシュスピア
		case ML_BRANDISH:
			{
				int rate = 1000+200*skill_lv;
				int bds = 0;
				if(wflag == 1) {
					if(skill_lv > 3)
						bds += rate/2;
					if(skill_lv > 6)
						bds += rate/4;
					if(skill_lv > 9)
						bds += rate/8;
				} else if(wflag == 2) {
					if(skill_lv > 6)
						bds += rate/2;
					if(skill_lv > 9)
						bds += rate/4;
				} else if(wflag == 3) {
					if(skill_lv > 3)
						bds += rate/2;
				}
				DMG_FIX( rate+bds, 1000 );
			}
			break;
		case KN_BOWLINGBASH:	// ボウリングバッシュ
		case MS_BOWLINGBASH:
			DMG_FIX( 100+40*skill_lv, 100 );
			wd.blewcount = 0;
			break;
		case AS_SONICBLOW:	// ソニックブロー
			{
#ifdef PRE_RENEWAL
				int rate = 300+50*skill_lv;
				if(src_sd && pc_checkskill(src_sd,AS_SONICACCEL) > 0)
					rate = rate*110/100;
#else
				int rate = 400+40*skill_lv;
				if(sc && sc->data[SC_EDP].timer != -1)
					rate >>= 1;
#endif
				if(sc && sc->data[SC_ASSASIN].timer != -1)
				{
					if(map[src->m].flag.gvg)
						rate = rate*125/100;
					else
						rate *= 2;
				}
				DMG_FIX( rate, 100 );
			}
			break;
		case AS_GRIMTOOTH:	// グリムトゥース
			DMG_FIX( 100+20*skill_lv, 100 );
			break;
		case TF_SPRINKLESAND:	// 砂まき
			DMG_FIX( 130, 100 );
			break;
		case MC_CARTREVOLUTION:	// カートレボリューション
#ifdef PRE_RENEWAL
			if(src_sd && src_sd->cart_max_weight > 0 && src_sd->cart_weight > 0) {
				DMG_FIX( 150 + pc_checkskill(src_sd,BS_WEAPONRESEARCH) + src_sd->cart_weight*100/src_sd->cart_max_weight, 100 );
			} else {
				DMG_FIX( 150, 100 );
			}
#endif
			wd.blewcount = 0;
			break;
		case NPC_COMBOATTACK:	// 多段攻撃
			DMG_FIX( 50, 100 );
			break;
		case NPC_RANDOMATTACK:	// ランダムATK攻撃
			DMG_FIX( 50+atn_rand()%150, 100 );
			break;
		case NPC_WATERATTACK:
		case NPC_GROUNDATTACK:
		case NPC_FIREATTACK:
		case NPC_WINDATTACK:
		case NPC_POISONATTACK:
		case NPC_HOLYATTACK:
		case NPC_DARKNESSATTACK:
		case NPC_TELEKINESISATTACK:
		case NPC_UNDEADATTACK:
			DMG_FIX( 25+75*skill_lv, 100 );
			break;
		case NPC_PIERCINGATT:
			DMG_FIX( 75, 100 );
			break;
		case RG_BACKSTAP:	// バックスタブ
			{
				int rate = 300+40*skill_lv;
				if(src_sd && src_sd->status.weapon == WT_BOW) {	// 弓なら半減
					rate /= 2;
				}
				DMG_FIX( rate, 100 );
			}
			break;
		case RG_RAID:		// サプライズアタック
#ifdef PRE_RENEWAL
			DMG_FIX( 100+40*skill_lv, 100 );
#else
			DMG_FIX( 100+80*skill_lv, 100 );
#endif
			break;
		case RG_INTIMIDATE:	// インティミデイト
			DMG_FIX( 100+30*skill_lv, 100 );
			break;
		case CR_SHIELDCHARGE:	// シールドチャージ
			DMG_FIX( 100+20*skill_lv, 100 );
			break;
#ifdef PRE_RENEWAL
		case CR_SHIELDBOOMERANG:	// シールドブーメラン
			if(src_sd && src_sd->equip_index[EQUIP_INDEX_LARM] >= 0) {
				int idx = src_sd->equip_index[EQUIP_INDEX_LARM];
				if(src_sd->inventory_data[idx] && itemdb_isarmor(src_sd->inventory_data[idx]->nameid)) {
					wd.damage += src_sd->inventory_data[idx]->weight/10;
					wd.damage += src_sd->status.inventory[idx].refine * status_get_overrefine_bonus(0);
				}
			}
			break;
#endif
		case CR_HOLYCROSS:	// ホーリークロス
		case NPC_DARKCROSS:	// ダーククロス
			DMG_FIX( 100+35*skill_lv, 100 );
			break;
		case CR_GRANDCROSS:	// グランドクロス
		case NPC_GRANDDARKNESS:	// グランドダークネス
			if (!battle_config.gx_cardfix)
				calc_flag.nocardfix = 1;
			break;
#ifdef PRE_RENEWAL
		case AM_DEMONSTRATION:	// デモンストレーション
			DMG_FIX( 100+20*skill_lv, 100 );
			calc_flag.nocardfix = 1;
			break;
		case AM_ACIDTERROR:	// アシッドテラー
			DMG_FIX( 100+40*skill_lv, 100 );
			calc_flag.nocardfix = 1;
			break;
#endif
		case MO_TRIPLEATTACK:	// 三段掌
			DMG_FIX( 100+20*skill_lv, 100 );
			break;
		case MO_FINGEROFFENSIVE:	// 指弾
#ifdef PRE_RENEWAL
			DMG_FIX( 100+50*skill_lv, 100 );
#else
			DMG_FIX( 125+25*skill_lv, 100 );
#endif
			break;
		case MO_INVESTIGATE:	// 発勁
#ifdef PRE_RENEWAL
			if(t_def1 < 1000000) {
				DMG_FIX( (100+75*skill_lv) * (t_def1 + t_def2), 100*50 );
			}
#else
			DMG_FIX( 100+75*skill_lv, 100 );
#endif
			break;
		case MO_BALKYOUNG:
			DMG_FIX( 300, 100 );
			break;
		case MO_EXTREMITYFIST:	// 阿修羅覇鳳拳
			if(src_sd) {
#ifdef PRE_RENEWAL
				DMG_FIX( 8+src_sd->status.sp/10, 1 );
#else
				wd.damage = wd.damage * (800+src_sd->status.sp*10) / 100;
#endif
				src_sd->status.sp = 0;
				clif_updatestatus(src_sd,SP_SP);
			} else {
				DMG_FIX( 8, 1 );
			}
			DMG_ADD( 250+150*skill_lv );
#ifndef PRE_RENEWAL
			wd.damage -= t_def1 + t_def2;
#endif
			break;
		case MO_CHAINCOMBO:	// 連打掌
#ifdef PRE_RENEWAL
			DMG_FIX( 150+50*skill_lv, 100 );
#else
			DMG_FIX( 250+50*skill_lv, 100 );
#endif
			break;
		case MO_COMBOFINISH:	// 猛龍拳
#ifdef PRE_RENEWAL
			DMG_FIX( 240+60*skill_lv, 100 );
#else
			DMG_FIX( 450+50*skill_lv, 100 );
#endif
			// PTには入っている
			// カウンターアタックの確率上昇
			if(src_sd && src_sd->status.party_id > 0) {
				struct party *pt = party_search(src_sd->status.party_id);
				if(pt != NULL)
				{
					struct map_session_data* psrc_sd = NULL;
					for(i=0; i<MAX_PARTY; i++)
					{
						psrc_sd = pt->member[i].sd;
						if(!psrc_sd || src_sd == psrc_sd)
							continue;
						if(src_sd->bl.m == psrc_sd->bl.m && pc_checkskill(psrc_sd,TK_COUNTER) > 0)
						{
							status_change_start(&psrc_sd->bl,SC_COUNTER_RATE_UP,1,0,0,0,battle_config.tk_counter_rate_up_keeptime,0);
						}
					}
				}
			}
			break;
		case TK_STORMKICK:	// 旋風蹴り
			DMG_FIX( 160+20*skill_lv, 100 );
			break;
		case TK_DOWNKICK:	// 下段蹴り
			DMG_FIX( 160+20*skill_lv, 100 );
			break;
		case TK_TURNKICK:	// 回転蹴り
			DMG_FIX( 190+30*skill_lv, 100 );
			wd.blewcount = 0;
			break;
		case TK_COUNTER:	// カウンター蹴り
			DMG_FIX( 190+30*skill_lv, 100 );
			// PTには入っている
			// 三段掌の確率上昇
			if(src_sd && src_sd->status.party_id > 0 && (skill = pc_checkskill(src_sd,SG_FRIEND)) > 0) {
				struct party *pt = party_search(src_sd->status.party_id);
				if(pt != NULL)
				{
					struct map_session_data* psrc_sd = NULL;
					for(i=0; i<MAX_PARTY; i++)
					{
						psrc_sd = pt->member[i].sd;
						if(!psrc_sd || src_sd == psrc_sd)
							continue;
						if(src_sd->bl.m == psrc_sd->bl.m && pc_checkskill(psrc_sd,MO_TRIPLEATTACK) > 0)
						{
							status_change_start(&psrc_sd->bl,SC_TRIPLEATTACK_RATE_UP,skill,0,0,0,battle_config.tripleattack_rate_up_keeptime,0);
						}
					}
				}
			}
			break;
		case BA_MUSICALSTRIKE:	// ミュージカルストライク
		case DC_THROWARROW:	// 矢撃ち
			DMG_FIX( 60+40*skill_lv, 100 );
			break;
		case CH_TIGERFIST:	// 伏虎拳
			DMG_FIX( 40+100*skill_lv, 100 );
			break;
		case CH_CHAINCRUSH:	// 連柱崩撃
			DMG_FIX( 400+100*skill_lv, 100 );
			break;
		case CH_PALMSTRIKE:	// 猛虎硬派山
			DMG_FIX( 200+100*skill_lv, 100 );
			break;
#ifdef PRE_RENEWAL
		case LK_SPIRALPIERCE:	// スパイラルピアース
		case ML_SPIRALPIERCE:
			DMG_FIX( 80+40*skill_lv, 100 );
			break;
#endif
		case LK_HEADCRUSH:	// ヘッドクラッシュ
			DMG_FIX( 100+40*skill_lv, 100 );
			break;
		case LK_JOINTBEAT:	// ジョイントビート
			DMG_FIX( 50+10*skill_lv, 100 );
			break;
		case ASC_METEORASSAULT:	// メテオアサルト
			DMG_FIX( 40+40*skill_lv, 100 );
			calc_flag.nocardfix = 1;
			break;
#ifdef PRE_RENEWAL
		case ASC_BREAKER:	// ソウルブレイカー
			DMG_FIX( skill_lv, 1 );
			calc_flag.nocardfix = 1;
			break;
#endif
		case SN_SHARPSHOOTING:	// シャープシューティング
		case MA_SHARPSHOOTING:
			DMG_FIX( 200+50*skill_lv, 100 );
			break;
		case CG_ARROWVULCAN:	// アローバルカン
			DMG_FIX( 200+100*skill_lv, 100 );
			break;
		case AS_SPLASHER:	// ベナムスプラッシャー
			if(src_sd) {
#ifdef PRE_RENEWAL
				DMG_FIX( 500+50*skill_lv+20*pc_checkskill(src_sd,AS_POISONREACT), 100 );
#else
				DMG_FIX( 500+75*skill_lv, 100 );
#endif
			} else {
#ifdef PRE_RENEWAL
				DMG_FIX( 500+50*skill_lv, 100 );
#else
				DMG_FIX( 500+75*skill_lv, 100 );
#endif
			}
			if(wflag > 1) {
				DMG_FIX( 1, wflag );
			}
			calc_flag.nocardfix = 1;
			break;
		case AS_POISONREACT:	// ポイズンリアクト（攻撃で反撃）
			wd.damage = wd.damage*(100+30*skill_lv)/100;
			//wd.damage2 = wd.damage2	// 左手には乗らない
			break;
		case TK_JUMPKICK:	// 飛び蹴り
			if(sc && (sc->data[SC_RUN].timer != -1 || sc->data[SC_DODGE_DELAY].timer != -1)) {
				DMG_FIX( 30 + (10+status_get_lv(src)/10)*skill_lv, 100 );
				if(sc->data[SC_DODGE_DELAY].timer != -1)
					status_change_end(src,SC_DODGE_DELAY,-1);
			} else {
				DMG_FIX( 30+10*skill_lv, 100 );
			}
			if(src_sd && sc && sc->data[SC_RUN].timer != -1 && sc->data[SC_SPURT].timer != -1) {
				// タイリギ中で且つスパート状態なら威力さらにアップ
				// 計算式不明なので適当
				DMG_ADD( 10*pc_checkskill(src_sd,TK_RUN) );
			}
			// ティオアプチャギによる対象のステータス異常解除
			if(target_sd && target_sd->status.class_ == PC_CLASS_SL)	// ソウルリンカーは無視
				break;
			if(t_sc && t_sc->data[SC_PRESERVE].timer != -1)		// プリザーブ中は切れない
				break;
			status_change_release(target,0x10);
			break;
#ifdef PRE_RENEWAL
		case PA_SHIELDCHAIN:	// シールドチェイン
			if(src_sd) {
				int s_dex = status_get_dex(src);
				int s_luk = status_get_luk(src);
				int idx   = src_sd->equip_index[EQUIP_INDEX_LARM];
				DMG_SET( s_str+(s_str/10)*(s_str/10)+(s_dex/5)+(s_luk/5) );
				if(idx >= 0 && src_sd->inventory_data[idx] && itemdb_isarmor(src_sd->inventory_data[idx]->nameid))
					DMG_ADD( src_sd->status.inventory[idx].refine*4 + src_sd->inventory_data[idx]->weight/10 );
			} else {
				DMG_FIX( 100+30*skill_lv, 100 );
			}
			break;
#endif
		case WS_CARTTERMINATION:	// カートターミネーション
			{
				int rate = (skill_lv >= 16)? 1: 16-skill_lv;
				if(src_sd && src_sd->cart_max_weight > 0 && src_sd->cart_weight > 0) {
#ifdef PRE_RENEWAL
					DMG_FIX( src_sd->cart_weight, rate*100 );
					DMG_FIX( 8000, src_sd->cart_max_weight );
#else
					rate = src_sd->cart_weight / 10 / rate;
					DMG_FIX( rate, 100 );
#endif
				} else if(!src_sd) {
					DMG_FIX( 80, rate );
				}
			}
			calc_flag.nocardfix = 1;
			break;
#ifdef PRE_RENEWAL
		case CR_ACIDDEMONSTRATION:	// アシッドデモンストレーション
		case GN_FIRE_EXPANSION_ACID:	// ファイアーエクスパンション(塩酸)
			{
				int s_int = status_get_int(src);
				atn_bignumber dmg = (atn_bignumber)s_int * s_int * t_vit * skill_lv * 7 / 10 / (s_int + t_vit);
				if(target->type != BL_MOB)
					dmg /= 2;
				DMG_SET( (int)dmg );
			}
			calc_flag.nocardfix = 1;
			break;
#endif
		case GS_FLING:	// フライング
			DMG_SET( status_get_jlv(src) );
			calc_flag.nocardfix = 1;
			break;
		case GS_TRIPLEACTION:	// トリプルアクション
			DMG_FIX( 150, 100 );
			break;
		case GS_BULLSEYE:	// ブルズアイ
			if(t_race == RCT_BRUTE || t_race == RCT_DEMIHUMAN || t_race == RCT_PLAYER_HUMAN || t_race == RCT_PLAYER_DORAM)
				DMG_FIX( 500, 100 );
			calc_flag.nocardfix = 1;
			break;
#ifdef PRE_RENEWAL
		case GS_MAGICALBULLET:	// マジカルバレット
			{
				int matk1 = status_get_matk1(src);
				int matk2 = status_get_matk2(src);
				if(matk1 > matk2)
					wd.damage += matk2+atn_rand()%(matk1-matk2+1);
				else
					wd.damage += matk2;
			}
			break;
#endif
		case GS_TRACKING:	// トラッキング
			if(src_sd && !src_sd->state.arrow_atk && src_sd->arrow_atk > 0) {
				int arr = atn_rand()%(src_sd->arrow_atk+1);
				DMG_ADD( arr );
			}
			DMG_FIX( 200+100*skill_lv, 100 );
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
		case GS_DISARM:		// ディスアーム
			if(src_sd && !src_sd->state.arrow_atk && src_sd->arrow_atk > 0) {
				int arr = atn_rand()%(src_sd->arrow_atk+1);
				DMG_ADD( arr );
			}
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
		case GS_PIERCINGSHOT:	// ピアーシングショット
			if(src_sd && !src_sd->state.arrow_atk && src_sd->arrow_atk > 0) {
				int arr = atn_rand()%(src_sd->arrow_atk+1);
				DMG_ADD( arr );
			}
#ifdef PRE_RENEWAL
			DMG_FIX( 100+20*skill_lv, 100 );
#else
			if(src_sd && src_sd->status.weapon == WT_RIFLE) {
				DMG_FIX( 250+30*skill_lv, 100 );
			} else {
				DMG_FIX( 200+20*skill_lv, 100 );
			}
#endif
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
		case GS_RAPIDSHOWER:	// ラピッドシャワー
			if(src_sd && !src_sd->state.arrow_atk && src_sd->arrow_atk > 0) {
				int arr = atn_rand()%(src_sd->arrow_atk+1);
				DMG_ADD( arr );
			}
			DMG_FIX( 500+50*skill_lv, 100 );
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
		case GS_DESPERADO:	// デスペラード
			if(src_sd && !src_sd->state.arrow_atk && src_sd->arrow_atk > 0) {
				int arr = atn_rand()%(src_sd->arrow_atk+1);
				DMG_ADD( arr );
			}
			DMG_FIX( 50+50*skill_lv, 100 );
			if(wflag&1)	// フォーリンエンジェル
				DMG_FIX( 200, 100 );
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
		case GS_DUST:		// ダスト
			if(src_sd && !src_sd->state.arrow_atk && src_sd->arrow_atk > 0) {
				int arr = atn_rand()%(src_sd->arrow_atk+1);
				DMG_ADD( arr );
			}
			DMG_FIX( 100+50*skill_lv, 100 );
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
		case GS_FULLBUSTER:	// フルバスター
			if(src_sd && !src_sd->state.arrow_atk && src_sd->arrow_atk > 0) {
				int arr = atn_rand()%(src_sd->arrow_atk+1);
				DMG_ADD( arr );
			}
			DMG_FIX( 300+100*skill_lv, 100 );
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
		case GS_SPREADATTACK:	// スプレッドアタック
			if(src_sd && !src_sd->state.arrow_atk && src_sd->arrow_atk > 0) {
				int arr = atn_rand()%(src_sd->arrow_atk+1);
				DMG_ADD( arr );
			}
#ifdef PRE_RENEWAL
			DMG_FIX( 80+20*skill_lv, 100 );
#else
			DMG_FIX( 200+30*skill_lv, 100 );
#endif
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
	case GS_GROUNDDRIFT:		// グラウンドドリフト
			DMG_FIX( 200+20*skill_lv, 100 );
			break;
#ifdef PRE_RENEWAL
		case NJ_SYURIKEN:	// 手裏剣投げ
		case NJ_KUNAI:		// 苦無投げ
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
#else
		case NJ_SYURIKEN:	// 手裏剣投げ
			if(src_sd) {
				skill = pc_checkskill(src_sd,NJ_TOBIDOUGU);
				DMG_ADD( skill * 3 );
				if(src_sd->arrow_atk) {
					DMG_ADD( src_sd->arrow_atk );
				}
			}
			DMG_FIX( 100 + skill_lv * 5, 100 );
			break;
		case NJ_KUNAI:		// 苦無投げ
			if(src_sd) {
				skill = pc_checkskill(src_sd,NJ_TOBIDOUGU);
				DMG_ADD( skill * 3 );
			}
			DMG_FIX( 100 * skill_lv, 100 );
			break;
#endif
		case NJ_HUUMA:		// 風魔手裏剣投げ
			{
#ifdef PRE_RENEWAL
				int rate = 150+150*skill_lv;
				if(wflag > 1)
					rate /= wflag;
#else
				int rate = 250*skill_lv-50;
				if(src_sd) {
					skill = pc_checkskill(src_sd,NJ_TOBIDOUGU);
					DMG_ADD( skill * 3 );
				}
#endif
				DMG_FIX( rate, 100 );
			}
			break;
		case NJ_ZENYNAGE:	// 銭投げ
			{
				int dmg = 0;
				if(src_sd) {
					dmg = src_sd->zenynage_damage;
					src_sd->zenynage_damage = 0;	// 撃ったらリセット
				} else {
					dmg = skill_get_zeny(NJ_ZENYNAGE,skill_lv)/2;
					if(dmg > 0)
						dmg += atn_rand()%dmg;
					else
						dmg = 500*skill_lv + atn_rand()%(500*skill_lv);
				}
				if(target->type == BL_PC || t_mode & MD_BOSS) {
					dmg /= 2;
				}
				DMG_SET( dmg );
				calc_flag.nocardfix = 1;
			}
			break;
		case NJ_TATAMIGAESHI:	// 畳返し
#ifdef PRE_RENEWAL
			DMG_FIX( 100+10*skill_lv, 100 );
#else
			DMG_FIX( 200+20*skill_lv, 100 );
			wd.damage = wd.damage - (t_def1 + t_def2);
#endif
			break;
		case NJ_KASUMIKIRI:	// 霞斬り
#ifdef PRE_RENEWAL
			DMG_FIX( 100+10*skill_lv, 100 );
#else
			DMG_FIX( 100+20*skill_lv, 100 );
#endif
			break;
		case NJ_KIRIKAGE:	// 影斬り
#ifdef PRE_RENEWAL
			DMG_FIX( skill_lv, 1 );
#else
			DMG_FIX( 50+150*skill_lv, 100 );
#endif
			break;
#ifdef PRE_RENEWAL
		case NJ_ISSEN:		// 一閃
			{
				int hp = status_get_hp(src);
				DMG_SET( (s_str*40)+(skill_lv*(hp-1)*8)/100 );
				unit_heal(src,-hp+1,0,0,0);
				if(sc && sc->data[SC_NEN].timer != -1)
					status_change_end(src,SC_NEN,-1);
			}
			break;
#endif
		case NPC_CRITICALSLASH:		// クリティカルスラッシュ
			DMG_FIX( 100, 100 );
			break;
		case NPC_FIREBREATH:		// ファイアブレス
		case NPC_ICEBREATH:		// アイスブレス
		case NPC_THUNDERBREATH:		// サンダーブレス
		case NPC_ACIDBREATH:		// アシッドブレス
		case NPC_DARKNESSBREATH:	// ダークネスブレス
		case NPC_HELLJUDGEMENT:		// ヘルジャッジメント
		case NPC_DANCINGBLADE_ATK:	// ダンシングブレイド(連鎖)
		case NPC_DARKPIERCING:		// ダークピアーシング
			DMG_FIX( 100*skill_lv, 100 );
			break;
		case NPC_PULSESTRIKE:		// パルスストライク
			DMG_FIX( 100*skill_lv, 100 );
			wd.blewcount = 0;
			break;
		case NPC_VAMPIRE_GIFT:		// ヴァンパイアリックタッチ
			DMG_FIX( 100 * ((skill_lv - 1) % 5 + 1), 100 );
			break;
		case NPC_VENOMFOG:			// ベナムフォグ
			if(src_md)
				wd.damage = mobdb_search(src_md->class_)->atk1;
			else
				wd.damage = status_get_atk(src);
			DMG_FIX( 100 * skill_lv, 100 );
			wd.damage = battle_attr_fix(wd.damage, s_ele, status_get_element(target));
			break;
		case NPC_REVERBERATION_ATK:	// M振動残響(攻撃)
			DMG_FIX( 400 + 200 * skill_lv, 100 );
			if(wflag > 1) {
				DMG_FIX( 1, wflag );
			}
			break;
		case NPC_IGNITIONBREAK:	// Mイグニッションブレイク
			{
				int dmg = 500 + 500 * skill_lv;
				int dist = unit_distance(src,target);
				if(dist > 3)			// 遠距離
					dmg -= 500;
				else if(dist > 1)		// 中距離
					dmg -= 250;
				DMG_FIX( dmg, 100 );
			}
			break;
		case NPC_ARROWSTORM:		// Mアローストーム
			DMG_FIX( 1000 + 1000 * (skill_lv >= 5), 100 );
			break;
		case NPC_DRAGONBREATH:	/* Mドラゴンブレス */
			if(skill_lv < 6) {
				DMG_FIX( 500 + 500 * skill_lv, 100 );
			}
			else {
				DMG_FIX( 500 + 500 * (skill_lv-5), 100 );
			}
			break;
		case NPC_PHANTOMTHRUST:	// Mファントムスラスト
			DMG_FIX( 100, 100 );
			break;
		case NPC_HELLJUDGEMENT2:		// デモニックヘルジャッジメント
			DMG_FIX( 1000*skill_lv, 100 );
			break;
		case HFLI_MOON:		// ムーンライト
			DMG_FIX( 110+110*skill_lv, 100 );
			break;
		case HFLI_SBR44:	// S.B.R.44
			if(src_hd) {
				atn_bignumber dmg = (atn_bignumber)( src_hd->intimate * skill_lv ) * ( 100-t_def1) / 100 + t_def2;
				DMG_SET( (int)dmg );
				src_hd->intimate = 200;
				if(battle_config.homun_skill_intimate_type)
					src_hd->status.intimate = 200;
				clif_send_homdata(src_hd->msd,1,src_hd->intimate/100);
			}
			break;
		case MH_NEEDLE_OF_PARALYZE:	// ニードルオブパラライズ
			DMG_FIX( 700+100*skill_lv, 100 );
			break;
		case MH_SONIC_CRAW:	// ソニッククロー
			DMG_FIX( 40*skill_lv * status_get_lv(src) / 150, 100 );
			break;
		case MH_SILVERVEIN_RUSH:	// シルバーベインラッシュ
			DMG_FIX( 150 * skill_lv * status_get_lv(src) / 100, 100 );
			break;
		case MH_MIDNIGHT_FRENZY:	// ミッドナイトフレンジ
			DMG_FIX( 300 * skill_lv * status_get_lv(src) / 150, 100 );
			break;
		case MH_STAHL_HORN:	// シュタールホーン
			DMG_FIX( 500 + 100 * skill_lv * status_get_lv(src) / 150, 100 );
			break;
		case MH_TINDER_BREAKER:	// ティンダーブレイカー
			DMG_FIX( 100 * skill_lv * status_get_lv(src) / 100, 100 );
			break;
		case MH_EQC:			// E.Q.C
			if(t_sc && t_sc->data[SC_EQC].timer != -1 && t_sc->data[SC_EQC].val2 > 0) {
				if(t_sc->data[SC_EQC].val2 < status_get_max_hp(src)) {
					DMG_SET( status_get_max_hp(src) );
				}
				else {
					DMG_SET( t_sc->data[SC_EQC].val2 - status_get_max_hp(src) );
				}
				t_sc->data[SC_EQC].val2 = 0;
			}
			break;
		case MH_LAVA_SLIDE:	// ラーヴァスライド
			DMG_FIX( (20 * skill_lv + 3 * status_get_lv(src)) * status_get_lv(src) / 120, 100 );
			break;
		case MH_MAGMA_FLOW:	// マグマフロー
			DMG_FIX( (100 * skill_lv + 3 * status_get_lv(src)) * status_get_lv(src) / 120, 100 );
			break;
		case MER_CRASH:		// クラッシュ
			DMG_FIX( 100+10*skill_lv, 100 );
			break;
		case RK_SONICWAVE:	// ソニックウェーブ
			DMG_FIX( (700 + 100 * skill_lv) * status_get_lv(src) / 100, 100 );
			break;
		case RK_HUNDREDSPEAR:	// ハンドレッドスピア
			{
				int dmg = 600 + 80 * skill_lv;
				if(sc && sc->data[SC_DRAGONIC_AURA].timer != -1)
					dmg = 700 + 200 * skill_lv;
				if(src_sd) {
					int idx = src_sd->equip_index[EQUIP_INDEX_RARM];
					if(idx >= 0 && src_sd->inventory_data[idx] && itemdb_isweapon(src_sd->inventory_data[idx]->nameid))
						dmg += 1000 - src_sd->inventory_data[idx]->weight/10;
				}
				DMG_FIX( (dmg * (status_get_lv(src)/2+50) / 100) + (((src_sd)? pc_checkskill(src_sd,LK_SPIRALPIERCE): 0) * 50), 100 );
			}
			break;
		case RK_WINDCUTTER:	// ウィンドカッター
			DMG_FIX( (100 + 50 * skill_lv) * status_get_lv(src) / 100, 100 );
			break;
		case RK_IGNITIONBREAK:	// イグニッションブレイク
			{
				int dmg = 300 * skill_lv;
				int dist = unit_distance(src,target);
				if(dist > 3)			// 遠距離
					dmg = 200 * skill_lv;
				else if(dist > 1)		// 中距離
					dmg = 250 * skill_lv;
				dmg = dmg * status_get_lv(src) / 100;
				if(s_ele == ELE_FIRE)	// 火属性武器装備時
					dmg += 100 * skill_lv;
				DMG_FIX( dmg, 100 );
			}
			break;
		case RK_DRAGONBREATH:	// ファイアードラゴンブレス
		case RK_DRAGONBREATH_WATER:	// ウォータードラゴンブレス
			{
				atn_bignumber dmg = (status_get_hp(src) / 50 + status_get_max_sp(src) / 4) * skill_lv;
				int aura = 0;
				if(src_sd) {
					dmg = dmg * ( 95 + pc_checkskill(src_sd,RK_DRAGONTRAINING) * 5 ) / 100;
					aura = pc_checkskill(src_sd,DK_DRAGONIC_AURA);
				}
				dmg *= status_get_lv(src) / 100;
				if(aura > 0)
				dmg *= (atn_bignumber)(1 + (pow((double)(status_get_pow(src) + status_get_patk(src)), 1.05555) / 100 * 250 / 300));
				DMG_SET( battle_attr_fix(dmg, s_ele, status_get_element(target)) );
			}
			break;
		case RK_STORMBLAST:		// ストームブラスト
			DMG_FIX( ((src_sd)? pc_checkskill(src_sd,RK_RUNEMASTERY): 1) * 100 + (status_get_int(src) / 8) * 100, 100 );
			break;
		case RK_PHANTOMTHRUST:	// ファントムスラスト
			DMG_FIX( (50 * skill_lv + ((src_sd)? pc_checkskill(src_sd,KN_SPEARMASTERY): 0) * 10) * status_get_lv(src) / 150, 100 );
			break;
		case GC_CROSSIMPACT:	// クロスインパクト
			if(sc && sc->data[SC_EDP].timer != -1) {
				DMG_FIX( (1000 + 100 * skill_lv) * status_get_lv(src) / 120 / 2, 100 );
			} else {
				DMG_FIX( (1000 + 100 * skill_lv) * status_get_lv(src) / 120, 100 );
			}
			break;
		case GC_DARKILLUSION:	// ダークイリュージョン
			DMG_FIX( 100, 100 );
			break;
		case GC_COUNTERSLASH:	// カウンタースラッシュ
			if(src_sd) {
				int rate = (300 + 150 * skill_lv) * status_get_lv(src) / 120 + status_get_agi(src) * 2 + status_get_jlv(src) * 4;
				if(sc && sc->data[SC_EDP].timer != -1)
					rate >>= 1;
				DMG_FIX( rate, 100 );
			} else {
				DMG_FIX( (300 + 150 * skill_lv) * status_get_lv(src) / 120 + status_get_agi(src) * 2, 100 );
			}
			break;
		case GC_VENOMPRESSURE:	// ベナムプレッシャー
			DMG_FIX( 1000, 100 );
			break;
		case GC_PHANTOMMENACE:	// ファントムメナス
			DMG_FIX( 300, 100 );
			break;
		case GC_ROLLINGCUTTER:	// ローリングカッター
			if(sc && sc->data[SC_EDP].timer != -1) {
				DMG_FIX( (50 + 50 * skill_lv) * status_get_lv(src) / 100 / 2, 100 );
			} else {
				DMG_FIX( (50 + 50 * skill_lv) * status_get_lv(src) / 100, 100 );
			}
			break;
		case GC_CROSSRIPPERSLASHER:	// クロスリッパースラッシャー
			{
				int dmg = (400 + 80 * skill_lv) * status_get_lv(src) / 100;
				if(sc && sc->data[SC_ROLLINGCUTTER].timer != -1) {
					dmg += sc->data[SC_ROLLINGCUTTER].val1 * status_get_agi(src);
				}
				if(sc && sc->data[SC_EDP].timer != -1) {
					dmg >>= 1;
				}
				DMG_FIX( dmg, 100 );
			}
			break;
		case GC_DARKCROW:	// ダーククロー
			DMG_FIX( 100 * skill_lv, 100 );
			break;
		case AB_DUPLELIGHT_MELEE:	// グレイアムライト
			DMG_FIX( 100 + 10 * skill_lv, 100 );
			break;
		case RA_CLUSTERBOMB:	// クラスターボム
			DMG_FIX( 200 + 100 * skill_lv, 100 );
			break;
		case RA_FIRINGTRAP:		// ファイアリングトラップ
		case RA_ICEBOUNDTRAP:	// アイスバウンドトラップ
			DMG_FIX( 100, 100 );
			break;
		case RA_ARROWSTORM:		// アローストーム
			DMG_FIX( (1000 + 80 * skill_lv) * status_get_lv(src) / 100, 100 );
			break;
		case RA_AIMEDBOLT:		// エイムドボルト
			wd.div_ = wflag;	// 矢消費判定時のHit数を使用
			DMG_FIX( ((500 + 50 * skill_lv) * status_get_lv(src) / 100) * wd.div_, 100 );
			break;
		case RA_WUGDASH:		// ウォーグダッシュ
			DMG_FIX( 300, 100 );
			break;
		case RA_WUGSTRIKE:		// ウォーグストライク
			DMG_FIX( 250 * skill_lv, 100 );
			break;
		case RA_WUGBITE:		// ウォーグバイト
			DMG_FIX( 800 + 200 * skill_lv, 100 );
			break;
		case RA_SENSITIVEKEEN:	// 鋭敏な嗅覚
			DMG_FIX( 100 + 50 * skill_lv, 100 );
			break;
		case NC_BOOSTKNUCKLE:	// ブーストナックル
			DMG_FIX( (200 + 100 * skill_lv + status_get_dex(src)) * status_get_lv(src) / 120, 100 );
			if(sc && sc->data[SC_ABR_DUAL_CANNON].timer != -1) {
				wd.div_ = 2;
			}
			break;
		case NC_PILEBUNKER:	// パイルバンカー
			DMG_FIX( (300 + 100 * skill_lv + status_get_str(src)) * status_get_lv(src) / 100, 100 );
			break;
		case NC_VULCANARM:	// バルカンアーム
			DMG_FIX( (70 * skill_lv + status_get_dex(src)) * status_get_lv(src) / 120, 100 );
			if(sc && sc->data[SC_ABR_DUAL_CANNON].timer != -1) {
				wd.div_ = 2;
			}
			break;
		case NC_FLAMELAUNCHER:	// フレイムスローワー
		case NC_COLDSLOWER:		// コールドスローワー
			DMG_FIX( (300 + 300 * skill_lv) * status_get_lv(src) / 150, 100 );
			break;
		case NC_SELFDESTRUCTION:	// セルフディストラクション
			{
				int mf_lv = src_sd ? pc_checkskill(src_sd,NC_MAINFRAME) : 2;
				DMG_SET( (skill_lv + 1) * (mf_lv + 8) * (status_get_sp(src) + status_get_vit(src)) * status_get_lv(src) / 100 + status_get_hp(src) );
			}
			break;
		case NC_AXEBOOMERANG:	// アックスブーメラン
			if(src_sd) {
				int dmg = 250 + 50 * skill_lv;
				int idx = src_sd->equip_index[EQUIP_INDEX_RARM];
				if(idx >= 0 && src_sd->inventory_data[idx] && itemdb_isweapon(src_sd->inventory_data[idx]->nameid))
					dmg += src_sd->inventory_data[idx]->weight/10;
				DMG_FIX( dmg * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (250 + 50 * skill_lv) * status_get_lv(src) / 100, 100 );
			}
			break;
		case NC_POWERSWING:	// パワースイング
			if(sc && sc->data[SC_ABR_BATTLE_WARIOR].timer != -1) {
				DMG_FIX( 500 + 150 * skill_lv + (status_get_str(src) + status_get_dex(src)) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( 300 + 100 * skill_lv + (status_get_str(src) + status_get_dex(src)) * status_get_lv(src) / 100, 100 );
			}
			break;
		case NC_AXETORNADO:	// アックストルネード
			if(sc && sc->data[SC_AXE_STOMP].timer != -1) {
				DMG_FIX( (230 + 230 * skill_lv + status_get_vit(src)*2) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (200 + 180 * skill_lv + status_get_vit(src)) * status_get_lv(src) / 100, 100 );
			}
			break;
#ifdef PRE_RENEWAL
		case NC_ARMSCANNON:	// アームズキャノン
			if(src_sd && src_sd->arrow_atk) {
				DMG_ADD( src_sd->arrow_atk );
			}
			DMG_FIX( (300 + (400 - 50 * t_size) * skill_lv) * status_get_lv(src) / 120, 100 );
			if(sc && sc->data[SC_ABR_DUAL_CANNON].timer != -1) {
				wd.div_ = 2;
			}
			break;
#endif
		case NC_MAGMA_ERUPTION:	/* マグマイラプション */
			DMG_FIX( 450 + 50 * skill_lv, 100 );
			break;
		case SC_FATALMENACE:	// フェイタルメナス
			if(sc && sc->data[SC_ABYSS_DAGGER].timer != -1) {
				DMG_FIX( (120 + 120 * skill_lv) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (100 + 100 * skill_lv) * status_get_lv(src) / 100, 100 );
			}
			if(src_sd && src_sd->status.weapon == WT_DAGGER)
				wd.div_ = 2;
			break;
		case SC_TRIANGLESHOT:	// トライアングルショット
			DMG_FIX( ((skill_lv - 1) * status_get_agi(src) / 2 + 300) * status_get_lv(src) / 120, 100 );
			break;
		case SC_FEINTBOMB:	// フェイントボム
			DMG_FIX( (1 + ((skill_lv == 1)? 2: 3) + (skill_lv - 1) / 3) * status_get_dex(src) / 2 * status_get_jlv(src) / 10 * status_get_lv(src) / 120, 100 );
			wd.blewcount = 0;
			break;
		case LG_CANNONSPEAR:	// キャノンスピア
			if(sc && sc->data[SC_SPEAR_SCAR].timer != -1) {
				DMG_FIX( (200 + status_get_str(src)) * skill_lv * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (50 + status_get_str(src)) * skill_lv * status_get_lv(src) / 100, 100 );
			}
			break;
		case LG_BANISHINGPOINT:	// バニシングポイント
			if(sc && sc->data[SC_SPEAR_SCAR].timer != -1) {
				DMG_FIX( (100 * skill_lv + ((src_sd)? pc_checkskill(src_sd,SM_BASH): 0) * 60) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (50 * skill_lv + ((src_sd)? pc_checkskill(src_sd,SM_BASH): 0) * 30) * status_get_lv(src) / 100, 100 );
			}
			break;
		case LG_SHIELDPRESS:	// シールドプレス
			if(src_sd) {
				int idx = src_sd->equip_index[EQUIP_INDEX_LARM];
				if(idx >= 0 && src_sd->inventory_data[idx] && itemdb_isarmor(src_sd->inventory_data[idx]->nameid)) {
					if(src_sd->sc.data[SC_SHIELD_POWER].timer != -1) {
						DMG_FIX( (300 * skill_lv + status_get_str(src) + src_sd->inventory_data[idx]->weight/10) * status_get_lv(src) / 100, 100);
					} else {
						DMG_FIX( (200 * skill_lv + status_get_str(src) + src_sd->inventory_data[idx]->weight/10) * status_get_lv(src) / 100, 100);
					}
				}
			} else {
				DMG_FIX( (200 * skill_lv + status_get_str(src)) * status_get_lv(src) / 100, 100 );
			}
			break;
		case LG_PINPOINTATTACK:	// ピンポイントアタック
			DMG_FIX( (100 * skill_lv + status_get_agi(src) * 5) * status_get_lv(src) / 120, 100 );
			break;
		case LG_RAGEBURST:	// レイジバーストアタック
			{
				int rate;
				if(sc && sc->data[SC_FORCEOFVANGUARD].timer != -1 && sc->data[SC_FORCEOFVANGUARD].val4 > 0) {
					rate = 200 * sc->data[SC_FORCEOFVANGUARD].val4;
				} else {
					rate = 200;
				}
				rate += (status_get_max_hp(src) - status_get_hp(src)) / 100;
				rate = rate * status_get_lv(src) / 100;
				DMG_FIX( rate, 100 );
			}
			break;
		case LG_SHIELDSPELL:	// シールドスペル
			if(src_sd) {
				int idx = src_sd->equip_index[EQUIP_INDEX_LARM];
				if(idx >= 0 && src_sd->inventory_data[idx] && itemdb_isarmor(src_sd->inventory_data[idx]->nameid)) {
#ifdef PRE_RENEWAL
					DMG_FIX( status_get_lv(src) * 4 + src_sd->inventory_data[idx]->def * 100 + status_get_vit(src) * 2, 100 );
#else
					DMG_FIX( status_get_lv(src) * 4 + src_sd->inventory_data[idx]->def * 10 + status_get_vit(src) * 2, 100 );
#endif
				}
			}
			else {
				DMG_FIX( status_get_lv(src) * 4 + status_get_vit(src) * 2, 100 );
			}
			break;
		case LG_OVERBRAND:	// オーバーブランド
			DMG_FIX( (400 * skill_lv + ((src_sd)? pc_checkskill(src_sd,CR_SPEARQUICKEN): 0) * 50) * status_get_lv(src) / 150, 100 );
			break;
		case LG_OVERBRAND_BRANDISH:		// オーバーブランド(薙ぎ)
			DMG_FIX( (300 * skill_lv + status_get_str(src) + status_get_dex(src)) * status_get_lv(src) / 150, 100 );
			break;
		case LG_OVERBRAND_PLUSATK:		// オーバーブランド(追撃)
			DMG_FIX( 200 * skill_lv, 100 );
			break;
		case LG_MOONSLASHER:	// ムーンスラッシャー
			DMG_FIX( (120 * skill_lv + ((src_sd)? pc_checkskill(src_sd,LG_OVERBRAND): 0) * 80) * status_get_lv(src) / 100, 100 );
			break;
		case LG_HESPERUSLIT:	// ヘスペルスリット
			{
				int rate;
				rate = (120 * skill_lv + 200 * wd.div_) * status_get_lv(src) / 100;
				if(wd.div_ == 6) {
					rate = rate * 150 / 100;
				}
				DMG_FIX( rate, 100 );
				break;
			}
		case LG_EARTHDRIVE:	// アースドライブ
			if(src_sd) {
				int idx = src_sd->equip_index[EQUIP_INDEX_LARM];
				if(idx >= 0 && src_sd->inventory_data[idx] && itemdb_isarmor(src_sd->inventory_data[idx]->nameid))
					if(src_sd->sc.data[SC_SHIELD_POWER].timer != -1) {
						DMG_FIX( ((300 + 100 * skill_lv) * src_sd->inventory_data[idx]->weight / 1000) * status_get_lv(src) / 100, 100 );
					} else {
						DMG_FIX( ((100 + 100 * skill_lv) * src_sd->inventory_data[idx]->weight / 1000) * status_get_lv(src) / 100, 100 );
					}
			} else {
				DMG_FIX( (100 + 100 * skill_lv) * status_get_lv(src) / 100, 100 );
			}
			break;
		case SR_DRAGONCOMBO:	// 双竜脚
			DMG_FIX( ( 100 + 40 * skill_lv ) * status_get_lv(src) / 100, 100 );
			break;
		case SR_SKYNETBLOW:	// 天羅地網
			if(sc && sc->data[SC_COMBO].timer != -1 && sc->data[SC_COMBO].val1 == SR_DRAGONCOMBO) {
				DMG_FIX( (100 * skill_lv + status_get_agi(src) + 150) * status_get_lv(src) / 100, 100 );		// コンボ発動時
			} else {
				DMG_FIX( (80 * skill_lv + status_get_agi(src)) * status_get_lv(src) / 100, 100 );		// 通常時
			}
			break;
		case SR_EARTHSHAKER:	// 地雷震
			if(!wflag) {	// 通常
				DMG_FIX( (50 * skill_lv * status_get_lv(src) / 100) + status_get_int(src) * 2, 100 );
			} else {	// ハイド相手
				DMG_FIX( (150 * skill_lv * status_get_lv(src) / 100) + status_get_int(src) * 3, 100 );
			}
			break;
		case SR_FALLENEMPIRE:	// 大纏崩墜
			DMG_FIX( (100 + 250 * skill_lv) * status_get_lv(src) / 100, 100 );
			break;
		case SR_TIGERCANNON:	// 號砲
			if(src_sd) {
				int hp = 0, sp = 0;
				if(!wflag) {
					hp = (int)((atn_bignumber)status_get_max_hp(src) * (10 + skill_lv * 2) / 100);
					sp = (int)((atn_bignumber)status_get_max_sp(src) * (5 + skill_lv) / 100);
				}
				if(sc && sc->data[SC_COMBO].timer != -1 && sc->data[SC_COMBO].val1 == SR_FALLENEMPIRE) {
					DMG_FIX( (hp + sp) / 2 * status_get_lv(src) / 100 , 100 );		// コンボ発動時
				} else {
					DMG_FIX( (hp + sp) / 4 * status_get_lv(src) / 100 , 100 );			// 通常時
				}
			}
			break;
		case SR_RAMPAGEBLASTER:	// 爆気散弾
			if(src_sd && src_sd->spiritball.old) {
				if(sc && sc->data[SC_EXPLOSIONSPIRITS].timer != -1) {
					DMG_FIX( (75 + 25 * skill_lv) * status_get_lv(src) / 150 * src_sd->spiritball.old, 100 );
				} else {
					DMG_FIX( 20 * skill_lv * status_get_lv(src) / 150 * src_sd->spiritball.old, 100 );
				}
			} else if(sc && sc->data[SC_EXPLOSIONSPIRITS].timer != -1) {
				DMG_FIX( (75 + 25 * skill_lv) * status_get_lv(src) / 150, 100 );
			} else {
				DMG_FIX( 20 * skill_lv * status_get_lv(src) / 150, 100 );
			}
			break;
		case SR_CRESCENTELBOW:	// 破砕柱(衝突)
			DMG_FIX( 200 * skill_lv, 100 );
			break;
		case SR_CRESCENTELBOW_AUTOSPELL:	// 破砕柱(反撃)
			{
				int hp = status_get_hp(target);
				if(hp > 100000)
					hp = 100000;
				DMG_FIX( skill_lv * hp / 100 * status_get_lv(src) / 125, 100 );
			}
			break;
		case SR_KNUCKLEARROW:	// 修羅身弾
			if(!wflag) {
				DMG_FIX( (500 + 100 * skill_lv) * status_get_lv(src) / 100, 100 );
			} else if(target_sd) {		// 衝突ダメージ
				DMG_FIX( (150 * skill_lv) + (status_get_lv(target) / 3 * 10) * status_get_lv(src) / 100 + (target_sd->weight / target_sd->max_weight * 10), 100 );
			} else {
				DMG_FIX( (150 * skill_lv) + (status_get_lv(target) / 3 * 10) * status_get_lv(src) / 100, 100 );
			}
			break;
		case SR_WINDMILL:	// 旋風腿
			DMG_FIX( (status_get_lv(src) + status_get_dex(src)) * status_get_lv(src) / 100, 100 );
			break;
		case SR_GATEOFHELL:	// 羅刹破凰撃
			if(sc && sc->data[SC_COMBO].timer != -1 && sc->data[SC_COMBO].val1 == SR_FALLENEMPIRE) {
				DMG_FIX( 800 * skill_lv * status_get_lv(src) / 100, 100 );		// コンボ発動時
			} else {
				DMG_FIX( 500 * skill_lv * status_get_lv(src) / 100, 100 );		// 通常時
			}
			break;
		case SR_GENTLETOUCH_QUIET:	// 点穴 -默-
			DMG_FIX( ( 100 * skill_lv + status_get_dex(src) ) * status_get_lv(src) / 100, 100 );
			break;
		case SR_HOWLINGOFLION:	// 獅子吼
			DMG_FIX( 200 * skill_lv * status_get_lv(src) / 100, 100 );
			break;
		case SR_RIDEINLIGHTNING:	// 雷光弾
			DMG_FIX( ( 200 * skill_lv + ((s_ele == ELE_WIND)? 50 * skill_lv: 0) ) * status_get_lv(src) / 100, 100 );
			break;
		case WM_REVERBERATION_MELEE:	// 振動残響(物理)
			DMG_FIX( (300 + 100 * skill_lv) * status_get_lv(src) / 100, 100 );
			if(wflag > 1) {
				DMG_FIX( 1, wflag );
			}
			break;
		case WM_SEVERE_RAINSTORM:		// シビアレインストーム
		case WM_SEVERE_RAINSTORM_MELEE:	// シビアレインストーム(攻撃)
			if(src_sd && (src_sd->status.weapon == WT_MUSICAL || src_sd->status.weapon == WT_WHIP)) {
				DMG_FIX( (status_get_agi(src) + status_get_dex(src)) * skill_lv / 5 * status_get_lv(src) / 60, 100 );
			} else {
				DMG_FIX( (status_get_agi(src) + status_get_dex(src)) * skill_lv / 5 * status_get_lv(src) / 100, 100 );
			}
			break;
		case WM_GREAT_ECHO:		// グレートエコー
			{
				int rate = 400 + 200 * skill_lv;
				if(wflag >= 3 && wflag <= 7) {
					rate += 100<<(wflag-3);
				}
				DMG_FIX( rate * status_get_lv(src) / 100, 100 );
			}
			break;
		case WM_SOUND_OF_DESTRUCTION:	// サウンドオブディストラクション
			DMG_FIX( 250, 100 );
			break;
		case GN_CART_TORNADO:	// カートトルネード
			DMG_FIX( 100 * skill_lv + ((src_sd)? pc_checkskill(src_sd,GN_REMODELING_CART): 1) * 50 + ((src_sd)? src_sd->cart_weight/10 / (150-src_sd->status.str): 1), 100 );
			break;
		case GN_CARTCANNON:		// カートキャノン
			if(src_sd && src_sd->arrow_atk) {
				DMG_ADD( src_sd->arrow_atk );
			}
			DMG_FIX( 60 * skill_lv + (((src_sd)? pc_checkskill(src_sd,GN_REMODELING_CART): 1) * 50 * status_get_int(src) / 40), 100 );
			break;
		case GN_WALLOFTHORN:	// ソーンウォール
			DMG_FIX( 150 + 50 * skill_lv, 100 );
			break;
		case GN_CRAZYWEED_ATK:	// クレイジーウィード
			DMG_FIX( 500 + 100 * skill_lv, 100 );
			break;
		case GN_SPORE_EXPLOSION: // スポアエクスプロージョン
			DMG_FIX( ( 200 + 150 * skill_lv + status_get_int(src)) * status_get_lv(src) / 100, 100 );
			break;
		case GN_SLINGITEM_RANGEMELEEATK:	// スリングアイテム(遠距離攻撃)
			switch(skill_lv) {
			case 0:	/* リンゴ爆弾 */
				DMG_FIX( 400, 100 );
				break;
			case 1:	/* ココナッツ爆弾 */
				DMG_FIX( 1000, 100 );
				break;
			case 2:	/* メロン爆弾 */
				DMG_FIX( 400, 100 );
				break;
			case 3:	/* パイナップル爆弾 */
				DMG_FIX( 1000, 100 );
				break;
			case 4:	/* バナナ爆弾 */
				DMG_FIX( 800, 100 );
				break;
			case 5:	/* 黒い塊 */
				DMG_FIX( 200, 100 );
				break;
			case 6:	/* 硬くて黒い塊 */
				DMG_FIX( 300, 100 );
				break;
			case 7:	/* とても硬い塊 */
				DMG_FIX( 400, 100 );
				break;
			}
			break;
		case KO_JYUMONJIKIRI:	// 十文字斬り
			if(t_sc && t_sc->data[SC_KO_JYUMONJIKIRI].timer != -1) {
				DMG_FIX( 300 * skill_lv * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( 200 * skill_lv * status_get_lv(src) / 100, 100 );
			}
			break;
		case KO_SETSUDAN:		// 黄泉返し
			{
				int rate = 100 * skill_lv;

				if(t_sc) {
					int soul;

					for(soul = SC_ALCHEMIST; soul <= SC_GUNNER; soul++) {
						if(t_sc->data[soul].timer != -1) {
							rate += 200 * t_sc->data[soul].val1;
							break;
						}
					}
					for(soul = SC_SOULSHADOW; soul <= SC_SOULGOLEM; soul++) {
						if(t_sc->data[soul].timer != -1) {
							rate += 200 * t_sc->data[soul].val1;
							status_change_end(target,soul,-1);
							break;
						}
					}
				}

				DMG_FIX( rate * status_get_lv(src) / 100, 100 );
				status_change_soulclear(target);
			}
			break;
		case KO_BAKURETSU:		// 爆裂苦無
			DMG_FIX( ((skill_lv * (50 + status_get_dex(src) / 4)) * ((src_sd)? pc_checkskill(src_sd,NJ_TOBIDOUGU): 1) * 4 / 10 * status_get_lv(src) / 100) + status_get_jlv(src) * 10, 100);
			break;
		case KO_HAPPOKUNAI:		// 八方苦無
			DMG_FIX( 300 + 60 * skill_lv, 100 );
			wd.damage = wd.damage - (t_def1 + t_def2);
			break;
		case KO_MUCHANAGE:	// 無茶投げ
			{
				int dmg = 0;
				if(src_sd) {
					dmg = src_sd->zenynage_damage;
				} else {
					dmg = skill_get_zeny(KO_MUCHANAGE,skill_lv)/2;
					if(dmg > 0)
						dmg += atn_rand()%dmg;
					else
						dmg = 5000*skill_lv + atn_rand()%(5000*skill_lv);
				}
				if(wflag > 1)
					dmg /= wflag;
				if(t_mode & MD_BOSS)
					dmg /= 2;
				DMG_SET( dmg );
				calc_flag.nocardfix = 1;
			}
			break;
		case KO_HUUMARANKA:	// 風魔手裏剣 -乱華-
			DMG_FIX( (150 * skill_lv + status_get_str(src) + ((src_sd)? pc_checkskill(src_sd, NJ_HUUMA): 0) * 100) * status_get_lv(src) / 100, 100 );
			break;
		case RL_MASS_SPIRAL:		// マススパイラル
			{
				int rate = status_get_def(target);
				rate = (200 + (rate > 500? 500: rate)) * skill_lv;
				DMG_FIX( rate, 100 );
				if(src_sd)
					src_sd->state.arrow_atk = 1;
			}
			break;
		case RL_BANISHING_BUSTER:	// バニシングバスター
			DMG_FIX( 200 * skill_lv * status_get_lv(src) / 100, 100 );
			break;
		case RL_S_STORM:		// シャッターストーム
			DMG_FIX( 1700 + 200 * skill_lv, 100 );
			break;
		case RL_QD_SHOT:		// クイックドローショット
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			wd.div_ = wflag&0xf;
			break;
		case RL_FIREDANCE:		// ファイアーダンス
			DMG_FIX( (1000 + 100 * skill_lv + ((src_sd)? pc_checkskill(src_sd,GS_DESPERADO): 0) * 20) * status_get_lv(src) / 100, 100 );
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
		case RL_H_MINE:			// ハウリングマイン
			if(wflag&1) {		// ハウリングマイン(追撃)
				DMG_FIX( 1000 + 400 * skill_lv, 100 );
			} else {
				DMG_FIX( 400 * skill_lv, 100 );
			}
			break;
		case RL_R_TRIP:			// ラウンドトリップ
		case RL_R_TRIP_PLUSATK:	// ラウンドトリップ(追撃)
			DMG_FIX( (100 + 40 * skill_lv) * status_get_lv(src) / 100, 100 );
			break;
		case RL_D_TAIL:			// ドラゴンテイル
			if(t_sc && t_sc->data[SC_C_MARKER].timer != -1) {
				DMG_FIX( (1000 + 400 * skill_lv) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (500 + 200 * skill_lv) * status_get_lv(src) / 100, 100 );
			}
			break;
		case RL_FIRE_RAIN:		// ファイアーレイン
			DMG_FIX( 500 + 500 * skill_lv, 100 );
			break;
		case RL_AM_BLAST:		// アンチマテリアルブラスト
			DMG_FIX( 1500 + 300 * skill_lv, 100 );
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
		case RL_SLUGSHOT:		// スラッグショット
			{
				int rate = 1200 * skill_lv * (t_size+2);
				if(target->type == BL_PC)
					rate /= 2;
				DMG_FIX( rate, 100 );
				if(src_sd)
					src_sd->state.arrow_atk = 1;
			}
			break;
		case RL_HAMMER_OF_GOD:	// ハンマーオブゴッド
			if(t_sc && t_sc->data[SC_C_MARKER].timer != -1) {
				DMG_FIX( (500 + 100 * skill_lv + ((src_sd)? src_sd->coin.num: 0) * 200) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (500 + 100 * skill_lv + ((src_sd)? src_sd->coin.num: 0) * 50) * status_get_lv(src) / 100, 100 );
			}
			if(src_sd)
				src_sd->state.arrow_atk = 1;
			break;
		case SJ_FULLMOONKICK:	// 満月脚
			{
				int rate = ( 500 + 150 * skill_lv ) * status_get_lv(src) / 100;
				if(sc && sc->data[SC_LIGHTOFMOON].timer != -1 ) {
					rate += ( rate * sc->data[SC_LIGHTOFMOON].val2 ) / 100;
				}
				DMG_FIX( rate, 100 );
			}
			break;
		case SJ_NEWMOONKICK:	// 朔月脚
			DMG_FIX( 1650 + 50 * skill_lv, 100 );
			break;
		case SJ_FLASHKICK:		// 閃光脚
			DMG_FIX( 100, 100 );
			break;
		case SJ_PROMINENCEKICK:	// 紅焔脚
			DMG_FIX( 650 + 50 * skill_lv, 100 );
			break;
		case SJ_SOLARBURST:	// 太陽爆発
			{
				int rate = ( 1000 + 220 * skill_lv ) * status_get_lv(src) / 100;
				if(sc && sc->data[SC_LIGHTOFSUN].timer != -1 ) {
					rate += ( rate * sc->data[SC_LIGHTOFSUN].val2 ) / 100;
				}
				DMG_FIX( rate, 100 );
			}
			break;
		case SJ_FALLINGSTAR_ATK:	// 流星落下
		case SJ_FALLINGSTAR_ATK2:
			{
				int rate = ( 100 + 100 * skill_lv ) * status_get_lv(src) / 100;
				if (sc && sc->data[SC_LIGHTOFSTAR].timer != -1 )
					rate += ( rate * sc->data[SC_LIGHTOFSTAR].val2 ) / 100;
				DMG_FIX( rate, 100 );
			}
			break;
		case SJ_STAREMPEROR:		// 星帝降臨
			DMG_FIX( 1500 + 500 * skill_lv, 100 );
			break;
		case SJ_NOVAEXPLOSING:	// 新星爆発
			DMG_FIX( 500 + 500 * skill_lv, 100 );
			DMG_ADD(status_get_max_hp(src) * skill_lv / 5  + status_get_max_sp(src) * skill_lv * 2);
			break;
		case SJ_BOOKOFCREATINGSTAR:	// 創星の書
			DMG_FIX( 500 + 500 * skill_lv, 100 );
			break;
		case SU_BITE:	// かみつく
			if(status_get_hp(target) / status_get_max_hp(target) * 100 <= 70) {
				DMG_FIX( 1500, 100 );
			} else {
				DMG_FIX( 1000, 100 );
			}
			break;
		case SU_SCRATCH:	// ひっかく
			DMG_FIX( 400 + 200 * skill_lv, 100 );
			break;
		case SU_PICKYPECK:	// ピッキ突き
			{
				int rate = 1250 + 50 * skill_lv;
				if(src_sd && pc_checkskill(src_sd,SU_SPIRITOFLIFE)) {	// 生命の魂
					int s_hp = status_get_hp(target) / status_get_max_hp(target) * 100;
					if(s_hp >= 100)
						rate = rate * 200 / 100;
					else if(s_hp >= 81)
						rate = rate * 150 / 100;
					else if(s_hp >= 51)
						rate = rate * 130 / 100;
					else if(s_hp >= 10)
						rate = rate * 110 / 100;
				}
				DMG_FIX( rate, 100 );
			}
			break;
		case SU_PICKYPECK_DOUBLE_ATK:	// ピッキ突き(追撃)
			{
				int rate = 2500 + 100 * skill_lv;
				if(src_sd && pc_checkskill(src_sd,SU_SPIRITOFLIFE)) {	// 生命の魂
					int s_hp = status_get_hp(target) / status_get_max_hp(target) * 100;
					if(s_hp >= 100)
						rate = rate * 200 / 100;
					else if(s_hp >= 81)
						rate = rate * 150 / 100;
					else if(s_hp >= 51)
						rate = rate * 130 / 100;
					else if(s_hp >= 10)
						rate = rate * 110 / 100;
				}
				DMG_FIX( rate, 100 );
			}
			break;
		case SU_SCAROFTAROU:	// タロウの傷
			{
				int rate = 4000 + 200 * skill_lv;
				if(src_sd && pc_checkskill(src_sd,SU_SPIRITOFLIFE)) {	// 生命の魂
					int s_hp = status_get_hp(target) / status_get_max_hp(target) * 100;
					if(s_hp >= 100)
						rate = rate * 200 / 100;
					else if(s_hp >= 81)
						rate = rate * 150 / 100;
					else if(s_hp >= 51)
						rate = rate * 130 / 100;
					else if(s_hp >= 10)
						rate = rate * 110 / 100;
				}
				if(t_mode & MD_BOSS)
					rate *= 2;
				if(t_sc && t_sc->data[SC_BITESCAR].timer != -1)	// タロウの傷状態
					rate *= 2;
				DMG_FIX( rate, 100 );
			}
			break;
		case SU_LUNATICCARROTBEAT:	// キャロットビート
		case SU_LUNATICCARROTBEAT2:	// キャロットビート
			{
				int rate = 1000 + 100 * skill_lv;
				if(src_sd && pc_checkskill(src_sd,SU_SPIRITOFLIFE)) {	// 生命の魂
					int s_hp = status_get_hp(target) / status_get_max_hp(target) * 100;
					if(s_hp >= 100)
						rate = rate * 200 / 100;
					else if(s_hp >= 81)
						rate = rate * 150 / 100;
					else if(s_hp >= 51)
						rate = rate * 130 / 100;
					else if(s_hp >= 10)
						rate = rate * 110 / 100;
				}
				DMG_FIX( rate * (status_get_lv(src) >= 100? status_get_lv(src): 100) / 100, 100 );
			}
			break;
		case SU_SVG_SPIRIT:	// サベージの魂
			{
				int rate = 2500 + 100 * skill_lv;
				if(src_sd && pc_checkskill(src_sd,SU_SPIRITOFLIFE)) {	// 生命の魂
					int s_hp = status_get_hp(target) / status_get_max_hp(target) * 100;
					if(s_hp >= 100)
						rate = rate * 200 / 100;
					else if(s_hp >= 81)
						rate = rate * 150 / 100;
					else if(s_hp >= 51)
						rate = rate * 130 / 100;
					else if(s_hp >= 10)
						rate = rate * 110 / 100;
				}
				DMG_FIX( rate, 100 );
			}
			break;
		case DK_SERVANTWEAPON_ATK:	// サーヴァントウェポン(攻撃)
			DMG_FIX( (1250 + skill_lv*50 + status_get_pow(src)*5) * status_get_lv(src) / 100, 100 );
			break;
		case DK_SERVANT_W_PHANTOM:	// サーヴァントウェポン：ファントム
			DMG_FIX( (1250 + skill_lv*50 + status_get_pow(src)*5) * status_get_lv(src) / 100, 100 );
			wd.div_ = wflag;		// wflag（武器体の数）を攻撃回数とする
			break;
		case DK_SERVANT_W_DEMOL:	// サーヴァントウェポン：デモリッション
			DMG_FIX( (1250 + skill_lv*50 + status_get_pow(src)*5) * status_get_lv(src) / 100, 100 );
			wd.div_ = wflag;		// wflag（武器体の数）を攻撃回数とする
			break;
		case DK_HACKANDSLASHER:		// ハックアンドスラッシャー
		case DK_HACKANDSLASHER_ATK:	// ハックアンドスラッシャー(攻撃)
			DMG_FIX( (1400 + skill_lv*100 + status_get_pow(src)*8) * status_get_lv(src) / 100, 100 );
			break;
		case DK_DRAGONIC_AURA:	// ドラゴニックオーラ
			DMG_FIX( (7000 + skill_lv*2000 + status_get_pow(src)*90) * status_get_lv(src) / 100, 100 );
			break;
		case DK_MADNESS_CRUSHER:	// マッドネスクラッシャー
			{
				int weight = 0, wlv = 0;
				if(src_sd && src_sd->equip_index[EQUIP_INDEX_RARM] >= 0) {
					int idx = src_sd->equip_index[EQUIP_INDEX_RARM];
					if(src_sd->inventory_data[idx] && itemdb_isweapon(src_sd->inventory_data[idx]->nameid)) {
						weight = src_sd->inventory_data[idx]->weight/10;
						wlv = src_sd->inventory_data[idx]->wlv;
					}
				}
				DMG_FIX( (3000 + skill_lv*900 + status_get_pow(src)*(10+skill_lv*3) + weight * wlv) * status_get_lv(src) / 100, 100 );
			}
			break;
		case DK_STORMSLASH:		//ストームスラッシュ
			DMG_FIX( (1250 + skill_lv*50 + status_get_pow(src)*5) * status_get_lv(src) / 100, 100 );
			if(sc && sc->data[SC_GIANTGROWTH].timer != -1 && atn_rand() % 100 < 15) {		// ジャイアントグロース状態
				DMG_FIX( 200, 100 );
			}
			break;
		case IQ_OLEUM_SANCTUM:		/* 聖油洗礼 */
			DMG_FIX( (2000 + skill_lv*500 + status_get_pow(src)*15) * status_get_lv(src) / 100, 100 );
			break;
		case IQ_MASSIVE_F_BLASTER:	/* 炎火滅魔神弾 */
			DMG_FIX( (3500 + skill_lv*1000 + status_get_pow(src)*45) * status_get_lv(src) / 100, 100 );
			break;
		case IQ_EXPOSION_BLASTER:	/* 爆火神弾 */
			DMG_FIX( (2000 + skill_lv*500 + status_get_pow(src)*15) * status_get_lv(src) / 100, 100 );
			break;
		case IQ_FIRST_BRAND:		/* 第一撃：烙印 */
			DMG_FIX( (2000 + skill_lv*500 + status_get_pow(src)*15) * status_get_lv(src) / 100, 100 );
			break;
		case IQ_SECOND_FLAME:		/* 第二撃：滅魔の火 */
			DMG_FIX( (3600 + skill_lv*900 + status_get_pow(src)*27) * status_get_lv(src) / 100, 100 );
			break;
		case IQ_SECOND_FAITH:		/* 第二撃：信念 */
			DMG_FIX( (3150 + skill_lv*750 + status_get_pow(src)*23) * status_get_lv(src) / 100, 100 );
			break;
		case IQ_SECOND_JUDGEMENT:	/* 第二撃：審判 */
			DMG_FIX( (3375 + skill_lv*825 + status_get_pow(src)*25) * status_get_lv(src) / 100, 100 );
			break;
		case IQ_THIRD_PUNISH:		/* 第三撃：断罪 */
			DMG_FIX( (7000 + skill_lv*1000 + status_get_pow(src)*40) * status_get_lv(src) / 100, 100 );
			break;
		case IQ_THIRD_FLAME_BOMB:	/* 第三撃：滅火撃 */
			DMG_FIX( (7000 + skill_lv*1000 + status_get_pow(src)*40) * status_get_lv(src) / 100, 100 );
			if(src_sd) {
				if(src_sd->spiritball.num >= 11) {
					wd.div_ = 3;
				} else if(src_sd->spiritball.num >= 6) {
					wd.div_ = 2;
				}
			}
			break;
		case IQ_THIRD_CONSECRATION:	/* 第三撃：浄化 */
			DMG_FIX( (7000 + skill_lv*1000 + status_get_pow(src)*40) * status_get_lv(src) / 100, 100 );
			break;
		case IG_GRAND_JUDGEMENT:	/* グランドジャッジメント */
			DMG_FIX( (7000 + skill_lv*2000 + status_get_pow(src)*35) * status_get_lv(src) / 100, 100 );
			break;
		case IG_SHIELD_SHOOTING:	/* シールドシューティング */
			if(src_sd) {
				int idx = src_sd->equip_index[EQUIP_INDEX_LARM];
				if(idx >= 0 && src_sd->inventory_data[idx] && itemdb_isarmor(src_sd->inventory_data[idx]->nameid)) {
					DMG_FIX( (600 + (800 + pc_checkskill(src_sd,IG_SHIELD_MASTERY)*40) * skill_lv + src_sd->status.inventory[idx].refine*200 + src_sd->inventory_data[idx]->weight/10 + status_get_pow(src)*30) * status_get_lv(src) / 100, 100);
				}
			} else {
				DMG_FIX( (600 + 800 * skill_lv + status_get_pow(src)*30) * status_get_lv(src) / 100, 100 );
			}
			break;
		case IG_OVERSLASH:	/* オーバースラッシュ */
			DMG_FIX( (skill_lv*70 + ((src_sd)? pc_checkskill(src_sd,IG_SPEAR_SWORD_M): 0) * (skill_lv * 5) + status_get_pow(src)*3) * status_get_lv(src) / 100, 100);
			if(wflag < 2) {
				wd.div_ = skill_get_num(skill_num,1);
			} else if(wflag < 4) {
				wd.div_ = skill_get_num(skill_num,2);
			} else {
				wd.div_ = skill_get_num(skill_num,3);
			}
			break;
		case CD_EFFLIGO:	/* エフィリゴ */
			if(t_race == RCT_UNDEAD || t_race == RCT_DEMON) {
				DMG_FIX( (4000 + 500 * skill_lv + ((src_sd)? pc_checkskill(src_sd,CD_MACE_BOOK_M): 0) * (400 + skill_lv * 50) + status_get_pow(src)*60) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (3000 + 375 * skill_lv + ((src_sd)? pc_checkskill(src_sd,CD_MACE_BOOK_M): 0) * (200 + skill_lv * 25) + status_get_pow(src)*30) * status_get_lv(src) / 100, 100 );
			}
			break;
		case CD_PETITIO:	/* ペティティオ */
			DMG_FIX( (250 * skill_lv + ((src_sd)? pc_checkskill(src_sd,CD_MACE_BOOK_M): 0) * (skill_lv * 20) + status_get_pow(src)*15) * status_get_lv(src) / 100, 100 );
			break;
		case SHC_DANCING_KNIFE:		/* ダンシングナイフ */
			DMG_FIX( (100 + 100 * skill_lv + status_get_pow(src)*2) * status_get_lv(src) / 100, 100 );
			break;
		case SHC_SAVAGE_IMPACT:		/* サベージインパクト */
			if(sc && sc->data[SC_SHADOW_EXCEED].timer != -1) {
				DMG_FIX( (1000 + 200 * skill_lv + status_get_pow(src)*10) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (500 + 100 * skill_lv + status_get_pow(src)*5) * status_get_lv(src) / 100, 100 );
			}
			if(sc && sc->data[SC_CLOAKINGEXCEED].timer != -1) {
				wd.div_ = 2;
			}
			break;
		case SHC_ETERNAL_SLASH:		/* エターナルスラッシュ */
			if(sc && sc->data[SC_SHADOW_EXCEED].timer != -1) {
				DMG_FIX( (100 + 100 * skill_lv + status_get_pow(src)*2) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (50 + 50 * skill_lv + status_get_pow(src)) * status_get_lv(src) / 100, 100 );
			}
			if(sc && sc->data[SC_E_SLASH_COUNT].timer != -1) {
				wd.div_ = sc->data[SC_E_SLASH_COUNT].val1;
			}
			break;
		case SHC_SHADOW_STAB:		/* シャドウスタブ */
			if(sc && sc->data[SC_CLOAKINGEXCEED].timer != -1) {
				DMG_FIX( (1250 + 1250 * skill_lv + status_get_pow(src)*25) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (500 + 500 * skill_lv + status_get_pow(src)*10) * status_get_lv(src) / 100, 100 );
			}
			calc_flag.idef = calc_flag.idef_ = 1;
			break;
		case SHC_IMPACT_CRATER:		/* インパクトクレーター */
			DMG_FIX( (50 + 50 * skill_lv + status_get_pow(src)) * status_get_lv(src) / 100, 100 );
			if(sc && sc->data[SC_ROLLINGCUTTER].timer != -1) {
				wd.div_ = sc->data[SC_ROLLINGCUTTER].val1;
			}
			break;
		case SHC_FATAL_SHADOW_CROW:		/* フェイタルシャドウクロー */
			DMG_FIX( (600 + 150 * skill_lv + status_get_pow(src)*7) * status_get_lv(src) / 100, 100 );
			break;
		case MT_AXE_STOMP:		/* アックスストンプ */
			DMG_FIX( (3150 + 750 * skill_lv + status_get_pow(src)*10) * status_get_lv(src) / 100, 100 );
			if(src_sd && src_sd->status.weapon == WT_2HAXE) {
				wd.div_ = 2;
			}
			break;
		case MT_RUSH_QUAKE:		/* ラッシュクエイク */
			DMG_FIX( (2700 * skill_lv + status_get_pow(src)*40) * status_get_lv(src) / 100, 100 );
			break;
		case MT_A_MACHINE:		/* 攻撃装置有効化 */
			DMG_FIX( (300 * skill_lv + status_get_pow(src)*5) * status_get_lv(src) / 100, 100 );
			break;
		case ABC_ABYSS_DAGGER:		/* アビスダガー */
			DMG_FIX( (3150 + 750 * skill_lv + status_get_pow(src)*23) * status_get_lv(src) / 100, 100 );
			break;
		case ABC_UNLUCKY_RUSH:		/* アンラッキーラッシュ */
			DMG_FIX( (2100 + 600 * skill_lv + status_get_pow(src)*17) * status_get_lv(src) / 100, 100 );
			break;
		case ABC_CHAIN_REACTION_SHOT:		/* チェーンリアクションショット */
		case ABC_CHAIN_REACTION_SHOT_ATK:	/* チェーンリアクションショット(攻撃) */
			DMG_FIX( (2350 + 550 * skill_lv + status_get_con(src)*17) * status_get_lv(src) / 100, 100 );
			break;
		case ABC_DEFT_STAB:		/* デフトスタブ */
			DMG_FIX( (3400 + 350 * skill_lv + status_get_pow(src)*23) * status_get_lv(src) / 100, 100 );
			break;
		case ABC_FRENZY_SHOT:	/* フレンジショット */
			DMG_FIX( (1100 + 100 * skill_lv + status_get_con(src)*7) * status_get_lv(src) / 100, 100 );
			if(atn_rand()%100 < skill_lv * 5) {
				wd.div_ = 3;
			}
			break;
		case WH_HAWKRUSH:		/* ホークラッシュ */
			DMG_FIX( (1000 + 100 * skill_lv + status_get_con(src)*5) * ((src_sd)? 100+pc_checkskill(src_sd,WH_NATUREFRIENDLY)*20: 100) / 100 * status_get_lv(src) / 100, 100 );
			break;
		case WH_GALESTORM:		/* ゲイルストーム */
			DMG_FIX( (1000 + 200 * skill_lv + status_get_con(src)*10) * status_get_lv(src) / 100, 100 );
			if(sc && sc->data[SC_CALAMITYGALE].timer != -1 && (t_race == RCT_BRUTE || t_race == RCT_FISH)) {
				DMG_FIX( 200, 100);
			}
			break;
		case WH_HAWKBOOMERANG:	/* ホークブーメラン */
			DMG_FIX( (1000 + 100 * skill_lv + status_get_con(src)*5) * ((src_sd)? 100+pc_checkskill(src_sd,WH_NATUREFRIENDLY)*20: 100) / 100 * status_get_lv(src) / 100, 100 );
			break;
		case WH_CRESCIVE_BOLT:		/* クレッシブボルト */
			DMG_FIX( (1000 + 200 * skill_lv + status_get_con(src)*10) * status_get_lv(src) / 100, 100 );
			if(sc && sc->data[SC_CRESCIVEBOLT].timer != -1) {
				if(sc->data[SC_CRESCIVEBOLT].val1 == 1) {
					DMG_FIX( 110, 100);
				} else if(sc->data[SC_CRESCIVEBOLT].val1 == 2) {
					DMG_FIX( 125, 100);
				} else {
					DMG_FIX( 150, 100);
				}
			}
			if(sc && sc->data[SC_CALAMITYGALE].timer != -1) {
				if(t_race == RCT_BRUTE || t_race == RCT_FISH) {
					DMG_FIX( 200, 100);
				} else {
					DMG_FIX( 125, 100);
				}
			}
			break;
		case WH_DEEPBLINDTRAP:	/* ディープブラインドトラップ */
		case WH_SOLIDTRAP:		/* ソリッドトラップ */
		case WH_SWIFTTRAP:		/* スイフトトラップ */
		case WH_FLAMETRAP:		/* フレイムトラップ */
			DMG_FIX( (1500 + 300 * skill_lv + status_get_con(src)*10) * ((src_sd)? 100+pc_checkskill(src_sd,WH_ADVANCED_TRAP)*20: 100) / 100 * status_get_lv(src) / 100, 100 );
			break;
		case BO_ACIDIFIED_ZONE_WATER:		/* アシディファイドゾーン(水) */
		case BO_ACIDIFIED_ZONE_GROUND:		/* アシディファイドゾーン(地) */
		case BO_ACIDIFIED_ZONE_WIND:		/* アシディファイドゾーン(風) */
		case BO_ACIDIFIED_ZONE_FIRE:		/* アシディファイドゾーン(火) */
			if(sc && sc->data[SC_RESEARCHREPORT].timer != -1) {
				DMG_FIX( (2500 + 250 * skill_lv + status_get_pow(src)*10) * status_get_lv(src) / 100, 100 );
				if(t_race == RCT_FORMLESS || t_race == RCT_PLANT) {
					DMG_FIX( 150, 100);
				}
			} else {
				DMG_FIX( (2000 + 200 * skill_lv + status_get_pow(src)*5) * status_get_lv(src) / 100, 100 );
			}
			break;
		case BO_ACIDIFIED_ZONE_WATER_INSTALL_ON_GROUND:		/* アシディファイドゾーン(水)(設置) */
		case BO_ACIDIFIED_ZONE_GROUND_INSTALL_ON_GROUND:	/* アシディファイドゾーン(地)(設置) */
		case BO_ACIDIFIED_ZONE_WIND_INSTALL_ON_GROUND:		/* アシディファイドゾーン(風)(設置) */
		case BO_ACIDIFIED_ZONE_FIRE_INSTALL_ON_GROUND:		/* アシディファイドゾーン(火)(設置) */
			DMG_FIX( (625 * skill_lv / 10) * status_get_lv(src) / 100, 100 );
			break;
		case TR_RHYTHMSHOOTING:		/*リズムシューティング */
			if(t_sc && t_sc->data[SC_SOUNDBLEND].timer != -1) {
				DMG_FIX( (1250 + 350 * skill_lv + status_get_con(src)*10) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (750 + 150 * skill_lv + status_get_con(src)*5) * status_get_lv(src) / 100, 100 );
			}
			break;

		case NPC_BO_THROWROCK:		/* スローロック */
			DMG_FIX( 20000 * status_get_lv(src) / 100, 100 );
			break;
		case NPC_BO_WOODEN_ATTACK:	/* ウドゥンアタック */
			DMG_FIX( 20000 * status_get_lv(src) / 100, 100 );
			break;
		case NW_THE_VIGILANTE_AT_NIGHT:		/* ビジラントアットナイト */
			if(src_sd && src_sd->status.weapon == WT_GATLING) {
				if(sc && sc->data[SC_INTENSIVE_AIM_COUNT].timer != -1) {
					DMG_FIX( (375 + skill_lv * 85 + status_get_con(src) * 2 + (125 + skill_lv * 25) * sc->data[SC_INTENSIVE_AIM_COUNT].val1) * status_get_lv(src) / 100, 100 );
				} else {
					DMG_FIX( (375 + skill_lv * 85 + status_get_con(src) * 2) * status_get_lv(src) / 100, 100 );
				}
				wd.div_ = 7;
				calc_flag.idef = calc_flag.idef_ = 1;
			} else {
				if(sc && sc->data[SC_INTENSIVE_AIM_COUNT].timer != -1) {
					DMG_FIX( (700 + skill_lv * 150 + status_get_con(src) * 3 + (300 + skill_lv * 50) * sc->data[SC_INTENSIVE_AIM_COUNT].val1) * status_get_lv(src) / 100, 100 );
				} else {
					DMG_FIX( (700 + skill_lv * 150 + status_get_con(src) * 3) * status_get_lv(src) / 100, 100 );
				}
			}
			break;
		case NW_ONLY_ONE_BULLET:		/* オンリーワンバレット */
			if(src_sd && src_sd->status.weapon == WT_RIFLE) {
				if(sc && sc->data[SC_INTENSIVE_AIM_COUNT].timer != -1) {
					DMG_FIX( (2800 + skill_lv * 500 + status_get_con(src) * 3 + (950 + skill_lv * 150) * sc->data[SC_INTENSIVE_AIM_COUNT].val1) * status_get_lv(src) / 100, 100 );
				} else {
					DMG_FIX( (2800 + skill_lv * 500 + status_get_con(src) * 3) * status_get_lv(src) / 100, 100 );
				}
			} else {
				if(sc && sc->data[SC_INTENSIVE_AIM_COUNT].timer != -1) {
					DMG_FIX( (6000 + skill_lv * 900 + status_get_con(src) * 3 + (950 + skill_lv * 150) * sc->data[SC_INTENSIVE_AIM_COUNT].val1) * status_get_lv(src) / 100, 100 );
				} else {
					DMG_FIX( (6000 + skill_lv * 900 + status_get_con(src) * 3) * status_get_lv(src) / 100, 100 );
				}
				calc_flag.idef = calc_flag.idef_ = 1;
			}
			break;
		case NW_SPIRAL_SHOOTING:		/* スパイラルシューティング */
			if(src_sd && src_sd->status.weapon == WT_GRENADE) {
				if(sc && sc->data[SC_INTENSIVE_AIM_COUNT].timer != -1) {
					DMG_FIX( (1700 + skill_lv * 300 + status_get_con(src) * 3 + (550 + skill_lv * 100) * sc->data[SC_INTENSIVE_AIM_COUNT].val1) * status_get_lv(src) / 100, 100 );
				} else {
					DMG_FIX( (1700 + skill_lv * 300 + status_get_con(src) * 3) * status_get_lv(src) / 100, 100 );
				}
				wd.div_ += 1;
			} else {
				if(sc && sc->data[SC_INTENSIVE_AIM_COUNT].timer != -1) {
					DMG_FIX( (1950 + skill_lv * 350 + status_get_con(src) * 3 + (550 + skill_lv * 100) * sc->data[SC_INTENSIVE_AIM_COUNT].val1) * status_get_lv(src) / 100, 100 );
				} else {
					DMG_FIX( (1950 + skill_lv * 350 + status_get_con(src) * 3) * status_get_lv(src) / 100, 100 );
				}
			}
			break;
		case NW_MAGAZINE_FOR_ONE:		/* マガジンフォーワン */
			if(src_sd && src_sd->status.weapon == WT_GATLING) {
				if(sc && sc->data[SC_INTENSIVE_AIM_COUNT].timer != -1) {
					DMG_FIX( (430 + skill_lv * 90 + status_get_con(src) * 2 + (125 + skill_lv * 25) * sc->data[SC_INTENSIVE_AIM_COUNT].val1) * status_get_lv(src) / 100, 100 );
				} else {
					DMG_FIX( (430 + skill_lv * 90 + status_get_con(src) * 2) * status_get_lv(src) / 100, 100 );
				}
				wd.div_ += 4;
			} else {
				if(sc && sc->data[SC_INTENSIVE_AIM_COUNT].timer != -1) {
					DMG_FIX( (500 + skill_lv * 50 + status_get_con(src) * 2 + (125 + skill_lv * 25) * sc->data[SC_INTENSIVE_AIM_COUNT].val1) * status_get_lv(src) / 100, 100 );
				} else {
					DMG_FIX( (500 + skill_lv * 50 + status_get_con(src) * 2) * status_get_lv(src) / 100, 100 );
				}
			}
			break;
		case NW_WILD_FIRE:		/* ワイルドファイア */
			if(src_sd && src_sd->status.weapon == WT_GRENADE) {
				if(sc && sc->data[SC_INTENSIVE_AIM_COUNT].timer != -1) {
					DMG_FIX( (3700 + skill_lv * 600 + status_get_con(src) * 3 + (950 + skill_lv * 150) * sc->data[SC_INTENSIVE_AIM_COUNT].val1) * status_get_lv(src) / 100, 100 );
				} else {
					DMG_FIX( (3700 + skill_lv * 600 + status_get_con(src) * 3) * status_get_lv(src) / 100, 100 );
				}
			} else {
				if(sc && sc->data[SC_INTENSIVE_AIM_COUNT].timer != -1) {
					DMG_FIX( (3700 + skill_lv * 600 + status_get_con(src) * 3 + (950 + skill_lv * 150) * sc->data[SC_INTENSIVE_AIM_COUNT].val1) * status_get_lv(src) / 100, 100 );
				} else {
					DMG_FIX( (3700 + skill_lv * 600 + status_get_con(src) * 3) * status_get_lv(src) / 100, 100 );
				}
			}
			break;
		case NW_BASIC_GRENADE:		/* ベーシックグレネード */
			DMG_FIX( (3000 + skill_lv * 600 + status_get_con(src) * 5 + ((src_sd)? pc_checkskill(src_sd,NW_GRENADE_MASTERY) * 50: 0)) * status_get_lv(src) / 100, 100 );
			break;
		case NW_HASTY_FIRE_IN_THE_HOLE:	/* ヘイスティファイアインザホール */
			DMG_FIX( (3000 + skill_lv * 600 + status_get_con(src) * 2 + ((src_sd)? pc_checkskill(src_sd,NW_GRENADE_MASTERY) * 20: 0)) * status_get_lv(src) / 100, 100 );
			break;
		case NW_GRENADES_DROPPING:	/* グレネーズドロッピング */
			DMG_FIX( (1350 + skill_lv * 300 + status_get_con(src) * 3 + ((src_sd)? pc_checkskill(src_sd,NW_GRENADE_MASTERY) * 30: 0)) * status_get_lv(src) / 100, 100 );
			break;
		case NW_MISSION_BOMBARD:	/* ミッションボンバード */
			if(wflag&1) {		// 設置攻撃
				DMG_FIX( (5000 + skill_lv * 1000 + status_get_con(src) * 5 + ((src_sd)? pc_checkskill(src_sd,NW_GRENADE_MASTERY) * 30: 0)) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (2500 + skill_lv * 750 + status_get_con(src) * 5 + ((src_sd)? pc_checkskill(src_sd,NW_GRENADE_MASTERY) * 100: 0)) * status_get_lv(src) / 100, 100 );
			}
			break;
		case SH_CHUL_HO_SONIC_CLAW:	/* タイガースラッシュ */
			if((src_sd && pc_checkskill(src_sd,SH_COMMUNE_WITH_CHUL_HO)) || (sc && sc->data[SC_TEMPORARY_COMMUNION].timer != -1)) {
				DMG_FIX( (3000 + skill_lv * 500 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SH_MYSTICAL_CREATURE_MASTERY) * 150: 0)) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (2050 + skill_lv * 350 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SH_MYSTICAL_CREATURE_MASTERY) * 100: 0)) * status_get_lv(src) / 100, 100 );
			}
			break;
		case SH_HOWLING_OF_CHUL_HO:	/* タイガーハウリング */
			if((src_sd && pc_checkskill(src_sd,SH_COMMUNE_WITH_CHUL_HO)) || (sc && sc->data[SC_TEMPORARY_COMMUNION].timer != -1)) {
				DMG_FIX( (2400 + skill_lv * 300 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SH_MYSTICAL_CREATURE_MASTERY) * 100: 0)) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (1600 + skill_lv * 200 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SH_MYSTICAL_CREATURE_MASTERY) * 50: 0)) * status_get_lv(src) / 100, 100 );
			}
			break;
		case SH_HOGOGONG_STRIKE:	/* タイガーストライク */
			if((src_sd && pc_checkskill(src_sd,SH_COMMUNE_WITH_CHUL_HO)) || (sc && sc->data[SC_TEMPORARY_COMMUNION].timer != -1)) {
				DMG_FIX( (450 + skill_lv * 150 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SH_MYSTICAL_CREATURE_MASTERY) * 20: 0)) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (300 + skill_lv * 100 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SH_MYSTICAL_CREATURE_MASTERY) * 10: 0)) * status_get_lv(src) / 100, 100 );
			}
			break;
		case HN_DOUBLEBOWLINGBASH:	/* ダブルボウリングバッシュ */
			{
				static const int ss_rate[11] = { 100, 101, 103, 105, 107, 109, 111, 113, 115, 120, 125 };
				int ss_lv = (src_sd)? pc_checkskill(src_sd,HN_SELFSTUDY_TATICS): 0;
				int rate = (850 + ss_lv * 3 * skill_lv + status_get_pow(src) * 2) * status_get_lv(src) / 100;

				// 独学のダメージ倍増計算
				if(ss_lv > 10) ss_lv = 10;
				rate = rate * ss_rate[ss_lv] / 100;

				// ブレイキングリミット
				if(sc && sc->data[SC_BREAKINGLIMIT].timer != -1)
					rate = rate * 150 / 100;

				DMG_FIX( rate, 100 );
				if(wflag >= 4) {
					wd.div_ += 2;
				} else if(wflag >= 2) {
					wd.div_ += 1;
				}
			}
			break;
		case HN_MEGA_SONIC_BLOW:	/* メガソニックブロー */
			{
				static const int ss_rate[11] = { 100, 101, 103, 105, 107, 109, 111, 113, 115, 120, 125 };
				int ss_lv = (src_sd)? pc_checkskill(src_sd,HN_SELFSTUDY_TATICS): 0;
				int rate = (1650 + skill_lv * 50 + ss_lv * 5 * skill_lv + status_get_pow(src) * 4) * status_get_lv(src) / 100;

				// 独学のダメージ倍増計算
				if(ss_lv > 10) ss_lv = 10;
				rate = rate * ss_rate[ss_lv] / 100;

				// ブレイキングリミット
				if(sc && sc->data[SC_BREAKINGLIMIT].timer != -1)
					rate = rate * 150 / 100;

				DMG_FIX( rate, 100 );
				if(status_get_hp(target) < status_get_max_hp(target)/2) {
					wd.damage <<= 1;
					if(calc_flag.lh)
						wd.damage2 <<= 1;
				}
			}
			break;
		case HN_SHIELD_CHAIN_RUSH:	/* シールドチェーンラッシュ */
			{
				static const int ss_rate[11] = { 100, 101, 103, 105, 107, 109, 111, 113, 115, 120, 125 };
				int ss_lv = (src_sd)? pc_checkskill(src_sd,HN_SELFSTUDY_TATICS): 0;
				int rate = (3050 + skill_lv * 175 + ss_lv * 3 * skill_lv + status_get_pow(src) * 3) * status_get_lv(src) / 100;

				// 独学のダメージ倍増計算
				if(ss_lv > 10) ss_lv = 10;
				rate = rate * ss_rate[ss_lv] / 100;

				// ブレイキングリミット
				if(sc && sc->data[SC_BREAKINGLIMIT].timer != -1)
					rate = rate * 150 / 100;

				DMG_FIX( rate, 100 );
			}
			break;
		case HN_SPIRAL_PIERCE_MAX:	/* スパイラルピアースマックス */
			{
				static const int ss_rate[11] = { 100, 101, 103, 105, 107, 109, 111, 113, 115, 120, 125 };
				int ss_lv = (src_sd)? pc_checkskill(src_sd,HN_SELFSTUDY_TATICS): 0;
				int rate = 4475 + skill_lv * 125 + ss_lv * 3 * skill_lv;
				switch(t_size) {
					case 0: rate = rate * 150 / 100; break;		// 小型
					case 1: rate = rate * 130 / 100; break;		// 中型
					case 2: rate = rate * 120 / 100; break;		// 大型
				}
				rate = (rate + status_get_pow(src) * 3) * status_get_lv(src) / 100;		// 特性ステータスとベースレベルによる補正はサイズの影響を受けない

				// 独学のダメージ倍増計算
				if(ss_lv > 10) ss_lv = 10;
				rate = rate * ss_rate[ss_lv] / 100;

				// ブレイキングリミット
				if(sc && sc->data[SC_BREAKINGLIMIT].timer != -1)
					rate = rate * 150 / 100;

				DMG_FIX( rate, 100 );
			}
			break;
		case SKE_RISING_SUN:		/* 天地一陽 */
			DMG_FIX( (250 + skill_lv * 50 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SKE_SKY_MASTERY) * 5 * skill_lv: 0)) * status_get_lv(src) / 100, 100 );
			break;
		case SKE_NOON_BLAST:		/* 太天一陽 */
			DMG_FIX( (1125 + skill_lv * 175 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SKE_SKY_MASTERY) * 5 * skill_lv: 0)) * status_get_lv(src) / 100, 100 );
			break;
		case SKE_SUNSET_BLAST:		/* 天陽 */
			DMG_FIX( (1575 + skill_lv * 225 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SKE_SKY_MASTERY) * 5 * skill_lv: 0)) * status_get_lv(src) / 100, 100 );
			break;
		case SKE_RISING_MOON:		/* 天地一月 */
			DMG_FIX( (175 + skill_lv * 25 + status_get_pow(src) * 3 + ((src_sd)? pc_checkskill(src_sd,SKE_SKY_MASTERY) * 5 * skill_lv: 0)) * status_get_lv(src) / 100, 100 );
			break;
		case SKE_MIDNIGHT_KICK:		/* 太天一月 */
			if(sc && (sc->data[SC_MIDNIGHT_MOON].timer != -1 || sc->data[SC_SKY_ENCHANT].timer != -1)) {
				DMG_FIX( (1500 + skill_lv * 200 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SKE_SKY_MASTERY) * 5 * skill_lv: 0)) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (750 + skill_lv * 100 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SKE_SKY_MASTERY) * 5 * skill_lv: 0)) * status_get_lv(src) / 100, 100 );
			}
			break;
		case SKE_DAWN_BREAK:		/* 天月 */
			if(sc && (sc->data[SC_DAWN_MOON].timer != -1 || sc->data[SC_SKY_ENCHANT].timer != -1)) {
				DMG_FIX( (1750 + skill_lv * 300 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SKE_SKY_MASTERY) * 5 * skill_lv: 0)) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (875 + skill_lv * 150 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SKE_SKY_MASTERY) * 5 * skill_lv: 0)) * status_get_lv(src) / 100, 100 );
			}
			break;
		case SKE_TWINKLING_GALAXY:	/* 天地万星 */
			DMG_FIX( (250 + skill_lv * 100 + status_get_pow(src) * 3 + ((src_sd)? pc_checkskill(src_sd,SKE_SKY_MASTERY) * 3 * skill_lv: 0)) * status_get_lv(src) / 100, 100 );
			break;
		case SKE_STAR_BURST:		/* 天命落星 */
			DMG_FIX( (150 + skill_lv * 100 + status_get_pow(src) * 3 + ((src_sd)? pc_checkskill(src_sd,SKE_SKY_MASTERY) * 5 * skill_lv: 0)) * status_get_lv(src) / 100, 100 );
			break;
		case SKE_STAR_CANNON:		/* 天星 */
			DMG_FIX( (750 + skill_lv * 100 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SKE_SKY_MASTERY) * 5 * skill_lv: 0)) * status_get_lv(src) / 100, 100 );
			break;
		case SKE_ALL_IN_THE_SKY:	/* 天羅万象 */
			DMG_FIX( (skill_lv * 300 + status_get_pow(src) * 10) * status_get_lv(src) / 100, 100 );
			if(t_race == RCT_DEMON || t_race == RCT_DEMIHUMAN) {
				wd.div_ += 2;
			}
			break;
		case SS_SHIMIRU:			/* 影潜り */
			DMG_FIX( (2500 + skill_lv * 500 + status_get_con(src) * 5) * status_get_lv(src) / 100, 100 );
			break;
		case SS_KAGEGARI:			/* 影狩り */
			DMG_FIX( (3500 + skill_lv * 100 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SS_KAGEGISSEN) * 50: 0)) * status_get_lv(src) / 100, 100 );
			break;
		case SS_KAGENOMAI:			/* 影の舞 */
			DMG_FIX( (3500 + skill_lv * 100 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SS_KAGEGARI) * 50: 0)) * status_get_lv(src) / 100, 100 );
			if(wflag&2) {	// 分身攻撃
				wd.damage = wd.damage * 30 / 100;
				if(calc_flag.lh)
					wd.damage2 = wd.damage2 * 30 / 100;
			}
			break;
		case SS_KAGEGISSEN:			/* 影一閃 */
			DMG_FIX( (2000 + skill_lv * 100 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SS_KAGENOMAI) * 50: 0)) * status_get_lv(src) / 100, 100 );
			break;
		case SS_FUUMASHOUAKU:		/* 風魔手裏剣 -掌握- */
			DMG_FIX( (skill_lv * 50 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SS_FUUMAKOUCHIKU) * 50: 0)) * status_get_lv(src) / 100, 100 );
			break;
		case SS_FUUMAKOUCHIKU:		/* 風魔手裏剣 -構築- */
			if(wflag) {	// 爆発
				DMG_FIX( (10500 + skill_lv * 200 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SS_FUUMASHOUAKU) * 50: 0)) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (3800 + skill_lv * 100 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SS_FUUMASHOUAKU) * 50: 0)) * status_get_lv(src) / 100, 100 );
			}
			break;
		case SS_KUNAIWAIKYOKU:		/* 苦無 -歪曲- */
			DMG_FIX( (3900 + skill_lv * 100 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SS_KUNAIKUSSETSU) * 50: 0)) * status_get_lv(src) / 100, 100 );
			if(wflag&2) {	// 分身攻撃
				wd.damage = wd.damage * 30 / 100;
				if(calc_flag.lh)
					wd.damage2 = wd.damage2 * 30 / 100;
			}
			break;
		case SS_KUNAIKAITEN:		/* 苦無 -回転- */
			DMG_FIX( (1500 + skill_lv * 200 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SS_KUNAIWAIKYOKU) * 50: 0)) * status_get_lv(src) / 100, 100 );
			break;
		case SS_KUNAIKUSSETSU:		/* 苦無 -屈折- */
			DMG_FIX( (500 + skill_lv * 50 + status_get_pow(src) * 5 + ((src_sd)? pc_checkskill(src_sd,SS_KUNAIKAITEN) * 50: 0)) * status_get_lv(src) / 100, 100 );
			break;
		case SS_KAGEAKUMU:			/* 幻術 -影縫い- */
			DMG_FIX( (23000 + status_get_pow(src) * 10) * status_get_lv(src) / 100, 100 );
			if(t_sc && t_sc->data[SC_NIGHTMARE].timer != -1) {
				wd.damage = wd.damage * 150 / 100;
				if(calc_flag.lh)
					wd.damage2 = wd.damage2 * 150 / 100;
			}
			break;
		case SS_HITOUAKUMU:			/* 幻術 -苦無- */
			DMG_FIX( (23000 + status_get_pow(src) * 10) * status_get_lv(src) / 100, 100 );
			if(t_sc && t_sc->data[SC_NIGHTMARE].timer != -1) {
				wd.damage = wd.damage * 150 / 100;
				if(calc_flag.lh)
					wd.damage2 = wd.damage2 * 150 / 100;
			}
			break;
		case DK_DRAGONIC_BREATH:	/* ドラゴニックブレス */
			if(sc && sc->data[SC_DRAGONIC_AURA].timer != -1) {
				DMG_FIX( (3500 + skill_lv*400 + status_get_pow(src)*20 + status_get_max_hp(src)/500 + status_get_max_sp(src)/20) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (2750 + skill_lv*325 + status_get_pow(src)*10 + status_get_max_hp(src)/500 + status_get_max_sp(src)/20) * status_get_lv(src) / 100, 100 );
			}
			calc_flag.idef = calc_flag.idef_ = 1;
			break;
		case MT_SPARK_BLASTER:		/* スパークブラスター */
			DMG_FIX( (3750 + skill_lv*375 + status_get_pow(src)*20) * status_get_lv(src) / 100, 100 );
			calc_flag.idef = calc_flag.idef_ = 1;
			break;
		case MT_TRIPLE_LASER:		/* トリプルレーザー */
			DMG_FIX( (1100 + skill_lv*200 + status_get_pow(src)*7) * status_get_lv(src) / 100, 100 );
			break;
		case MT_MIGHTY_SMASH:		/* マイティスマッシュ */
			if(sc && sc->data[SC_AXE_STOMP].timer != -1) {
				DMG_FIX( (4700 + skill_lv*400 + status_get_pow(src)*20) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (3400 + skill_lv*350 + status_get_pow(src)*10) * status_get_lv(src) / 100, 100 );
			}
			break;
		case BO_EXPLOSIVE_POWDER:	/* エクスプロッシブパウダー */
			if(sc && sc->data[SC_RESEARCHREPORT].timer != -1) {
				DMG_FIX( (3700 + skill_lv*1000 + status_get_pow(src)*29) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (3150 + skill_lv*750 + status_get_pow(src)*23) * status_get_lv(src) / 100, 100 );
			}
			break;
		case BO_MAYHEMIC_THORNS:	/* メイヘミックソーンズ */
			if(sc && sc->data[SC_RESEARCHREPORT].timer != -1) {
				DMG_FIX( (2500 + skill_lv*200 + status_get_pow(src)*15) * status_get_lv(src) / 100, 100 );
			} else {
				DMG_FIX( (2000 + skill_lv*100 + status_get_pow(src)*10) * status_get_lv(src) / 100, 100 );
			}
			break;
		case EL_CIRCLE_OF_FIRE:	// サークルオブファイア
			DMG_FIX( 300, 100 );
			break;
		case EL_FIRE_BOMB_ATK:	// ファイアーボム(攻撃)
			DMG_FIX( 300, 100 );
			break;
		case EL_FIRE_WAVE_ATK:	// ファイアーウェーブ(攻撃)
			DMG_FIX( 600, 100 );
			break;
		case EL_WATER_SCREW_ATK:	// ウォータースクリュー(攻撃)
			DMG_FIX( 1000, 100 );
			break;
		case EL_TIDAL_WEAPON:	// タイダルウェポン
			DMG_FIX( 1500, 100 );
			break;
		case EL_WIND_SLASH:	// ウィンドスラッシュ
			DMG_FIX( 200, 100 );
			break;
		case EL_HURRICANE:	// ハリケーンレイジ
			DMG_FIX( 700, 100 );
			break;
		case EL_TYPOON_MIS:	// タイフーンミサイル
			DMG_FIX( 1000, 100 );
			break;
		case EL_STONE_HAMMER:	// ストーンハンマー
			DMG_FIX( 500, 100 );
			break;
		case EL_ROCK_CRUSHER:	// ロックランチャー
			DMG_FIX( 800, 100 );
			break;
		case EL_STONE_RAIN:	// ストーンレイン
			DMG_FIX( 800, 100 );
			break;
		case ABR_BATTLE_BUSTER:		/* バトルバスター */
			DMG_FIX( 300 * status_get_lv(src) / 100, 100 );
			calc_flag.idef = calc_flag.idef_ = 1;
			break;
		case ABR_DUAL_CANNON_FIRE:	/* デュアルキャノンファイヤー */
			DMG_FIX( 150 * status_get_lv(src) / 100, 100 );
			calc_flag.idef = calc_flag.idef_ = 1;
			break;
		case ABR_INFINITY_BUSTER:	/* インフィニティバスター */
			DMG_FIX( 500 * status_get_lv(src) / 100, 100 );
			calc_flag.idef = calc_flag.idef_ = 1;
			break;
		case 0:			// 通常攻撃
			DMG_FIX( 100, 100 );
			break;
		}
		if(skill_lv < 0)
			calc_flag.nocardfix = 1;

		/* スキル倍率計算に加算終了 */
		if(add_rate > 0)
			add_rate = 0;

		/* 13. P.Atkによる物理攻撃ダメージ増加 */
		DMG_FIX( 100 + status_get_patk(src), 100 );

#ifdef PRE_RENEWAL
		/* 14．ファイティングの追加ダメージ */
		wd.damage += tk_power_damage;
		if(calc_flag.lh)
			wd.damage2 += tk_power_damage2;

		/* 15．防御無視判定および錐効果ダメージ計算 */
		switch (skill_num) {
		case KN_AUTOCOUNTER:
		case MO_INVESTIGATE:
		case MO_EXTREMITYFIST:
		case CR_ACIDDEMONSTRATION:
		case NPC_EARTHQUAKE:
		case GS_FLING:
		case NJ_ZENYNAGE:
		case GN_FIRE_EXPANSION_ACID:
		case KO_MUCHANAGE:
			break;
		default:
			if( wd.type != 0 )	// クリティカル時は無効
				break;
			if( skill_num == WS_CARTTERMINATION && !battle_config.def_ratio_atk_to_carttermination )
				break;
			if( skill_num == PA_SHIELDCHAIN && !battle_config.def_ratio_atk_to_shieldchain )
				break;
			if(src_sd && t_def1 < 1000000)
			{
				// bIgnoreDef系判定
				ignored_rate  = ignored_rate  - src_sd->ignore_def_ele[t_ele]  - src_sd->ignore_def_race[t_race]  - src_sd->ignore_def_enemy[t_enemy];
				ignored_rate_ = ignored_rate_ - src_sd->ignore_def_ele_[t_ele] - src_sd->ignore_def_race_[t_race] - src_sd->ignore_def_enemy_[t_enemy];
				if(t_mode & MD_BOSS) {
					ignored_rate  -= src_sd->ignore_def_race[RCT_BOSS];
					ignored_rate_ -= src_sd->ignore_def_race_[RCT_BOSS];
				} else {
					ignored_rate  -= src_sd->ignore_def_race[RCT_NONBOSS];
					ignored_rate_ -= src_sd->ignore_def_race_[RCT_NONBOSS];
				}

				if(battle_config.left_cardfix_to_right) {
					// 左手カード補正設定あり
					ignored_rate -= 100 - ignored_rate_;
					ignored_rate_ = 100;
				}

				if(skill_num != CR_GRANDCROSS && skill_num != AM_ACIDTERROR) {
					int mask = (1<<t_race) | ( (t_mode&MD_BOSS)? (1<<RCT_BOSS): (1<<RCT_NONBOSS));

					// bDefRatioATK系、bIgnoreDef系が無いときのみ効果有り
					if( !calc_flag.idef && ignored_rate == 100 && (src_sd->def_ratio_atk_ele & (1<<t_ele) || src_sd->def_ratio_atk_race & mask || src_sd->def_ratio_atk_enemy & (1<<t_enemy)) ) {
						if(skill_num != ASC_BREAKER || !calc_flag.idef_) {
							wd.damage = (wd.damage * (t_def1 + t_def2)) / 100;
							calc_flag.idef = 1;
						}
					}
					if( calc_flag.lh || skill_num == ASC_BREAKER ) {
						if( !calc_flag.idef_ && ignored_rate_ == 100 && (src_sd->def_ratio_atk_ele_ & (1<<t_ele) || src_sd->def_ratio_atk_race_ & mask || src_sd->def_ratio_atk_enemy_ & (1<<t_enemy)) ) {
							if(skill_num != ASC_BREAKER || (!calc_flag.idef && battle_config.left_cardfix_to_right)) {
								wd.damage2 = (wd.damage2 * (t_def1 + t_def2)) / 100;
								calc_flag.idef_ = 1;
								if(!calc_flag.idef && battle_config.left_cardfix_to_right) {
									wd.damage = (wd.damage * (t_def1 + t_def2)) / 100;
									calc_flag.idef = 1;
								}
							}
						}
					}
				}
			}

			// グランドクロス、グランドダークネスはDEF無視を強制解除
			if(skill_num == CR_GRANDCROSS || skill_num == NPC_GRANDDARKNESS) {
				calc_flag.idef  = 0;
				calc_flag.idef_ = 0;
			}
			break;
		}
#else
		/* （RE）スキル修正５（追加ダメージ２） */
		if(src_sd && src_sd->status.weapon == WT_KATAR && skill_num != ASC_BREAKER && skill_lv >= 0) {
			// カタール研究
			if((skill = pc_checkskill(src_sd,ASC_KATAR)) > 0) {
				wd.damage += wd.damage*(10+(skill * 2))/100;
			}
		}

		/* （RE）精錬ダメージの追加 */
		if( src_sd ) {
			switch (skill_num) {
			case CR_SHIELDBOOMERANG:	// シールドブーメラン
				wd.damage = status_get_baseatk(src);
				if(src_sd->equip_index[EQUIP_INDEX_LARM] >= 0) {
					int idx = src_sd->equip_index[EQUIP_INDEX_LARM];
					if(src_sd->inventory_data[idx] && itemdb_isarmor(src_sd->inventory_data[idx]->nameid)) {
						wd.damage += src_sd->inventory_data[idx]->weight/10;
						wd.damage += src_sd->status.inventory[idx].refine * 4;
					}
				}
				break;
			}
		}
#endif

		/* 16．Resによる物理ダメージ減少 */
		if(t_res > 0) {
			int res_rate = 100;
			if(sc) {
				if(sc->data[SC_A_TELUM].timer != -1) {		// アルグトゥステルム
					res_rate -= sc->data[SC_A_TELUM].val2;
				}
				if(sc->data[SC_POTENT_VENOM].timer != -1) {		// ポテントベナム
					res_rate -= sc->data[SC_POTENT_VENOM].val2;
				}
			}

			if(res_rate < 50) {
				res_rate = 50;
			}
			if(res_rate != 100) {
				t_res = t_res * res_rate / 100;
			}
			wd.damage = wd.damage * (2000 + t_res) / (2000 + t_res * 5);
			if(calc_flag.lh)
				wd.damage2 = wd.damage2 * (2000 + t_res) / (2000 + t_res * 5);
		}

		/* 17．対象の防御力によるダメージの減少 */
		switch (skill_num) {
#ifndef PRE_RENEWAL
		case MC_CARTREVOLUTION:
		case CR_SHIELDBOOMERANG:
		case CR_GRANDCROSS:
		case NPC_GRANDDARKNESS:
		case AM_DEMONSTRATION:
		case AM_ACIDTERROR:
		case ASC_BREAKER:
		case PA_SHIELDCHAIN:
		case GS_MAGICALBULLET:
		case NJ_ISSEN:
		case NJ_TATAMIGAESHI:
#endif
		case KN_AUTOCOUNTER:
		case MO_INVESTIGATE:
		case MO_EXTREMITYFIST:
		case CR_ACIDDEMONSTRATION:
		case NJ_ZENYNAGE:
		case NPC_CRITICALSLASH:
		case NPC_VENOMFOG:
		case NPC_EARTHQUAKE:
		case GS_FLING:
		case GS_PIERCINGSHOT:
		case RA_CLUSTERBOMB:
		case RA_FIRINGTRAP:
		case RA_ICEBOUNDTRAP:
		case GC_COUNTERSLASH:
		case NC_ARMSCANNON:
		case NC_SELFDESTRUCTION:
		case GN_FIRE_EXPANSION_ACID:
		case KO_MUCHANAGE:
		case MH_EQC:
			break;
		default:
#ifdef PRE_RENEWAL
			if(wd.type != 0)	// クリティカル時は無効
				break;
#endif
			// 太陽と月と星の融合 DEF無視
			if(sc && sc->data[SC_FUSION].timer != -1)
				calc_flag.idef = 1;

			// DEF無視フラグがないとき
			if( ((calc_flag.rh && !calc_flag.idef) || (calc_flag.lh && !calc_flag.idef_)) && t_def1 < 1000000 )
			{
				int vitbonusmax;

				// エクスピアティオ
				if(sc && sc->data[SC_EXPIATIO].timer != -1 && wd.flag&BF_SHORT) {
					t_def1 -= t_def1 * sc->data[SC_EXPIATIO].val2 / 100;
				}

				// 点穴 -活-
				if(t_sc && t_sc->data[SC_GENTLETOUCH_REVITALIZE].timer != -1) {
					t_def2 += t_sc->data[SC_GENTLETOUCH_REVITALIZE].val2;
				}

				if(battle_config.vit_penalty_type > 0 && (!t_sc || t_sc->data[SC_STEELBODY].timer == -1)) {
					int target_count = 1;

					if(target->type != BL_HOM) {
						target_count = unit_counttargeted(target,battle_config.vit_penalty_count_lv);
					}
					if(target_count >= battle_config.vit_penalty_count) {
						if(battle_config.vit_penalty_type == 1) {
							int rate = 100 - (target_count - (battle_config.vit_penalty_count - 1)) * battle_config.vit_penalty_num;
							t_def1 = t_def1 * rate / 100;
							t_def2 = t_def2 * rate / 100;
							t_vit  = t_vit  * rate / 100;
						} else if(battle_config.vit_penalty_type == 2) {
							int val = (target_count - (battle_config.vit_penalty_count - 1)) * battle_config.vit_penalty_num;
							t_def1 -= val;
							t_def2 -= val;
							t_vit  -= val;
						}
						if(t_def1 < 0) t_def1 = 0;
						if(t_def2 < 1) t_def2 = 1;
						if(t_vit  < 1) t_vit  = 1;
					}
				}

#ifdef PRE_RENEWAL
				if(t_sc) {
					// フォースオブバンガード
					if(t_sc->data[SC_FORCEOFVANGUARD].timer != -1) {
						t_def1 += (t_def1 * t_sc->data[SC_FORCEOFVANGUARD].val1 * 2) / 100;
					}
					// カモフラージュ
					if(t_sc->data[SC_CAMOUFLAGE].timer != -1)
						t_def1 -= t_def1 * ((10 - t_sc->data[SC_CAMOUFLAGE].val2) * 5) / 100;
					// エコーの歌
					if(t_sc->data[SC_ECHOSONG].timer != -1) {
						// 実際には除算DEF増加だが、暫定で減算DEF
						t_def2 += t_def2 * t_sc->data[SC_ECHOSONG].val4 / 100;
					}
					// 麻痺
					if(t_sc->data[SC_PARALYZE].timer != -1) {
						t_def1 -= t_def1 * t_sc->data[SC_PARALYZE].val3 / 100;
					}
					// オーバードブースト
					if(t_sc->data[SC_OVERED_BOOST].timer != -1) {
						t_def1 /= 2;
					}
				}
				// 土符：剛塊
				if(src_sd && src_sd->elementball.num && src_sd->elementball.ele == ELE_EARTH) {
					// 実際には除算DEF増加だが、暫定で減算DEF
					t_def2 += t_def2 * src_sd->elementball.num * 10 / 100;
				}
#else
				if(t_sc) {
					short reduce = 100;
					// アスムプティオ
					if(t_sc->data[SC_ASSUMPTIO].timer != -1) {
						if(map[target->m].flag.pvp || map[target->m].flag.gvg)
							reduce += 35;	// シーズは35%
						else
							reduce += 100;	// それ以外は100%
					}
					// フォースオブバンガード
					if(t_sc->data[SC_FORCEOFVANGUARD].timer != -1) {
						reduce += t_sc->data[SC_FORCEOFVANGUARD].val1 * 2;
					}
					// エコーの歌
					if(t_sc->data[SC_ECHOSONG].timer != -1) {
						reduce += t_sc->data[SC_ECHOSONG].val4;
					}
					// カモフラージュ
					if(t_sc->data[SC_CAMOUFLAGE].timer != -1) {
						reduce -= (10 - t_sc->data[SC_CAMOUFLAGE].val2) * 5;
					}
					// 麻痺
					if(t_sc->data[SC_PARALYZE].timer != -1) {
						reduce -= t_sc->data[SC_PARALYZE].val3;
					}
					// オーバードブースト
					if(t_sc->data[SC_OVERED_BOOST].timer != -1) {
						reduce -= 50;
					}
					// 土符：剛塊
					if(src_sd && src_sd->elementball.num && src_sd->elementball.ele == ELE_EARTH) {
						reduce += src_sd->elementball.num * 10;
					}
					if(reduce)
						t_def1 = t_def1 * reduce / 100;
					else
						t_def1 = 0;
				}
#endif

				// ディバインプロテクション
				if(target_sd && (skill = pc_checkskill(target_sd,AL_DP)) > 0) {
					int s_race = status_get_race(src);
					if(battle_check_undead(s_race,status_get_elem_type(src)) || s_race == RCT_DEMON)
						t_def2 += ((300 + 4 * target_sd->status.base_level) * skill) / 100;
				}
				// レンジャーメイン
				if(target_sd && (skill = pc_checkskill(target_sd,RA_RANGERMAIN)) > 0) {
					int s_race = status_get_race(src);
					if(s_race == RCT_BRUTE || s_race == RCT_PLANT || s_race == RCT_FISH)
						t_def2 += skill * 5;
				}
				// 火と大地の研究
				if(target_sd && (skill = pc_checkskill(target_sd,NC_RESEARCHFE)) > 0) {
					int s_element = status_get_element(src)%20;
					if(s_element == ELE_FIRE || s_element ==  ELE_EARTH)
						t_def2 += skill * 10;
				}
				vitbonusmax = (t_vit / 20) * (t_vit / 20) - 1;

				if(calc_flag.rh && !calc_flag.idef) {
					if(ignored_rate > 0) {
						// bIgnoreDef右手計算
						int t_def1_fix   = t_def1 * ignored_rate / 100;
						int t_def2_fix   = t_def2 * ignored_rate / 100;
						int vitbonus_fix = vitbonusmax * ignored_rate / 100;

						if(battle_config.player_defense_type) {
							wd.damage = wd.damage - (t_def1_fix * battle_config.player_defense_type) - t_def2_fix - ((vitbonus_fix < 1)? 0: atn_rand()%(vitbonus_fix + 1) );
							damage_ot = damage_ot - (t_def1_fix * battle_config.player_defense_type) - t_def2_fix - ((vitbonus_fix < 1)? 0: atn_rand()%(vitbonus_fix + 1) );
						} else {
#ifdef PRE_RENEWAL
							wd.damage = wd.damage * (100 - t_def1_fix) /100 - t_def2_fix - ((vitbonus_fix < 1)? 0: atn_rand()%(vitbonus_fix + 1) );
							damage_ot = damage_ot * (100 - t_def1_fix) /100;
#else
							wd.damage = wd.damage * (4000 + t_def1_fix) / (4000 + 10 * t_def1_fix) - t_def2_fix;
							damage_ot = damage_ot * (4000 + t_def1_fix) / (4000 + 10 * t_def1_fix);
#endif
						}
					}
				}
				if(calc_flag.lh && !calc_flag.idef_) {
					if(ignored_rate_ > 0) {
						// bIgnoreDef左手計算
						int t_def1_fix   = t_def1 * ignored_rate_ / 100;
						int t_def2_fix   = t_def2 * ignored_rate_ / 100;
						int vitbonus_fix = vitbonusmax * ignored_rate_ / 100;

						if(battle_config.player_defense_type) {
							wd.damage2 = wd.damage2 - (t_def1_fix * battle_config.player_defense_type) - t_def2_fix - ((vitbonus_fix < 1)? 0: atn_rand()%(vitbonus_fix + 1) );
							damage_ot2 = damage_ot2 - (t_def1_fix * battle_config.player_defense_type) - t_def2_fix - ((vitbonus_fix < 1)? 0: atn_rand()%(vitbonus_fix + 1) );
						} else {
#ifdef PRE_RENEWAL
							wd.damage2 = wd.damage2 * (100 - t_def1_fix) /100 - t_def2_fix - ((vitbonus_fix < 1)? 0: atn_rand()%(vitbonus_fix + 1) );
							damage_ot2 = damage_ot2 * (100 - t_def1_fix) /100;
#else
							wd.damage2 = wd.damage2 * (4000 + t_def1_fix) / (4000 + 10 * t_def1_fix) - t_def2_fix;
							damage_ot2 = damage_ot2 * (4000 + t_def1_fix) / (4000 + 10 * t_def1_fix);
#endif
						}
					}
				}
			}
			break;
		}

		// ペインキラー
		if(t_sc && t_sc->data[SC_PAIN_KILLER].timer != -1) {
			wd.damage -= t_sc->data[SC_PAIN_KILLER].val4;
			if(calc_flag.lh)
				wd.damage2 -= t_sc->data[SC_PAIN_KILLER].val4;
		}

		/* 18．状態異常中のダメージ追加でクリティカルにも有効なスキル */
		if (sc) {
#ifdef PRE_RENEWAL
			// オーバートラスト
			if(sc->data[SC_OVERTHRUST].timer != -1) {
				wd.damage += damage_ot*sc->data[SC_OVERTHRUST].val3/100;
				if(calc_flag.lh)
					wd.damage2 += damage_ot2*sc->data[SC_OVERTHRUST].val3/100;
			}
			// オーバートラストマックス
			if(sc->data[SC_OVERTHRUSTMAX].timer != -1) {
				wd.damage += damage_ot*(20*sc->data[SC_OVERTHRUSTMAX].val1)/100;
				if(calc_flag.lh)
					wd.damage2 += damage_ot2*(20*sc->data[SC_OVERTHRUSTMAX].val1)/100;
			}
			// トゥルーサイト
			if(sc->data[SC_TRUESIGHT].timer != -1) {
				DMG_FIX( 100+2*sc->data[SC_TRUESIGHT].val1, 100 );
			}
			// バーサーク
			if(sc->data[SC_BERSERK].timer != -1) {
				DMG_FIX( 200, 100 );
			}
			// エンチャントデッドリーポイズン
			if(sc->data[SC_EDP].timer != -1 && !calc_flag.nocardfix) {
				// 右手のみに効果がのる。カード効果無効のスキルには乗らない
				if(map[src->m].flag.pk && target->type == BL_PC) {
					wd.damage *= (150 + sc->data[SC_EDP].val1 * 50) * battle_config.pk_edp_down_rate / 10000;
				} else if(map[src->m].flag.gvg) {
					wd.damage *= (150 + sc->data[SC_EDP].val1 * 50) * battle_config.gvg_edp_down_rate / 10000;
				} else if(map[src->m].flag.pvp) {
					wd.damage *= (150 + sc->data[SC_EDP].val1 * 50) * battle_config.pvp_edp_down_rate / 10000;
				} else {
					wd.damage *= (150 + sc->data[SC_EDP].val1 * 50) / 100;
				}
				// calc_flag.nocardfix = 1;
			}
#endif
			// サクリファイス
			if(sc->data[SC_SACRIFICE].timer != -1 && !skill_num) {
				if(t_class != MOBID_EMPERIUM) {
					int dmg = (int)((atn_bignumber)status_get_max_hp(src) * 9 / 100);
					battle_heal(NULL, src, -dmg, 0, 0);
					wd.damage  = dmg * (90 + sc->data[SC_SACRIFICE].val1 * 10) / 100;
					wd.damage2 = 0;
					calc_flag.da = 0;
					clif_misceffect2(src,366);
				}
				if((--sc->data[SC_SACRIFICE].val2) <= 0)
					status_change_end(src, SC_SACRIFICE,-1);
			}
			// イクシードブレイク
			else if(sc->data[SC_EXEEDBREAK].timer != -1 && !skill_num) {
				wd.damage  = wd.damage * sc->data[SC_EXEEDBREAK].val2 / 100;
				wd.damage2 = 0;
				status_change_end(src, SC_EXEEDBREAK,-1);
			}
#ifdef PRE_RENEWAL
			// エンチャントブレイド
			if(sc->data[SC_ENCHANTBLADE].timer != -1 && (!skill_num || skill_num == RA_FIRINGTRAP || skill_num == RA_ICEBOUNDTRAP) ) {
				static struct Damage ebd = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
				ebd = battle_calc_attack(BF_MAGIC,src,target,RK_ENCHANTBLADE,sc->data[SC_ENCHANTBLADE].val1,wd.flag);
				wd.damage += ebd.damage + ((100 + sc->data[SC_ENCHANTBLADE].val1 * 20) * status_get_lv(src) / 150);
			}
			// クラッシュストライク
			if(sc->data[SC_CRUSHSTRIKE].timer != -1 && wd.flag&BF_SHORT && !skill_num) {
				if(src_sd) {
					int idx = src_sd->equip_index[EQUIP_INDEX_RARM];
					if(idx >= 0 && src_sd->inventory_data[idx]) {
						wd.damage *= (6 + src_sd->status.inventory[idx].refine) * src_sd->inventory_data[idx]->wlv + (itemdb_atk(src_sd->inventory_data[idx]->nameid) + src_sd->inventory_data[idx]->weight/10) / 100;
					}

					if(atn_rand() % 100 < 20) {	// 20%の確率で武器破壊
						pc_break_equip(src_sd, LOC_RARM);
					}
				}
				status_change_end(src, SC_CRUSHSTRIKE,-1);
			}
			// ジャイアントグロース
			else if(sc->data[SC_GIANTGROWTH].timer != -1 && wd.flag&BF_SHORT && !skill_num) {
				if(atn_rand() % 10000 < 1500) {
					wd.damage *= 3;
					if(calc_flag.lh)
						wd.damage2 *= 3;
				}
				if(src_sd && atn_rand() % 10000 < 100) {
					pc_break_equip(src_sd, LOC_RARM);
				}
			}
#endif
			// シールドスペル
			if(sc->data[SC_SHIELDSPELL_REF].timer != -1 && sc->data[SC_SHIELDSPELL_REF].val2 == 1 && !skill_num) {
				if(target_sd && atn_rand() % 100 < sc->data[SC_SHIELDSPELL_REF].val3) {
					pc_break_equip(target_sd, LOC_BODY);
				}
			}
			// アクラウスダッシュ
			if(sc->data[SC_ARCLOUSEDASH].timer != -1 && src_sd && pc_isdoram(src_sd) && wd.flag&BF_LONG) {
				wd.damage += wd.damage * sc->data[SC_ARCLOUSEDASH].val3 / 100;
			}
			// チャージングスピア
			if(sc && sc->data[SC_CHARGINGPIERCE].timer != -1 && sc->data[SC_CHARGINGPIERCE_COUNT].timer != -1 && sc->data[SC_CHARGINGPIERCE_COUNT].val1 >= 10) {
				switch(skill_num) {
				case KN_PIERCE:			// ピアース
					DMG_FIX( 100 + sc->data[SC_CHARGINGPIERCE].val1 * 150, 100 );
					break;
				case LK_SPIRALPIERCE:	// スパイラルピアース
					DMG_FIX( 100 + sc->data[SC_CHARGINGPIERCE].val1 * 100, 100 );
					break;
				case RK_HUNDREDSPEAR:	// ハンドレッドスピア
					DMG_FIX( 100 + sc->data[SC_CHARGINGPIERCE].val1 * 50, 100 );
					break;
				case DK_MADNESS_CRUSHER:	// マッドネスクラッシャー
					DMG_FIX( 100 + sc->data[SC_CHARGINGPIERCE].val1 * 10, 100 );
					break;
				}
			}
			// ヴィゴール
			if(sc && sc->data[SC_VIGOR].timer != -1 && wd.flag&BF_SHORT && !skill_num) {
				DMG_FIX( 100 + sc->data[SC_VIGOR].val1 * 100, 100 );
			}
			// ドラゴニックオーラ状態中のドラゴンブレス
			if(sc && sc->data[SC_DRAGONIC_AURA].timer != -1 && (skill_num == RK_DRAGONBREATH || skill_num ==RK_DRAGONBREATH_WATER)) {
				DMG_FIX( 100 + sc->data[SC_DRAGONIC_AURA].val1 * 15, 100 );
			}
		}

#ifdef PRE_RENEWAL
		/* 19．精錬ダメージの追加 */
		if( src_sd ) {
			switch(skill_num) {
			case MO_INVESTIGATE:
			case MO_EXTREMITYFIST:
			case PA_SHIELDCHAIN:
			case CR_ACIDDEMONSTRATION:
			case GS_FLING:
			case NJ_ZENYNAGE:
			case GN_FIRE_EXPANSION_ACID:
			case KO_MUCHANAGE:
				break;
			default:
				wd.damage += status_get_atk2(src);
				if(calc_flag.lh)
					wd.damage2 += status_get_atk_2(src);
				break;
			}
			switch (skill_num) {
			case LK_SPIRALPIERCE:		// スパイラルピアース
				if(src_sd->equip_index[EQUIP_INDEX_RARM] >= 0) {	// {((STR/10)^2 ＋ 武器重量×スキル倍率×0.8) × サイズ補正 ＋ 精錬}×カード倍率×属性倍率×5の模様
					int idx = src_sd->equip_index[EQUIP_INDEX_RARM];
					if(src_sd->inventory_data[idx] && itemdb_isweapon(src_sd->inventory_data[idx]->nameid)) {
						wd.damage = ( ( (s_str/10)*(s_str/10) + src_sd->inventory_data[idx]->weight * (skill_lv * 4 + 8 ) / 100 )
									* (5 - t_size) / 4 + status_get_atk2(src) ) * 5;
					}
				}
				break;
			case PA_SHIELDCHAIN:		// シールドチェイン
				if(src_sd->equip_index[EQUIP_INDEX_LARM] >= 0) {
					int idx = src_sd->equip_index[EQUIP_INDEX_LARM];
					if(src_sd->inventory_data[idx] && itemdb_isarmor(src_sd->inventory_data[idx]->nameid)) {
						int refinedamage = 2*(src_sd->status.inventory[idx].refine-4) + src_sd->status.inventory[idx].refine * src_sd->status.inventory[idx].refine;
						wd.damage = wd.damage * (100+30*skill_lv)/100;
						if(refinedamage > 0)
							wd.damage += atn_rand() % refinedamage;
						wd.damage = (wd.damage+100) * 5;
					}
				}
				break;
			}
		}
		if(skill_num == CR_SHIELDBOOMERANG) {	// シールドブーメラン
			int rate = 100+30*skill_lv;
			if(sc && sc->data[SC_CRUSADER].timer != -1)
				rate *= 2;
			DMG_FIX( rate, 100 );
		}

		if(sc) {
			// メランコリー
			if(sc->data[SC_GLOOMYDAY].timer != -1 && (skill_num == KN_BRANDISHSPEAR || skill_num == LK_SPIRALPIERCE || skill_num == CR_SHIELDCHARGE ||
			   skill_num == CR_SHIELDBOOMERANG || skill_num == PA_SHIELDCHAIN || skill_num == LG_SHIELDPRESS)) {
				wd.damage = wd.damage * (175 + sc->data[SC_GLOOMYDAY].val1 * 25) / 100;
				if(calc_flag.lh)
					wd.damage2 = wd.damage2 * (175 + sc->data[SC_GLOOMYDAY].val1 * 25) / 100;
			}
			// カートブースト
			if(sc->data[SC_GN_CARTBOOST].timer != -1) {
				wd.damage += sc->data[SC_GN_CARTBOOST].val1 * 10;
				if(calc_flag.lh)
					wd.damage2 += sc->data[SC_GN_CARTBOOST].val1 * 10;
			}
			// プラチナムアルター
			if(sc->data[SC_P_ALTER].timer != -1) {
				wd.damage += sc->data[SC_P_ALTER].val2;
				if(calc_flag.lh)
					wd.damage2 += sc->data[SC_P_ALTER].val2;
			}
		}
#endif

		// 0未満だった場合1に補正
		if(wd.damage  < 1) wd.damage  = 1;
		if(wd.damage2 < 1) wd.damage2 = 1;

		/* 20．スキル修正２（修練系）*/
		// 修練ダメージ(右手のみ) ソニックブロー時は別処理（1撃に付き1/8適応)
		if(src_sd) {
#ifdef PRE_RENEWAL
			switch(skill_num) {
		    case MO_INVESTIGATE:
		    case MO_EXTREMITYFIST:
		    case CR_GRANDCROSS:
		    case NPC_GRANDDARKNESS:
		    case NPC_EARTHQUAKE:
		    case LK_SPIRALPIERCE:
		   	case CR_ACIDDEMONSTRATION:
			case GS_FLING:
		    case NJ_ZENYNAGE:
		    case GN_FIRE_EXPANSION_ACID:
			case KO_MUCHANAGE:
				break;
			default:
				if(skill_lv < 0)
					break;
				wd.damage = battle_addmastery(src_sd,target,wd.damage,0);
				if(calc_flag.lh)
					wd.damage2 = battle_addmastery(src_sd,target,wd.damage2,1);
				break;
			}
#endif

			if(target->type & BL_CHAR) {
				int skill = 0;
				int rate = 0;
				int tclass = status_get_class(target);

				if((skill = pc_checkskill(src_sd,SG_STAR_ANGER)) > 0 && ((sc && sc->data[SC_MIRACLE].timer != -1) || tclass == src_sd->hate_mob[2])) {	//  星の怒り
					rate = (src_sd->status.base_level + status_get_dex(src) + status_get_luk(src) + s_str) / (12 - 3 * skill);
					if(rate > skill * 70)
						rate = skill * 70;
				} else if((skill = pc_checkskill(src_sd,SG_SUN_ANGER)) > 0 && tclass == src_sd->hate_mob[0]) {		// 太陽の怒り
					rate = (src_sd->status.base_level + status_get_dex(src) + status_get_luk(src)) / (12 - 3 * skill);
					if(rate > skill * 50)
						rate = skill * 50;
				} else if((skill = pc_checkskill(src_sd,SG_MOON_ANGER)) > 0 && tclass == src_sd->hate_mob[1]) {		// 月の怒り
					rate = (src_sd->status.base_level + status_get_dex(src) + status_get_luk(src)) / (12 - 3 * skill);
					if(rate > skill * 50)
						rate = skill * 50;
				}
				if(rate > 0) {
					DMG_FIX( 100 + rate, 100 );
				}
			}
		}
		if(sc) {
			if(sc->data[SC_AURABLADE].timer != -1)		// オーラブレード
				DMG_ADD( sc->data[SC_AURABLADE].val1*20 );
		}
	}

#ifdef PRE_RENEWAL
	/* 21．スキル修正３（必中ダメージ）*/
	if( src_sd && (skill = pc_checkskill(src_sd,BS_WEAPONRESEARCH)) > 0) {
		DMG_ADD( skill*2 );
	}
#endif
	if( src_sd && (skill = pc_checkskill(src_sd,TK_RUN)) > 0) {	// タイリギパッシブで蹴りの威力加算
		if( (skill_num == TK_DOWNKICK || skill_num == TK_STORMKICK || skill_num == TK_TURNKICK || skill_num == TK_COUNTER) &&
		    src_sd->weapontype1 == WT_FIST && src_sd->weapontype2 == WT_FIST ) {
			DMG_ADD( skill*10 );
		}
	}

#ifdef PRE_RENEWAL
	/* 22．カードによるダメージ追加処理 */
	if( src_sd && wd.damage > 0 && calc_flag.rh ) {
		cardfix = 100;
		if(!calc_flag.nocardfix) {
			if(!src_sd->state.arrow_atk) {	// 弓矢以外
				if(!battle_config.left_cardfix_to_right) {	// 左手カード補正設定無し
					cardfix = cardfix*(100+src_sd->addrace[t_race])/100;	// 種族によるダメージ修正
					cardfix = cardfix*(100+src_sd->addele[t_ele])/100;	// 属性によるダメージ修正
					cardfix = cardfix*(100+src_sd->addenemy[t_enemy])/100;	// 敵タイプによるダメージ修正
					cardfix = cardfix*(100+src_sd->addsize[t_size])/100;	// サイズによるダメージ修正
					cardfix = cardfix*(100+src_sd->addgroup[t_group])/100;	// グループによるダメージ修正
				} else {
					cardfix = cardfix*(100+src_sd->addrace[t_race]+src_sd->addrace_[t_race])/100;		// 種族によるダメージ修正(左手による追加あり)
					cardfix = cardfix*(100+src_sd->addele[t_ele]+src_sd->addele_[t_ele])/100;		// 属性によるダメージ修正(左手による追加あり)
					cardfix = cardfix*(100+src_sd->addenemy[t_enemy]+src_sd->addenemy_[t_enemy])/100;	// 敵タイプによるダメージ修正(左手による追加あり)
					cardfix = cardfix*(100+src_sd->addsize[t_size]+src_sd->addsize_[t_size])/100;		// サイズによるダメージ修正(左手による追加あり)
					cardfix = cardfix*(100+src_sd->addgroup[t_group]+src_sd->addgroup_[t_group])/100;	// グループによるダメージ修正(左手による追加あり)
				}
			} else { // 弓矢
				cardfix = cardfix*(100+src_sd->addrace[t_race]+src_sd->arrow_addrace[t_race])/100;	// 種族によるダメージ修正(弓矢による追加あり)
				cardfix = cardfix*(100+src_sd->addele[t_ele]+src_sd->arrow_addele[t_ele])/100;		// 属性によるダメージ修正(弓矢による追加あり)
				cardfix = cardfix*(100+src_sd->addenemy[t_enemy]+src_sd->arrow_addenemy[t_enemy])/100;	// 敵タイプによるダメージ修正(弓矢による追加あり)
				cardfix = cardfix*(100+src_sd->addsize[t_size]+src_sd->arrow_addsize[t_size])/100;	// サイズによるダメージ修正(弓矢による追加あり)
				cardfix = cardfix*(100+src_sd->addgroup[t_group]+src_sd->arrow_addgroup[t_group])/100;	// グループによるダメージ修正(弓矢による追加あり)
			}
			if(t_mode & MD_BOSS) {	// ボス
				if(!src_sd->state.arrow_atk) {	// 弓矢攻撃以外なら
					if(!battle_config.left_cardfix_to_right) {
						// 左手カード補正設定無し
						cardfix = cardfix*(100+src_sd->addrace[RCT_BOSS])/100;					// ボスモンスターに追加ダメージ
					} else {
						// 左手カード補正設定あり
						cardfix = cardfix*(100+src_sd->addrace[RCT_BOSS]+src_sd->addrace_[RCT_BOSS])/100;	// ボスモンスターに追加ダメージ(左手による追加あり)
					}
				} else {	// 弓矢攻撃
					cardfix = cardfix*(100+src_sd->addrace[RCT_BOSS]+src_sd->arrow_addrace[RCT_BOSS])/100;		// ボスモンスターに追加ダメージ(弓矢による追加あり)
				}
			} else {		// ボスじゃない
				if(!src_sd->state.arrow_atk) {	// 弓矢攻撃以外
					if(!battle_config.left_cardfix_to_right) {
						// 左手カード補正設定無し
						cardfix = cardfix*(100+src_sd->addrace[RCT_NONBOSS])/100;				// ボス以外モンスターに追加ダメージ
					} else {
						// 左手カード補正設定あり
						cardfix = cardfix*(100+src_sd->addrace[RCT_NONBOSS]+src_sd->addrace_[RCT_NONBOSS])/100;	// ボス以外モンスターに追加ダメージ(左手による追加あり)
					}
				} else {
					cardfix = cardfix*(100+src_sd->addrace[RCT_NONBOSS]+src_sd->arrow_addrace[RCT_NONBOSS])/100;	// ボス以外モンスターに追加ダメージ(弓矢による追加あり)
				}
			}
			// カード効果による特定レンジ攻撃のダメージ増幅
			if(wd.flag&BF_SHORT) {
				cardfix = cardfix * (100+src_sd->short_weapon_damege_rate) / 100;
			}
			if(wd.flag&BF_LONG) {
				cardfix = cardfix * (100+src_sd->long_weapon_damege_rate) / 100;
			}
			// 特定Class用補正処理(少女の日記→ボンゴン用？)
			for(i=0; i<src_sd->add_damage_class_count; i++) {
				if(src_sd->add_damage_classid[i] == t_class) {
					cardfix = cardfix*(100+src_sd->add_damage_classrate[i])/100;
					break;
				}
			}
		}
		if(skill_num > 0 && wd.damage > 0) {
			int rate = 100;

			// カード効果による特定スキルのダメージ増幅（武器スキル）
			if(src_sd->skill_dmgup.count > 0) {
				for(i=0; i<src_sd->skill_dmgup.count; i++) {
					if(skill_num == src_sd->skill_dmgup.id[i]) {
						rate += src_sd->skill_dmgup.rate[i];
						break;
					}
				}
			}
			// カード効果による特定属性スキルのダメージ増幅（武器スキル）
			rate += src_sd->skill_eleweapon_dmgup[s_ele];
			cardfix = cardfix*rate/100;
		}
		wd.damage = wd.damage*cardfix/100;	// カード補正によるダメージ増加
	}

	/* 23．カードによる左手ダメージ追加処理 */
	if( src_sd && wd.damage2 > 0 && calc_flag.lh && !calc_flag.nocardfix ) {
		cardfix = 100;
		if(!battle_config.left_cardfix_to_right) {	// 左手カード補正設定無し
			cardfix = cardfix*(100+src_sd->addrace_[t_race])/100;	// 種族によるダメージ修正左手
			cardfix = cardfix*(100+src_sd->addele_[t_ele])/100;	// 属性によるダメージ修正左手
			cardfix = cardfix*(100+src_sd->addenemy_[t_enemy])/100;	// 敵タイプによるダメージ修正左手
			cardfix = cardfix*(100+src_sd->addsize_[t_size])/100;	// サイズによるダメージ修正左手
			cardfix = cardfix*(100+src_sd->addgroup_[t_group])/100;	// グループによるダメージ修正左手
			if(t_mode & MD_BOSS)	// ボス
				cardfix = cardfix*(100+src_sd->addrace_[RCT_BOSS])/100;		// ボスモンスターに追加ダメージ左手
			else
				cardfix = cardfix*(100+src_sd->addrace_[RCT_NONBOSS])/100;	// ボス以外モンスターに追加ダメージ左手
		}
		// 特定Class用補正処理左手(少女の日記→ボンゴン用？)
		for(i=0; i<src_sd->add_damage_class_count_; i++) {
			if(src_sd->add_damage_classid_[i] == t_class) {
				cardfix = cardfix*(100+src_sd->add_damage_classrate_[i])/100;
				break;
			}
		}
		wd.damage2 = wd.damage2*cardfix/100;	// カード補正による左手ダメージ増加
	}

	/* 24．ソウルブレイカーの魔法ダメージとランダムダメージ計算 */
	if(skill_num == ASC_BREAKER) {
		damage_sbr = status_get_int(src) * skill_lv * 5;	// 魔法ダメージ
		damage_sbr += 500 + (atn_rand() % 500);	// ランダムダメージ
	}

	/* 25．カードによるダメージ減衰処理 */
	if( target_sd && (wd.damage > 0 || wd.damage2 > 0 || damage_sbr > 0) ) {	// 対象がPCの場合
		int s_race  = status_get_race(src);
		int s_enemy = status_get_enemy_type(src);
		int s_size  = status_get_size(src);
		int s_group = status_get_group(src);
		int ele_type= status_get_elem_type(src);
		cardfix = 100;
		cardfix = cardfix*(100-target_sd->subrace[s_race])/100;			// 種族によるダメージ耐性
		if (s_ele == ELE_NONE)
			cardfix = cardfix*(100-target_sd->subele[ELE_NEUTRAL])/100;	// 属性無しの耐性は無属性
		else
			cardfix = cardfix*(100-target_sd->subele[s_ele])/100;		// 属性によるダメージ耐性
		if (ele_type == ELE_NONE)
			cardfix = cardfix*(100-target_sd->def_eleenemy[ELE_NEUTRAL])/100;	// 属性無しの耐性は無属性
		else
			cardfix = cardfix*(100-target_sd->def_eleenemy[ele_type])/100;		// 敵属性によるダメージ耐性
		cardfix = cardfix*(100-target_sd->subenemy[s_enemy])/100;		// 敵タイプによるダメージ耐性
		cardfix = cardfix*(100-target_sd->subsize[s_size])/100;			// サイズによるダメージ耐性
		cardfix = cardfix*(100-target_sd->subgroup[s_group])/100;		// グループによるダメージ耐性

		if(status_get_mode(src) & MD_BOSS)
			cardfix = cardfix*(100-target_sd->subrace[RCT_BOSS])/100;	// ボスからの攻撃はダメージ減少
		else
			cardfix = cardfix*(100-target_sd->subrace[RCT_NONBOSS])/100;	// ボス以外からの攻撃はダメージ減少

		// 特定Class用補正処理左手(少女の日記→ボンゴン用？)
		for(i=0; i<target_sd->add_def_class_count; i++) {
			if(target_sd->add_def_classid[i] == status_get_class(src)) {
				cardfix = cardfix*(100-target_sd->add_def_classrate[i])/100;
				break;
			}
		}
		if(wd.flag&BF_LONG && !(src_md && (skill_num == AC_SHOWER || skill_num == SN_SHARPSHOOTING)) )
			cardfix = cardfix*(100-target_sd->long_attack_def_rate)/100;	// 遠距離攻撃はダメージ減少(ホルンCとか)
		if(wd.flag&BF_SHORT)
			cardfix = cardfix*(100-target_sd->near_attack_def_rate)/100;	// 近距離攻撃はダメージ減少(該当無し？)

		DMG_FIX( cardfix, 100 );	// カード補正によるダメージ減少

		damage_sbr = damage_sbr * cardfix / 100;	// カード補正によるソウルブレイカーの魔法ダメージ減少
	}
	// エウカリスティカ
	if(target_sd && (skill = pc_checkskill(target_sd,AB_EUCHARISTICA)) > 0) {
		if(s_ele == ELE_UNDEAD || s_ele == ELE_DARK)
			DMG_FIX( 100-skill, 100 );
	}
	if(src_sd && (skill = pc_checkskill(src_sd,AB_EUCHARISTICA)) > 0) {
		if(t_ele == ELE_UNDEAD || t_ele == ELE_DARK) {
			wd.damage += wd.damage * skill / 100;
			if(calc_flag.lh)
				wd.damage2 += wd.damage2 * skill / 100;
		}
	}
#else
	if(skill_num == PA_SHIELDCHAIN && wd.damage > 0) {
		wd.damage = wd.damage - (t_def1 + t_def2);
	}
	if(wd.damage > 0) {
		int rate = 100;

		if(src_sd) {
			if(skill_num > 0) {
				// カード効果による特定スキルのダメージ増幅（武器スキル）
				if(src_sd->skill_dmgup.count > 0) {
					for(i=0; i<src_sd->skill_dmgup.count; i++) {
						if(skill_num == src_sd->skill_dmgup.id[i]) {
							rate += src_sd->skill_dmgup.rate[i];
							break;
						}
					}
				}
				// カード効果による特定属性スキルのダメージ増幅（武器スキル）
				rate += src_sd->skill_eleweapon_dmgup[s_ele];
				// ホーリークロス
				if(skill_num == CR_HOLYCROSS && src_sd->status.weapon == WT_2HSPEAR)
					rate += 100;
			}
			// ファイティング
			if((skill = pc_checkskill(src_sd,TK_POWER)) > 0) {
				rate += skill * 20;
			}
		}
		if(sc) {
			// メランコリー
			if(sc->data[SC_GLOOMYDAY].timer != -1 && (skill_num == KN_BRANDISHSPEAR || skill_num == LK_SPIRALPIERCE || skill_num == CR_SHIELDCHARGE ||
			   skill_num == CR_SHIELDBOOMERANG || skill_num == PA_SHIELDCHAIN || skill_num == LG_SHIELDPRESS)) {
				rate += 75 + sc->data[SC_GLOOMYDAY].val1 * 25;
			}
		}
		wd.damage = wd.damage*rate/100;
	}

	if(wd.damage > 0) {
		switch (skill_num) {
		case MC_CARTREVOLUTION:	// カートレボリューション
			wd.damage = wd.damage * src_sd->cart_weight/src_sd->cart_max_weight + wd.damage * 150 / 100;
			wd.damage = wd.damage - (t_def1 + t_def2);
			break;
		case CR_SHIELDBOOMERANG:
			{
				int rate = 100+30*skill_lv;
				if(sc && sc->data[SC_CRUSADER].timer != -1)
					rate *= 2;
				wd.damage = wd.damage - (t_def1 + t_def2);
				wd.damage = wd.damage * rate / 100;
			}
			break;
		case NPC_VENOMFOG:
		case NC_ARMSCANNON:
		case NC_SELFDESTRUCTION:
			wd.damage = wd.damage - (t_def1 + t_def2);
			break;
		case RA_WUGDASH:
			if(src_sd && (skill = pc_checkskill(src_sd,RA_TOOTHOFWUG)) > 0) {	// トゥースオブウォーグによるダメージ増加
				DMG_ADD( ((src_sd->weight / 10 * 5 / 4) + skill * 6) * 3 );
			}
			break;
		}
	}
#endif

	/* 26．アイテムボーナスのフラグ処理 */
	if(src_sd && wd.flag&BF_WEAPON) {
		// 状態異常のレンジフラグ
		//   addeff_range_flag  0:指定無し 1:近距離 2:遠距離 3,4:それぞれのレンジで状態異常を発動させない
		//   flagがあり、攻撃タイプとflagが一致しないときは、flag+2する

		for(i = 0; i < MAX_EFF_TYPE; i++) {
			if( (src_sd->addeff_range_flag[i] == 1 && wd.flag&BF_LONG ) ||
			    (src_sd->addeff_range_flag[i] == 2 && wd.flag&BF_SHORT) ) {
				src_sd->addeff_range_flag[i] += 2;
			}
		}
	}

	/* 27．対象にステータス異常がある場合のダメージ減算処理 */
	if( t_sc && (wd.damage > 0 || wd.damage2 > 0) ) {
		cardfix = 100;
		if(t_sc->data[SC_DEFENDER].timer != -1 && wd.flag&BF_LONG && skill_num != CR_ACIDDEMONSTRATION && skill_num != GN_FIRE_EXPANSION_ACID)	// ディフェンダー状態で遠距離攻撃
			cardfix = cardfix*(100-t_sc->data[SC_DEFENDER].val2)/100;
		if(t_sc->data[SC_ADJUSTMENT].timer != -1 && wd.flag&BF_LONG)	// アジャストメント状態で遠距離攻撃
			cardfix -= 20;
#ifdef PRE_RENEWAL
		if(t_sc->data[SC_VENOMIMPRESS].timer != -1 && s_ele == ELE_POISON)		// ベナムインプレス
			cardfix += t_sc->data[SC_VENOMIMPRESS].val2;
		if(t_sc->data[SC_ORATIO].timer != -1 && s_ele == ELE_HOLY)		// オラティオ
			cardfix += t_sc->data[SC_ORATIO].val1;
		if(t_sc->data[SC_SOULCURSE].timer != -1 && s_ele == ELE_DARK)		// 死霊憑依
			cardfix += t_sc->data[SC_SOULCURSE].val2;
		if(t_sc->data[SC_HANDICAPSTATE_LIGHTNINGSTRIKE].timer != -1 && s_ele == ELE_EARTH)		// 激流
			cardfix += t_sc->data[SC_HANDICAPSTATE_LIGHTNINGSTRIKE].val2;
		if(t_sc->data[SC_HANDICAPSTATE_DEADLYPOISON].timer != -1 && s_ele == ELE_POISON)		// 強毒
			cardfix += t_sc->data[SC_HANDICAPSTATE_DEADLYPOISON].val2;
		// クライマックスクエイク
		if(t_sc->data[SC_CLIMAX_EARTH].timer != -1 && s_ele == ELE_EARTH)
			cardfix += t_sc->data[SC_CLIMAX_EARTH].val2;
		// クライマックスブルーム
		if(t_sc->data[SC_CLIMAX_BLOOM].timer != -1 && s_ele == ELE_FIRE)
			cardfix += t_sc->data[SC_CLIMAX_BLOOM].val2;
		// ジャックフロストノヴァ
		if(t_sc->data[SC_MISTYFROST].timer != -1 && s_ele == ELE_WATER)
			cardfix += t_sc->data[SC_MISTYFROST].val2;
#endif
		if(t_sc->data[SC_DEEP_SLEEP].timer != -1)		// 安息の子守唄
			cardfix += 50;
		if(cardfix != 100) {
			DMG_FIX( cardfix, 100 );	// ステータス異常補正によるダメージ減少
		}
	}

	/* 多段ヒット計算 */
	switch(skill_num) {
	case AC_DOUBLE:		// ダブルストレイフィング
	case MA_DOUBLE:
	case KN_PIERCE:		// ピアース
	case ML_PIERCE:
	case MO_FINGEROFFENSIVE:	// 指弾
	case NPC_COMBOATTACK:	// 多段攻撃
	case GS_TRIPLEACTION:	// トリプルアクション
#ifndef PRE_RENEWAL
	case LK_SPIRALPIERCE:	// スパイラルピアース
	case PA_SHIELDCHAIN:	// シールドチェイン
#endif
	case NC_BOOSTKNUCKLE:	// ブーストナックル
	case NC_VULCANARM:		// バルカンアーム
	case NC_ARMSCANNON:		// アームズキャノン
	case SC_FATALMENACE:	// フェイタルメナス
	case LG_HESPERUSLIT:	// ヘスペルスリット
	case SR_DRAGONCOMBO:	// 双龍脚
	case RL_QD_SHOT:		// クイックドローショット
	case DK_SERVANTWEAPON_ATK:	// サーヴァントウェポン(攻撃)
	case DK_SERVANT_W_PHANTOM:	// サーヴァントウェポン：ファントム
	case DK_SERVANT_W_DEMOL:	// サーヴァントウェポン：デモリッション
	case DK_HACKANDSLASHER:		// ハックアンドスラッシャー
	case DK_HACKANDSLASHER_ATK:	// ハックアンドスラッシャー(攻撃)
	case DK_STORMSLASH:		// ストームスラッシュ
	case IQ_THIRD_FLAME_BOMB:	// 第三撃：滅火撃
	case IQ_THIRD_CONSECRATION:	// 第三撃：浄化
	case IG_OVERSLASH:		// オーバースラッシュ
	case SHC_SAVAGE_IMPACT:		// サベージインパクト
	case SHC_ETERNAL_SLASH:		// エターナルスラッシュ
	case SHC_IMPACT_CRATER:		// インパクトクレーター
	case MT_AXE_STOMP:		// アックスストンプ
	case MH_SONIC_CRAW:		// ソニッククロー
	case ABC_FRENZY_SHOT:	// フレンジショット
	case BO_ACIDIFIED_ZONE_WATER:	// アシディファイドゾーン(水)
	case BO_ACIDIFIED_ZONE_GROUND:	// アシディファイドゾーン(地)
	case BO_ACIDIFIED_ZONE_WIND:	// アシディファイドゾーン(風)
	case BO_ACIDIFIED_ZONE_FIRE:	// アシディファイドゾーン(火)
	case TR_RHYTHMSHOOTING:		// リズムシューティング
	case NW_THE_VIGILANTE_AT_NIGHT:		// ビジラントアットナイト
	case NW_SPIRAL_SHOOTING:	// スパイラルシューティング
	case NW_MAGAZINE_FOR_ONE:	// マガジンフォーワン
	case SH_HOGOGONG_STRIKE:	// タイガーストライク
	case HN_DOUBLEBOWLINGBASH:	// ダブルボウリングバッシュ
 	case SKE_ALL_IN_THE_SKY:	// 天羅万象
	case MT_TRIPLE_LASER:		// トリプルレーザー
		// Hit数分修練等が乗るタイプ
		if(wd.div_ > 1)
			wd.damage *= wd.div_;
		break;
	case AS_SONICBLOW:		// ソニックブロー
	case CR_HOLYCROSS:		// ホーリークロス
	case MO_CHAINCOMBO:		// 連打掌
	case CH_CHAINCRUSH:		// 連柱崩撃
	case CG_ARROWVULCAN:	// アローバルカン
	case RK_HUNDREDSPEAR:	// ハンドレッドスピア
	case GC_CROSSIMPACT:	// クロスインパクト
	case GC_DARKCROW:		// ダーククロー
	case RA_ARROWSTORM:		// アローストーム
	case NC_AXETORNADO:		// アックストルネード
	case SC_TRIANGLESHOT:	// トライアングルショット
	case LG_EARTHDRIVE:		// アースドライブ
	case SR_SKYNETBLOW:		// 天羅地網
	case SR_FALLENEMPIRE:	// 大纏崩捶
	case NPC_ARROWSTORM:	// Mアローストーム
		// 1Hitを分割して表示するスキル
		break;
	}

	if(wd.damage  < 0) wd.damage  = 0;
	if(wd.damage2 < 0) wd.damage2 = 0;

#ifdef PRE_RENEWAL
	/* 28．属性の適用 */
	wd.damage = battle_attr_fix(wd.damage, s_ele, status_get_element(target));
	if(calc_flag.lh)
		wd.damage2 = battle_attr_fix(wd.damage2, s_ele_, status_get_element(target));

	/* 29．スキル修正４（追加ダメージ） */
	if(sc && sc->data[SC_MAGNUM].timer != -1) {	// マグナムブレイク状態
		int bonus_damage = BIGNUM2INT(battle_attr_fix(wd.damage, ELE_FIRE, status_get_element(target)) * 20/100);	// 火属性攻撃ダメージの20%を追加
		if(bonus_damage > 0) {
			DMG_ADD( bonus_damage );
		}
	}
	if(skill_num == ASC_BREAKER) {			// ソウルブレイカー
		wd.damage += damage_sbr;	// 魔法ダメージとランダムダメージを加算
		if(t_def1 < 1000000) {
			int vitbonusmax = (t_vit/20)*(t_vit/20)-1;
			wd.damage -= (t_def1 + t_def2 + ((vitbonusmax < 1)? 0: atn_rand()%(vitbonusmax+1)) + status_get_mdef(target) + status_get_mdef2(target))/2;
		}
	}
#endif
	if(sc) {
#ifndef PRE_RENEWAL
		// マジカルバレット
		if(sc->data[SC_MAGICALBULLET].timer != -1 && !skill_num ) {
			static struct Damage ebd = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
			ebd = battle_calc_attack(BF_MAGIC,src,target,GS_MAGICALBULLET,sc->data[SC_MAGICALBULLET].val1,wd.flag);
			wd.damage += ebd.damage;
		}
		// エンチャントブレイド
		if(sc->data[SC_ENCHANTBLADE].timer != -1 && wd.damage > 0 && (!skill_num || skill_num == RA_FIRINGTRAP || skill_num == RA_ICEBOUNDTRAP) ) {
			static struct Damage ebd = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
			ebd = battle_calc_attack(BF_MAGIC,src,target,RK_ENCHANTBLADE,sc->data[SC_ENCHANTBLADE].val1,wd.flag);
			wd.damage += ebd.damage + ((100 + sc->data[SC_ENCHANTBLADE].val1 * 20) * status_get_lv(src) / 150);
		}
		// クラッシュストライク
		if(sc->data[SC_CRUSHSTRIKE].timer != -1 && wd.flag&BF_SHORT && !skill_num) {
			if(src_sd) {
				int idx = src_sd->equip_index[EQUIP_INDEX_RARM];
				if(idx >= 0 && src_sd->inventory_data[idx]) {
					wd.damage *= (6 + src_sd->status.inventory[idx].refine) * src_sd->inventory_data[idx]->wlv + (itemdb_atk(src_sd->inventory_data[idx]->nameid) + src_sd->inventory_data[idx]->weight/10) / 100;
				}

				if(atn_rand() % 100 < 20) {	// 20%の確率で武器破壊
					pc_break_equip(src_sd, LOC_RARM);
				}
			}
			status_change_end(src, SC_CRUSHSTRIKE,-1);
		}
		// ジャイアントグロース
		else if(sc->data[SC_GIANTGROWTH].timer != -1 && wd.flag&BF_SHORT && !skill_num) {
			if(atn_rand() % 10000 < 1500) {
				wd.damage *= 3;
				if(calc_flag.lh)
					wd.damage2 *= 3;
			}
			if(src_sd && atn_rand() % 10000 < 100) {
				pc_break_equip(src_sd, LOC_RARM);
			}
		}
#endif
		// スペルフィスト
		if(sc->data[SC_SPELLFIST].timer != -1 && !skill_num) {
			wd = battle_calc_attack(BF_MAGIC,src,target,sc->data[SC_SPELLFIST].val2,1,wd.flag);
			wd.damage = wd.damage * (((sc->data[SC_SPELLFIST].val1 > 5)? (sc->data[SC_SPELLFIST].val1-5)*3+5: sc->data[SC_SPELLFIST].val1) + sc->data[SC_SPELLFIST].val3);
			if((--sc->data[SC_SPELLFIST].val4) <= 0)
				status_change_end(src, SC_SPELLFIST,-1);
		}
	}

	/* 30．必中ダメージの加算 */
	switch(skill_num) {
#ifdef PRE_RENEWAL
	case NJ_SYURIKEN:	// 手裏剣投げ
		if(src_sd) {
			skill = pc_checkskill(src_sd,NJ_TOBIDOUGU);
			DMG_ADD( skill * 3 );
			if(src_sd->arrow_atk) {
				DMG_ADD( src_sd->arrow_atk );
			}
		}
		DMG_ADD( skill_lv*4 );
		break;
#endif
	case LG_SHIELDPRESS:	// シールドプレス
		if(src_sd) {
			if(src_sd->equip_index[EQUIP_INDEX_LARM] >= 0) {
				int idx = src_sd->equip_index[EQUIP_INDEX_LARM];
				if(src_sd->inventory_data[idx] && itemdb_isarmor(src_sd->inventory_data[idx]->nameid)) {
					DMG_ADD( src_sd->status.inventory[idx].refine * status_get_vit(src) );
				}
			}
		}
		break;
	case SR_FALLENEMPIRE:	// 大纏崩捶
		if(target_sd) {
			DMG_ADD( status_get_str(src) * (status_get_size(target)*2+1 + skill_lv ) + status_get_dex(src) * target_sd->weight/10 / 120 );		// 対PC
		} else {
			DMG_ADD( status_get_str(src) * (status_get_size(target)*2+1 + skill_lv ) + status_get_dex(src) * status_get_lv(target) * 50 / 120 );		// 対MOB
		}
		break;
	case SR_TIGERCANNON:	// 號砲
		if(sc && sc->data[SC_COMBO].timer != -1 && sc->data[SC_COMBO].val1 == SR_FALLENEMPIRE) {
			DMG_ADD( status_get_lv(target) * 40 + skill_lv * 500 );			// コンボ発動時
		} else {
			DMG_ADD( status_get_lv(target) * 40 + skill_lv * 240 );			// 通常時
		}
		break;
	case SR_GATEOFHELL:	// 羅刹破凰撃
		if(src_sd) {
			int sp = 0;
			if(sc && sc->data[SC_COMBO].timer != -1 && sc->data[SC_COMBO].val1 == SR_FALLENEMPIRE) {
				// コンボ発動時
				sp = (int)((atn_bignumber)status_get_max_sp(src) * skill_lv / 100);
				DMG_ADD( status_get_max_sp(src) * ( 100 + 20 * skill_lv ) / 100 + status_get_lv(src) * 20 + status_get_max_hp(src) - status_get_hp(src) );
			} else {
				// 通常時
				sp = (int)((atn_bignumber)status_get_max_sp(src) * (10 + skill_lv) / 100);
				DMG_ADD( ( status_get_sp(src) - sp ) * ( 100 + 20 * skill_lv ) / 100 + status_get_lv(src) * 10 + status_get_max_hp(src) - status_get_hp(src) );
			}
			// SP消費
			if(src_sd->status.sp > sp) {
				src_sd->status.sp -= sp;
			} else {
				src_sd->status.sp = 0;
			}
			clif_updatestatus(src_sd,SP_SP);
		}
		break;
	}

#ifdef PRE_RENEWAL
	if(src_sd) {
		// 星のかけら、気球の適用
		int hit_bonus  = src_sd->spiritball.num * 3 + src_sd->coin.num * 3 + src_sd->bonus_damage;
		int hit_damage = hit_bonus + src_sd->star + src_sd->ranker_weapon_bonus;

		if(skill_num == NJ_KUNAI) {	// 苦無投げ
			if(src_sd->arrow_atk) {
				hit_damage += src_sd->arrow_atk * 3;
			}
			wd.damage += hit_damage * 3;
		} else if(skill_num != NC_ARMSCANNON) {
			wd.damage += hit_damage;
			if(calc_flag.lh)
				wd.damage2 += hit_bonus + src_sd->star_ + src_sd->ranker_weapon_bonus_;
		}
	}
#endif

#ifndef PRE_RENEWAL
	/* （RE）クリティカル */
	if(calc_flag.critical == 2)
		wd.damage = wd.damage * (140 + status_get_crate(src)) / 100;
#endif

	/* 31．必中固定ダメージ */
	if(src_sd && src_sd->special_state.fix_damage)
		DMG_SET( src_sd->fix_damage );

	/* 32．左手ダメージの補正 */
	if(calc_flag.rh == 0 && calc_flag.lh == 1) {	// 左手のみ武器装備
		wd.damage = wd.damage2;
		wd.damage2 = 0;
		// 一応左右を入れ替えておく
		calc_flag.rh = 1;
		calc_flag.lh = 0;
	} else if(src_sd && calc_flag.lh && src_sd->status.weapon != WT_KATAR) {		// 左手があるなら右手・左手修練の適用
		atn_bignumber dmg = wd.damage, dmg2 = wd.damage2;
		// 影狼・朧の場合
		if(src_sd->s_class.job == PC_JOB_KG || src_sd->s_class.job == PC_JOB_OB) {
			// 右手修練(80% 〜 120%) 右手全般
			skill = pc_checkskill(src_sd,KO_RIGHT);
			wd.damage = wd.damage * (70 + (skill * 10))/100;
			// 左手修練(60% 〜 100%) 左手全般
			skill = pc_checkskill(src_sd,KO_LEFT);
			wd.damage2 = wd.damage2 * (50 + (skill * 10))/100;
		} else {
			// 右手修練(60% 〜 100%) 右手全般
			skill = pc_checkskill(src_sd,AS_RIGHT);
			wd.damage = wd.damage * (50 + (skill * 10))/100;
			// 左手修練(40% 〜 80%) 左手全般
			skill = pc_checkskill(src_sd,AS_LEFT);
			wd.damage2 = wd.damage2 * (30 + (skill * 10))/100;
		}
		if(dmg > 0 && wd.damage < 1) wd.damage = 1;
		if(dmg2 > 0 && wd.damage2 < 1) wd.damage2 = 1;
	} else {
		wd.damage2 = 0;	// 念のため0を明示しておく
	}

	// フィアーブリーズ連撃判定
	if(skill_num == 0 && skill_lv >= 0 && sc && sc->data[SC_FEARBREEZE].timer != -1) {
		int div_ = 0;
		int rate = atn_rand()%100;

		if(rate < 20)	// Lv1~2:2Hit確率20%
			div_ = 2;
		else if(sc->data[SC_FEARBREEZE].val1 >= 3 && rate >= 20 && rate < 35)	// Lv3:3Hit確率15%
			div_ = 3;
		else if(sc->data[SC_FEARBREEZE].val1 >= 4 && rate >= 35 && rate < 45)	// Lv4:4Hit確率10%
			div_ = 4;
		else if(sc->data[SC_FEARBREEZE].val1 >= 5 && rate >= 45 && rate < 50)	// Lv5:5Hit確率=5%
			div_ = 5;

		if(div_ > 0){
			if(src_sd && !battle_delarrow(src_sd,div_ - 1,0))	// 矢消費チェック
				div_ = 0;
			else{
				if(wd.type == 0x0a)		// クリティカル攻撃の場合は連撃クリティカル表示
					wd.type = 0x0d;
				else
					wd.type = 0x08;
				wd.div_ = div_;
				wd.damage *= div_;
			}
		}
	}
	/* 連撃ダメージ計算 */
	else if(calc_flag.da > 0) {
		if(wd.type == 0x0a)		// クリティカル攻撃の場合は連撃クリティカル表示
			wd.type = 0x0d;
		else
			wd.type = 0x08;
		wd.div_ = 2;
		wd.damage += wd.damage;
	}

	/* 33．スキル修正５（追加ダメージ２） */
	if(src_sd && src_sd->status.weapon == WT_KATAR && skill_num != ASC_BREAKER && skill_lv >= 0) {
#ifdef PRE_RENEWAL
		// カタール研究
		if((skill = pc_checkskill(src_sd,ASC_KATAR)) > 0) {
			wd.damage += wd.damage*(10+(skill * 2))/100;
		}
#endif
		// カタール追撃ダメージ
		skill = pc_checkskill(src_sd,TF_DOUBLE);
		wd.damage2 = wd.damage * (1 + (skill * 2))/100;
		if(wd.damage > 0 && wd.damage2 < 1)
			wd.damage2 = 1;
	}
#ifndef PRE_RENEWAL
	// ソニックアクセラレーション
	if(src_sd && skill_num == AS_SONICBLOW && (skill = pc_checkskill(src_sd,AS_SONICACCEL)) > 0)
		wd.damage += wd.damage * (skill * 10) / 100;
#endif
	switch(skill_num) {
#ifdef PRE_RENEWAL
	case TF_POISON:
		wd.damage = battle_attr_fix(wd.damage + 15*skill_lv, s_ele, status_get_element(target) );
		break;
#else
	case GS_MAGICALBULLET:
		wd.damage = battle_attr_fix(wd.damage, ELE_GHOST, status_get_element(target) );
		break;
	case NPC_CRITICALSLASH:
	case AM_ACIDTERROR:
	case MO_EXTREMITYFIST:
	case CR_ACIDDEMONSTRATION:
	case GN_FIRE_EXPANSION_ACID:
	case NJ_ISSEN:
	case CR_SHIELDBOOMERANG:
	case PA_SHIELDCHAIN:
#endif
	case MC_CARTREVOLUTION:
	case NC_ARMSCANNON:
	case NC_SELFDESTRUCTION:
	case LG_SHIELDPRESS:
	case SR_FALLENEMPIRE:
	case SR_TIGERCANNON:
	case SR_GATEOFHELL:
	case MH_EQC:
		wd.damage = battle_attr_fix(wd.damage, ELE_NEUTRAL, status_get_element(target) );
		break;
	}

	/* 34．完全回避の判定 */
	if(skill_num == 0 && skill_lv >= 0 && target_sd != NULL && wd.div_ < 255 && atn_rand()%1000 < status_get_flee2(target) ) {
		wd.damage  = 0;
		wd.damage2 = 0;
		wd.type    = 0x0b;
		wd.dmg_lv  = ATK_LUCKY;
	}

	if (target_md != NULL && (battle_config.enemy_perfect_flee || mobdb_search(target_md->class_)->mode_opt[MDOPT_PERFECT_FREE]))	// 対象が完全回避をする設定がONなら
	{
		if(skill_num == 0 && skill_lv >= 0 && wd.div_ < 255 && atn_rand()%1000 < status_get_flee2(target) )
		{
			wd.damage  = 0;
			wd.damage2 = 0;
			wd.type    = 0x0b;
			wd.dmg_lv  = ATK_LUCKY;
		}
	}

	/* 35．固定ダメージ2 */
	if(t_mode&MD_PLANT) {	// MobのModeに頑強フラグが立っているときの処理
		if(wd.damage > 0)
			wd.damage = 1;
		if(wd.damage2 > 0)
			wd.damage2 = 1;
		if(skill_num == MO_TRIPLEATTACK) {	// 三段掌
			wd.damage *= wd.div_;
			wd.damage2 *= wd.div_;
		}
	}

	if( target_sd && target_sd->special_state.no_weapon_damage && skill_num != CR_GRANDCROSS && skill_num != RA_CLUSTERBOMB && skill_num != RA_FIRINGTRAP && skill_num != RA_ICEBOUNDTRAP &&
	    skill_num != NPC_GRANDDARKNESS) {
		// bNoWeaponDamageでグランドクロスじゃない場合はダメージが0
		wd.damage = wd.damage2 = 0;
	}

	/* 36．ダメージ最終計算 */
	if(skill_num != CR_GRANDCROSS && skill_num != NPC_GRANDDARKNESS && skill_num != NPC_EARTHQUAKE && skill_num != RA_CLUSTERBOMB && skill_num != RA_FIRINGTRAP && skill_num != RA_ICEBOUNDTRAP) {
		if(wd.damage2 < 1) {		// ダメージ最終修正
			wd.damage  = battle_calc_damage(src,target,wd.damage,wd.div_,skill_num,skill_lv,wd.flag);
		} else if(wd.damage < 1) {	// 右手がミス？
			wd.damage2 = battle_calc_damage(src,target,wd.damage2,wd.div_,skill_num,skill_lv,wd.flag);
		} else {			// 両手、カタールの場合はちょっと計算ややこしい
			atn_bignumber dmg = wd.damage+wd.damage2;
			wd.damage  = battle_calc_damage(src,target,dmg,wd.div_,skill_num,skill_lv,wd.flag);
			wd.damage2 = (int)((wd.damage2*100/(float)dmg)*wd.damage/100);
			if(wd.damage > 1 && wd.damage2 < 1) wd.damage2 = 1;
			wd.damage -= wd.damage2;
		}
	}
	/* 37. 点穴 -球-による気弾の生成 */
	if(src_sd && sc && sc->data[SC_GENTLETOUCH_ENERGYGAIN].timer != -1 && atn_rand()%100 < sc->data[SC_GENTLETOUCH_ENERGYGAIN].val2 && (wd.damage > 0 || wd.damage2 > 0)) {
		int max = (src_sd->s_class.job == PC_JOB_MO || src_sd->s_class.job == PC_JOB_SR)? pc_checkskill(src_sd,MO_CALLSPIRITS): skill_get_max(MO_CALLSPIRITS);
		if(sc->data[SC_RAISINGDRAGON].timer != -1)
			max += sc->data[SC_RAISINGDRAGON].val1;
		if(src_sd->spiritball.num < max)
			pc_addspiritball(src_sd,skill_get_time2(SR_GENTLETOUCH_ENERGYGAIN,sc->data[SC_GENTLETOUCH_ENERGYGAIN].val1),1);
	}
	// マグマフロー
	if(t_sc && t_sc->data[SC_MAGMA_FLOW].timer != -1 && atn_rand()%100 < t_sc->data[SC_MAGMA_FLOW].val2 && (wd.damage > 0 || wd.damage2 > 0)) {
		skill_castend_damage_id(target, target, MH_MAGMA_FLOW, t_sc->data[SC_MAGMA_FLOW].val1, gettick(), 0);
	}
	// パイロクラスティック
	if(sc && sc->data[SC_PYROCLASTIC].timer != -1 && atn_rand()%100 < sc->data[SC_PYROCLASTIC].val3 && (wd.damage > 0 || wd.damage2 > 0)) {
		skill_castend_pos2(src,target->x,target->y,BS_HAMMERFALL,sc->data[SC_PYROCLASTIC].val1,gettick(),0);
	}

	/* 38．物理攻撃スキルによるオートスペル発動(item_bonus) */
	if(wd.flag&BF_SKILL && src_sd && target != &src_sd->bl && (wd.damage + wd.damage2) > 0)
	{
		unsigned int asflag = EAS_ATTACK;
		unsigned int tick = gettick();

		if(skill_num == AM_DEMONSTRATION) {
			asflag += EAS_MISC;
		} else {
			if(wd.flag&BF_LONG)
				asflag += EAS_LONG;
			else
				asflag += EAS_SHORT;
		}

		// アクティブアイテムは物理攻撃スキルでも発動する
		bonus_activeitem_start(src_sd,asflag+EAS_NORMAL,tick);

		// weapon_attack_autospell無効時でも、融合状態であればオートスペルが発動する
		if(battle_config.weapon_attack_autospell || (sc && sc->data[SC_FUSION].timer != -1))
			asflag += EAS_NORMAL;
		else
			asflag += EAS_SKILL;

		bonus_autospell_start(&src_sd->bl,target,asflag,tick,0);
	}

	/* 39．太陽と月と星の融合 HP2%消費 */
	if(src_sd && sc && sc->data[SC_FUSION].timer != -1)
	{
		int hp;
		if(src_sd->status.hp < src_sd->status.max_hp * 20 / 100)	// HPが20％未満の時に攻撃をすれば即死
			hp = src_sd->status.hp;
		else if(target->type == BL_PC)
			hp = src_sd->status.max_hp * 8 / 100;
		else
			hp = src_sd->status.max_hp * 2 / 100;

		pc_heal(src_sd,-hp,0,0,0);
	}

	/* 40．カアヒ */
	if(skill_num == 0 && wd.flag&BF_WEAPON && t_sc && t_sc->data[SC_KAAHI].timer != -1)
	{
		int kaahi_lv = t_sc->data[SC_KAAHI].val1;
		if(status_get_hp(target) < status_get_max_hp(target))
		{
			if(target->type == BL_MOB || status_get_sp(target) >= 5*kaahi_lv)	// 対象がmob以外でSPが減少量以下のときは発生しない
			{
				int heal = skill_fix_heal(src, target, SL_KAAHI, 200 * kaahi_lv);
				unit_heal(target,heal,-5*kaahi_lv,0,1);
			}
		}
	}

	/* 41．太陽と月と星の奇跡 */
	if(src_sd && wd.flag&BF_WEAPON && src_sd->s_class.job == PC_JOB_SG && atn_rand()%10000 < battle_config.sg_miracle_rate)
		status_change_start(src,SC_MIRACLE,1,0,0,0,3600000,0);

	/* 42．計算結果の最終補正 */
	if(!calc_flag.lh)
		wd.damage2 = 0;
	wd.amotion = status_get_amotion(src);
	if(skill_num == KN_AUTOCOUNTER)
		wd.amotion >>= 1;
	wd.dmotion = status_get_dmotion(target);

	return wd;
}

/*==========================================
 * 基本武器ダメージ計算
 *------------------------------------------
 */
int battle_calc_base_magic_damage(struct block_list *src)
{
	int matk1, matk2, damage = 0;
	struct status_change *sc = NULL;
#ifndef PRE_RENEWAL
	struct map_session_data *sd = NULL;
#endif

	nullpo_retr(1, src);

	matk1 = status_get_matk1(src);	// pre:最大Matk, RE:武器Matk
	matk2 = status_get_matk2(src);	// pre:最低Matk, RE:ステータスMatk

	sc = status_get_sc(src);

#ifndef PRE_RENEWAL
	if(src->type == BL_PC)
		sd = (struct map_session_data *)src;

	if(sd) {
		short wlv = 0;
		int cost  = 0;
		int int_  = status_get_int(src);
		int dex   = status_get_dex(src);
		int idx   = sd->equip_index[EQUIP_INDEX_RARM];

		damage = matk2;		// ステータスMatkを確保

		// 武器があるなら武器Lvとコスト計算
		if(idx >= 0 && sd->inventory_data[idx]) {
			wlv  = sd->inventory_data[idx]->wlv;
			cost = matk1*2/3 - (int_/5)*(int_/5)/wlv;
			if(cost < 0)	// コストは0以下にならない
				cost = 0;
		}
		// 最大武器Matkと最低武器Matk計算
		matk2 = (matk1 * 5 + dex * (12+2*wlv))/15 - cost;
		matk1 = matk1 * (100+10*wlv)/100 - cost;
		if(matk2 > matk1)
			matk2 = matk1;
	}
#endif

	// リコグナイズドスペル
	if(sc && sc->data[SC_RECOGNIZEDSPELL].timer != -1) {
		matk1 = (matk1 > matk2)? matk1: matk2;
		matk2 = (matk2 > matk1)? matk2: matk1;
	}

#ifdef PRE_RENEWAL
	if(matk1 > matk2)
		damage = matk2+atn_rand()%(matk1-matk2+1);
	else
		damage = matk2;
#else
	// ステータスMatk+武器Matk計算
	if(matk1 > matk2)
		damage += matk2+atn_rand()%(matk1-matk2+1);
	else
		damage += matk2;
	// 過剰精錬の計算
	if(sd && sd->overrefine)
		damage += (atn_rand()%sd->overrefine)+1;
#endif

	if(sc) {
		// 魔法力増幅
		if(sc->data[SC_MAGICPOWER].timer != -1)
			damage += damage * (sc->data[SC_MAGICPOWER].val1 * 5) / 100;
		// 月明かりのセレナーデ
		if(sc->data[SC_MOONLIT_SERENADE].timer != -1)
			damage += sc->data[SC_MOONLIT_SERENADE].val4;
	}

	return ((damage>0)?damage:1);
}

#define MATK_FIX( a,b ) { mgd.damage = mgd.damage*(a+(add_rate*b/100))/(b); }

/*==========================================
 * 魔法ダメージ計算
 *------------------------------------------
 */
static struct Damage battle_calc_magic_attack(struct block_list *bl,struct block_list *target,int skill_num,int skill_lv,int flag)
{
	struct Damage mgd = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	struct map_session_data *sd = NULL, *tsd = NULL;
	struct status_change    *sc = NULL, *t_sc = NULL;
	int ele, race;
	int mdef1, mdef2, mres, t_ele, t_race, t_group, t_enemy, t_size, t_mode;
	int t_class, cardfix, i;
	int normalmagic_flag = 1;
	int add_rate = 0;
#ifndef PRE_RENEWAL
	atn_bignumber heal;
#endif

	// return前の処理があるので情報出力部のみ変更
	if( bl == NULL || target == NULL || target->type == BL_PET ) {
		nullpo_info(NLP_MARK);
		return mgd;
	}

	sd   = BL_DOWNCAST( BL_PC, bl );
	tsd  = BL_DOWNCAST( BL_PC, target );

	// アタッカー
	ele  = skill_get_pl(skill_num);
	race = status_get_race(bl);
	sc   = status_get_sc(bl);

	// ターゲット
	mdef1   = status_get_mdef(target);
	mdef2   = status_get_mdef2(target);
	mres    = status_get_mres(target);
	t_ele   = status_get_elem_type(target);
	t_race  = status_get_race(target);
	t_group = status_get_group(target);
	t_enemy = status_get_enemy_type(target);
	t_size  = status_get_size(target);
	t_mode  = status_get_mode(target);
	t_sc    = status_get_sc(target);

	if(sd) {
		sd->state.attack_type = BF_MAGIC;
		sd->state.arrow_atk = 0;
	}

	// キングスグレイス
	if(t_sc && t_sc->data[SC_KINGS_GRACE].timer != -1)
		return mgd;

	/* １．mgd構造体の初期設定 */
	mgd.div_      = skill_get_num(skill_num,skill_lv);
	mgd.blewcount = skill_get_blewcount(skill_num,skill_lv);
	mgd.flag      = BF_MAGIC|BF_LONG|BF_SKILL;

	// ここから距離による判定
	switch(skill_get_range_type(skill_num)) {
		case 0:	// 近距離
		case 2: // 強制遠距離
			break;
		case 1:	// 通常遠距離
			if(battle_config.calc_dist_flag & 2) {	// 魔法の時計算するか？ +2で計算する
				int target_dist = unit_distance(bl,target);	// 距離を取得
				if(target_dist < battle_config.allow_sw_dist) {	// SWで防げる距離より小さい＝近距離からの攻撃
					if(bl->type == BL_PC && battle_config.sw_def_type & 1) {	// 人間からのを判定するか +1でする
						mgd.flag = (mgd.flag&~BF_RANGEMASK)|BF_SHORT;
						break;
					} else if(bl->type == BL_MOB && battle_config.sw_def_type & 2) {	// モンスターからの魔法を判定するか +2でする
						mgd.flag = (mgd.flag&~BF_RANGEMASK)|BF_SHORT;
						break;
					}
				}
			}
			break;
	}

	/* ２．魔法攻撃基礎計算 */
	mgd.damage = battle_calc_base_magic_damage(bl);

#ifndef PRE_RENEWAL
	// ヒールの計算用に確保
	heal = mgd.damage;

	if(sd) {
		mgd.damage += sd->plus_matk;
		// （RE）MATK乗算処理(杖補正以外)
		if(sd->matk_rate != 100)
			MATK_FIX( sd->matk_rate, 100 );
		if(pc_checkskill(sd, SU_POWEROFLAND) > 0 && pc_checkskill_summoner(sd, SU_POWEROFLAND) >= 20)
			MATK_FIX( 110, 100 );
	}

	// ファイアーピラー
	if(skill_num == WZ_FIREPILLAR) {
		if(bl->type == BL_MOB) {
			MATK_FIX( 200+100*skill_lv, 100 );
		} else {
			MATK_FIX( 40+20*skill_lv, 100 );
		}
		mgd.damage += 100+50*skill_lv;
	}

	/* （RE）カードによるダメージ追加処理 */
	if(sd && mgd.damage > 0) {
		cardfix = 100;
		cardfix = cardfix*(100+sd->magic_addrace[t_race])/100;
		cardfix = cardfix*(100+sd->magic_addele[t_ele])/100;
		cardfix = cardfix*(100+sd->magic_addenemy[t_enemy])/100;
		cardfix = cardfix*(100+sd->magic_addsize[t_size])/100;
		if(t_mode & MD_BOSS)
			cardfix = cardfix*(100+sd->magic_addrace[RCT_BOSS])/100;
		else
			cardfix = cardfix*(100+sd->magic_addrace[RCT_NONBOSS])/100;
		cardfix = cardfix*(100+sd->magic_addgroup[t_group])/100;
		t_class = status_get_class(target);
		for(i=0; i<sd->add_magic_damage_class_count; i++) {
			if(sd->add_magic_damage_classid[i] == t_class) {
				cardfix = cardfix*(100+sd->add_magic_damage_classrate[i])/100;
				break;
			}
		}
		mgd.damage = mgd.damage*cardfix/100;
	}
#endif

	/* スキル倍率計算に加算 */
	if(sc) {
#ifndef PRE_RENEWAL
		if(sc->data[SC_MINDBREAKER].timer != -1) {
			add_rate += 20*sc->data[SC_MINDBREAKER].val1;
		}
#endif
	}
	// ドラゴノロジー
	if(sd && t_race == RCT_DRAGON && pc_checkskill(sd,SA_DRAGONOLOGY) > 0)
		add_rate += pc_checkskill(sd,SA_DRAGONOLOGY)*2;

	/* ３．基本ダメージ計算(スキルごとに処理) */
	switch(skill_num)
	{
		case AL_HEAL:	// ヒールor聖体
		case PR_BENEDICTIO:
#ifdef PRE_RENEWAL
			mgd.damage = skill_calc_heal(bl,skill_lv)/2;
#else
			mgd.damage = skill_calc_heal(bl,skill_lv);
#endif
			if(sd) {	// 向上効果を乗せる
				int rate = 0;
#ifndef PRE_RENEWAL
				if(sd->skill_dmgup.count > 0) {
					for(i=0; i<sd->skill_dmgup.count; i++) {
						if(skill_num == sd->skill_dmgup.id[i]) {
							rate += sd->skill_dmgup.rate[i];
							break;
						}
					}
				}
#endif
				rate += pc_checkskill(sd,HP_MEDITATIO)*2;
				mgd.damage += mgd.damage * rate / 100;
			}
#ifndef PRE_RENEWAL
			mgd.damage += heal;	// Matkの加算
			mgd.damage = mgd.damage / 2;
#endif
			normalmagic_flag = 0;
			break;
		case PR_ASPERSIO:		// アスペルシオ
			mgd.damage = 40;	// 固定ダメージ
			normalmagic_flag = 0;
			break;
		case PR_SANCTUARY:	// サンクチュアリ
			ele = ELE_HOLY;
			mgd.damage = (skill_lv > 6)? 388: 50*skill_lv;
#ifndef PRE_RENEWAL
			if(sd && sd->skill_dmgup.count > 0) {	// 向上効果を乗せる
				for(i=0; i<sd->skill_dmgup.count; i++) {
					if(skill_num == sd->skill_dmgup.id[i]) {
						mgd.damage += mgd.damage * sd->skill_dmgup.rate[i] / 100;
						break;
					}
				}
			}
#endif
			normalmagic_flag = 0;
			mgd.blewcount |= SAB_REVERSEBLOW;
			break;
		case PA_GOSPEL:		// ゴスペル(ランダムダメージ判定の場合)
			mgd.damage = 1000+atn_rand()%9000;
			normalmagic_flag = 0;
			break;
		case ALL_RESURRECTION:
		case PR_TURNUNDEAD:	// 攻撃リザレクションとターンアンデッド
			if(battle_check_undead(t_race,t_ele)) {
				int hp, mhp, thres;
				hp = status_get_hp(target);
				mhp = status_get_max_hp(target);
				thres = skill_lv * 20 + status_get_luk(bl) + status_get_int(bl) + status_get_lv(bl) + 200 - (hp * 200 / mhp);
				if(thres > 700)
					thres = 700;
				if(atn_rand()%1000 < thres && !(t_mode&MD_BOSS))	// 成功
					mgd.damage = hp;
				else					// 失敗
					mgd.damage = status_get_lv(bl) + status_get_int(bl) + skill_lv * 10;
			}
			normalmagic_flag = 0;
			break;
		case AL_INCAGI:			/* 速度増加 */
		case MER_INCAGI:
		case AL_BLESSING:		/* ブレッシング */
		case MER_BLESSING:
			mgd.damage = 1;
			normalmagic_flag = 0;
			break;

		case HW_NAPALMVULCAN:	// ナパームバルカン
		case MG_NAPALMBEAT:	// ナパームビート（分散計算込み）
			MATK_FIX( 70+10*skill_lv, 100 );
			if(flag > 0) {
				MATK_FIX( 1, flag );
			} else {
				if(battle_config.error_log)
					printf("battle_calc_magic_attack: NAPALM enemy count=0 !\n");
			}
			break;
		case MG_SOULSTRIKE:			// ソウルストライク（対アンデッドダメージ補正）
			if(battle_check_undead(t_race,t_ele))
				MATK_FIX( 20+skill_lv, 20 );	// MATKに補正じゃ駄目ですかね？
			break;
		case MG_COLDBOLT:	// コールドボルト
			if(sc && sc->data[SC_AQUAPLAY].timer != -1) {
				MATK_FIX( 100+sc->data[SC_AQUAPLAY].val3, 100 );
			}
			break;
		case MG_FROSTDIVER:	// フロストダイバー
			if(sc && sc->data[SC_AQUAPLAY].timer != -1) {
				MATK_FIX( 100+10*skill_lv+sc->data[SC_AQUAPLAY].val3, 100 );
			} else {
				MATK_FIX( 100+10*skill_lv, 100 );
			}
			break;
		case MG_FIREBALL:	// ファイアーボール
			if(flag > 2) {
				mgd.damage = 0;
			} else {
				if(bl->type == BL_MOB && battle_config.monster_skill_over && skill_lv >= battle_config.monster_skill_over) {
					MATK_FIX( 1000, 100 );
				} else {
#ifdef PRE_RENEWAL
					MATK_FIX( 70+10*skill_lv, 100 );
#else
					MATK_FIX( 140+20*skill_lv, 100 );
#endif
				}
				if(flag == 2)
					MATK_FIX( 3, 4 );
			}
			break;
		case MG_FIREWALL:	// ファイアーウォール
			if((t_ele == ELE_FIRE || battle_check_undead(t_race,t_ele)) && target->type != BL_PC)
				mgd.blewcount = 0;
			else
				mgd.blewcount |= SAB_REVERSEBLOW;
			if(sc && sc->data[SC_PYROTECHNIC].timer != -1) {
				MATK_FIX( 50+sc->data[SC_PYROTECHNIC].val3, 100 );
			} else {
				MATK_FIX( 50, 100 );
			}
			break;
		case MG_FIREBOLT:	// ファイアーボルト
			if(sc && sc->data[SC_PYROTECHNIC].timer != -1) {
				MATK_FIX( 100+sc->data[SC_PYROTECHNIC].val3, 100 );
			}
			break;
		case MG_LIGHTNINGBOLT:	// ライトニングボルト
			if(sc && sc->data[SC_GUST].timer != -1) {
				MATK_FIX( 100+sc->data[SC_GUST].val3, 100 );
			}
			break;
		case MG_THUNDERSTORM:	// サンダーストーム
#ifdef PRE_RENEWAL
			if(sc && sc->data[SC_GUST].timer != -1) {
				MATK_FIX( 80+sc->data[SC_GUST].val3, 100 );
			} else {
				MATK_FIX( 80, 100 );
			}
#else
			if(sc && sc->data[SC_GUST].timer != -1) {
				MATK_FIX( 100+sc->data[SC_GUST].val3, 100 );
			}
#endif
			break;
		case WZ_FROSTNOVA:	// フロストノヴァ
#ifdef PRE_RENEWAL
			MATK_FIX( 200+20*skill_lv, 300 );
#else
			MATK_FIX( 100+10*skill_lv, 100 );
#endif
			break;
#ifdef PRE_RENEWAL
		case WZ_FIREPILLAR:	// ファイアーピラー
			if(mdef1 < 1000000)
				mdef1 = mdef2 = 0;	// MDEF無視
			if(bl->type == BL_MOB) {
				MATK_FIX( 200+100*skill_lv, 100 );
			} else {
				MATK_FIX( 40+20*skill_lv, 100 );
			}
			mgd.damage += 50;
			break;
#endif
		case WZ_SIGHTRASHER:
			if(bl->type == BL_MOB && battle_config.monster_skill_over && skill_lv >= battle_config.monster_skill_over) {
				MATK_FIX( 1000, 100 );
			} else {
				MATK_FIX( 100+20*skill_lv, 100 );
			}
			break;
		case WZ_METEOR:
#ifndef PRE_RENEWAL
			MATK_FIX( 125, 100 );
			break;
#endif
		case NPC_DARKTHUNDER:	// ダークサンダー
			break;
		case WZ_JUPITEL:	// ユピテルサンダー
			if(bl->type == BL_MOB && battle_config.monster_skill_over && skill_lv >= battle_config.monster_skill_over) {
				mgd.div_ = 30;
				mgd.blewcount = 14;
			}
			break;
		case WZ_VERMILION:	// ロードオブヴァーミリオン
#ifdef PRE_RENEWAL
			MATK_FIX( 80+20*skill_lv, 100 );
#else
			MATK_FIX( 100+((skill_lv-1)*25)*skill_lv/10+(skill_lv >= 10? 5: 0), 100 );
#endif
			break;
		case WZ_WATERBALL:	// ウォーターボール
			MATK_FIX( 100+30*skill_lv, 100 );
			break;
		case WZ_STORMGUST:	// ストームガスト
#ifdef PRE_RENEWAL
			MATK_FIX( 100+40*skill_lv, 100 );
#else
			MATK_FIX( 70+50*skill_lv, 100 );
#endif
			//mgd.blewcount |= SAB_REVERSEBLOW;
			break;
		case WZ_EARTHSPIKE:	// アーススパイク
			if(sc && sc->data[SC_PETROLOGY].timer != -1) {
				MATK_FIX( 100+sc->data[SC_PETROLOGY].val3, 100 );
			}
			break;
		case WZ_HEAVENDRIVE:	// ヘヴンズドライブ
#ifdef PRE_RENEWAL
			if(sc && sc->data[SC_PETROLOGY].timer != -1) {
				MATK_FIX( 100+sc->data[SC_PETROLOGY].val3, 100 );
			}
#else
			if(sc && sc->data[SC_PETROLOGY].timer != -1) {
				MATK_FIX( 125+sc->data[SC_PETROLOGY].val3, 100 );
			} else {
				MATK_FIX( 125, 100 );
			}
#endif
			break;
		case AL_HOLYLIGHT:	// ホーリーライト
			MATK_FIX( 125, 100 );
			if(sc && sc->data[SC_PRIEST].timer != -1) {
				MATK_FIX( 500, 100 );
			}
			break;
		case AL_RUWACH:
			MATK_FIX( 145, 100 );
			break;
#ifndef PRE_RENEWAL
		case CR_GRANDCROSS:	// グランドクロス
		case NPC_GRANDDARKNESS:
			{
				static struct Damage wd = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
				wd = battle_calc_weapon_attack(bl,target,skill_num,skill_lv,flag);
				if(bl == target)
					mgd.damage /= 2;	// 反動は半分
				mgd.damage = (mgd.damage + wd.damage) / 2 * (100 + 40*skill_lv)/100;
				mgd.damage = mgd.damage - (mdef1 + mdef2 + status_get_def(target) + status_get_def2(target));
				if(bl == target) {
					if(bl->type & (BL_MOB | BL_HOM | BL_MERC | BL_ELEM))
						mgd.damage = 0;		// MOB,HOM,MERC,ELEMが使う場合は反動無し
				} else {
//					if(battle_config.gx_dupele)
						mgd.damage = battle_attr_fix(mgd.damage, ele, status_get_element(target));	// 属性2回かかる
					if(sd)
						mgd.damage = mgd.damage * (100+sd->long_weapon_damege_rate) / 100;
				}
				normalmagic_flag = 0;
			}
			break;
		case AM_DEMONSTRATION:	// デモンストレーション
		case AM_ACIDTERROR:	// アシッドテラー
		case ASC_BREAKER:	// ソウルブレイカー
		case CR_ACIDDEMONSTRATION:	// アシッドデモンストレーション
		case GN_FIRE_EXPANSION_ACID:	// ファイアーエクスパンション(塩酸)
			ele = ELE_NONE;
			normalmagic_flag = 0;
			break;
#endif
		case SL_STIN:	// エスティン
			if(status_get_size(target) == 0) {
				MATK_FIX( 10*skill_lv, 100 );
			} else {
				MATK_FIX( skill_lv, 100 );
			}
			if(skill_lv >= 7)
				status_change_start(bl,SC_SMA,skill_lv,0,0,0,3000,0);
			break;
		case SL_STUN:	// エスタン
			MATK_FIX( 5*skill_lv, 100 );
			ele = status_get_attack_element(bl);
			if(skill_lv >= 7)
				status_change_start(bl,SC_SMA,skill_lv,0,0,0,3000,0);
			break;
		case SL_SMA:	// エスマ
			MATK_FIX( 40+status_get_lv(bl), 100 );
			ele = status_get_attack_element_nw(bl);
			if(sc && sc->data[SC_SMA].timer != -1)
				status_change_end(bl,SC_SMA,-1);
			break;
		case NJ_KOUENKA:	// 紅炎華
			{
				int rate = 90;
				if(sd && sd->elementball.num && sd->elementball.ele == ELE_FIRE)
					rate += sd->elementball.num * 20;
				MATK_FIX( rate, 100 );
			}
			break;
		case NJ_KAENSIN:	// 火炎陣
			{
				int rate = 50;
				if(sd && sd->elementball.num && sd->elementball.ele == ELE_FIRE)
					rate += sd->elementball.num * 20;
				MATK_FIX( rate, 100 );
			}
			break;
		case NJ_HUUJIN:		// 風刃
			{
#ifdef PRE_RENEWAL
				int rate = 100;
#else
				int rate = 150;
#endif
				if(sd && sd->elementball.num && sd->elementball.ele == ELE_WIND)
					rate += sd->elementball.num * 20;
				MATK_FIX( rate, 100 );
			}
			break;
		case NJ_HYOUSENSOU:	// 氷閃槍
			{
				int rate = 70;
				if(sd && sd->elementball.num && sd->elementball.ele == ELE_WATER)
					rate += sd->elementball.num * 20;
				if(t_sc && t_sc->data[SC_SUITON].timer != -1)
					rate += 2 * t_sc->data[SC_SUITON].val1;
				MATK_FIX( rate, 100 );
			}
			break;
		case NJ_BAKUENRYU:	// 龍炎陣
			{
				int rate = 150 + 150 * skill_lv;
				if(sd && sd->elementball.num && sd->elementball.ele == ELE_FIRE)
					rate += sd->elementball.num * 100;
				MATK_FIX( rate, 100 );
			}
			break;
		case NJ_HYOUSYOURAKU:	// 氷柱落し
			{
#ifdef PRE_RENEWAL
				int rate = 100 + 50 * skill_lv;
#else
				int rate = 150 + 150 * skill_lv;
#endif
				if(sd && sd->elementball.num && sd->elementball.ele == ELE_WATER)
					rate += sd->elementball.num * 100;
				MATK_FIX( rate, 100 );
			}
			break;
		case NJ_RAIGEKISAI:	// 雷撃砕
			{
#ifdef PRE_RENEWAL
				int rate = 160 + 40 * skill_lv;
#else
				int rate = 100 + 100 * skill_lv;
#endif
				if(sd && sd->elementball.num && sd->elementball.ele == ELE_WIND)
					rate += sd->elementball.num * 20;
				MATK_FIX( rate, 100 );
			}
			break;
		case NJ_KAMAITACHI:	// 朔風
			{
				int rate = 100 + 100 * skill_lv;
				if(sd && sd->elementball.num && sd->elementball.ele == ELE_WIND)
					rate += sd->elementball.num * 100;
				MATK_FIX( rate, 100 );
			}
			break;
		case NPC_EARTHQUAKE:	// アースクエイク
			{
				static const int dmg[10] = { 300, 500, 600, 800, 1000, 1200, 1300, 1500, 1600, 1800 };
				static struct Damage wd = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

				wd = battle_calc_weapon_attack(bl,bl,skill_num,skill_lv,flag);

				mgd.damage = wd.damage;
				if(mdef1 < 1000000)
					mdef1 = mdef2 = 0;	// MDEF無視
				if(skill_lv <= sizeof(dmg)/sizeof(dmg[0])) {
					MATK_FIX( dmg[skill_lv-1], 100 );
				} else {
					MATK_FIX( 100 + 200 * skill_lv, 100 );
				}
				if(flag > 1) {
					MATK_FIX( 1, flag );
				}
				normalmagic_flag = 0;
			}
			break;
		case NPC_EVILLAND:	// イービルランド
			mgd.damage = (skill_lv > 6)? 666: skill_lv*100;
			normalmagic_flag = 0;
			break;
		case NPC_PULSESTRIKE2:	// パルスストライクII
			mgd.damage = status_get_matk1(bl);
			normalmagic_flag = 0;
			MATK_FIX( 200, 100 );
			break;
		case NPC_FLAMECROSS:	// フレイムクロス
			if((t_ele == ELE_FIRE || battle_check_undead(t_race,t_ele)) && target->type != BL_PC)
				mgd.blewcount = 0;
			else
				mgd.blewcount |= SAB_REVERSEBLOW;
			MATK_FIX( 20 * skill_lv, 100 );		// TODO: Atk * per
			break;
		case NPC_COMET:	// Mコメット
			if(flag == 3) {			// 遠距離
				MATK_FIX( 1000 + 500 * skill_lv, 100 );
			} else if(flag == 2) {		// 中距離
				MATK_FIX( 1500 + 500 * skill_lv, 100 );
			} else if(flag == 1) {		// 近距離
				MATK_FIX( 2000 + 500 * skill_lv, 100 );
			} else {		// 中心
				MATK_FIX( 2500 + 500 * skill_lv, 100 );
			}
			break;
		case NPC_JACKFROST:		// Mジャックフロスト
			MATK_FIX( 1000 + 300 * skill_lv, 100 );
			break;
		case NPC_FIRESTORM:		// 獄炎
			MATK_FIX( 300, 100 );
			break;
		case NPC_PSYCHIC_WAVE:	// Mサイキックウェーブ
			MATK_FIX( 500 * skill_lv, 100 );
			break;
		case NPC_RAYOFGENESIS:	// Mレイオブジェネシス
			MATK_FIX( 200 * skill_lv, 100 );
			break;
		case NPC_POISON_BUSTER:	/* Mポイズンバスター */
			MATK_FIX( 1500 * skill_lv, 100 );
			break;
		case NPC_CLOUD_KILL:		/* Mクラウドキル */
			MATK_FIX( 50 * skill_lv, 100 );
			break;
		case NPC_ELECTRICWALK:	// Mエレクトリックウォーク
		case NPC_FIREWALK:		// Mファイアーウォーク
			MATK_FIX( 100 * skill_lv, 100 );
			break;
		case NPC_CANE_OF_EVIL_EYE:	// ケーンオブイビルアイ
			mgd.damage = 150000;
			normalmagic_flag = 0;
			break;
		case MH_POISON_MIST:		// ポイズンミスト
			MATK_FIX( 40 * skill_lv * status_get_lv(bl) / 100, 100 );
			break;
		case MH_ERASER_CUTTER:	// イレイサーカッター
			if(skill_lv%2) {
				ele = ELE_WIND;
				MATK_FIX( 500 + 100 * skill_lv, 100 );
			}
			else
				MATK_FIX( 800 + 100 * skill_lv, 100 );
			break;
		case MH_XENO_SLASHER:	// ゼノスラッシャー
			if(skill_lv%2) {
				ele = ELE_WIND;
				MATK_FIX( 450 + 50 * skill_lv, 100 );
			}
			else
				MATK_FIX( 500 + 100 * skill_lv, 100 );
			break;
		case MH_HEILIGE_STANGE:	// ハイリエージュスタンジェ
			MATK_FIX( 500 + 250 * skill_lv * status_get_lv(bl) / 150, 100 );
			break;
		case RK_ENCHANTBLADE:	// エンチャントブレイド
			if(sc && sc->data[SC_ENCHANTBLADE].timer != -1) {
				mgd.damage += sc->data[SC_ENCHANTBLADE].val2;
			}
			break;
		case AB_HIGHNESSHEAL:	// ハイネスヒール
			mgd.damage = skill_calc_heal(bl,10);
			if(sd) {	// 向上効果を乗せる
				int rate = 0;
#ifndef PRE_RENEWAL
				if(sd->skill_dmgup.count > 0) {
					for(i=0; i<sd->skill_dmgup.count; i++) {
						if(skill_num == sd->skill_dmgup.id[i]) {
							rate += sd->skill_dmgup.rate[i];
							break;
						}
					}
				}
#endif
				rate += pc_checkskill(sd,HP_MEDITATIO)*2;
				mgd.damage += mgd.damage * rate / 100;
			}
#ifndef PRE_RENEWAL
			mgd.damage += heal;	// Matkの加算
#endif
			mgd.damage = mgd.damage * (170 + 30 * skill_lv) / 100;
			mgd.damage = mgd.damage / 2;
			normalmagic_flag = 0;
			break;
		case AB_JUDEX:		// ジュデックス
			MATK_FIX( (360 + 48 * skill_lv) * status_get_lv(bl) / 100, 100 );
			break;
		case AB_ADORAMUS:	// アドラムス
			MATK_FIX( (500 + 100 * skill_lv) * status_get_lv(bl) / 100, 100 );
			break;
		case AB_RENOVATIO:	// レノヴァティオ
			ele = ELE_HOLY;
			mgd.damage = (status_get_lv(bl) * 10 + status_get_int(bl));
			normalmagic_flag = 0;
			break;
		case AB_DUPLELIGHT_MAGIC:	// ミリアムライト
			MATK_FIX( 200 + 20 * skill_lv, 100 );
			break;
		case WL_SOULEXPANSION:		// ソウルエクスパンション
			MATK_FIX( (status_get_int(bl) + 400 + 100 * skill_lv) * status_get_lv(bl) / 100, 100 );
			break;
		case WL_FROSTMISTY:	// フロストミスティ
			MATK_FIX( (200 + 100 * skill_lv) * status_get_lv(bl) / 100, 100 );
			break;
		case WL_JACKFROST:	// ジャックフロスト
			if(t_sc && t_sc->data[SC_FROSTMISTY].timer != -1) {
				MATK_FIX( (1000 + 300 * skill_lv) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (500 + 100 * skill_lv) * status_get_lv(bl) / 150, 100 );
			}
			break;
		case WL_DRAINLIFE:	// ドレインライフ
			MATK_FIX( (status_get_int(bl) + 200 * skill_lv) * status_get_lv(bl) / 100, 100 );
			break;
		case WL_CRIMSONROCK:	// クリムゾンロック
			MATK_FIX( 1300 + (300 * skill_lv) * status_get_lv(bl) / 100, 100 );
			break;
		case WL_HELLINFERNO:	// ヘルインフェルノ
			if(flag) {	// 闇属性
				ele = ELE_DARK;
				MATK_FIX( 240 * skill_lv * status_get_lv(bl) / 100, 100 );
			} else {	// 火属性
				MATK_FIX( 60 * skill_lv * status_get_lv(bl) / 100, 100 );
			}
			break;
		case WL_COMET:	// コメット
			if(flag == 3) {			// 遠距離
				MATK_FIX( 800 + 200 * skill_lv, 100 );
			} else if(flag == 2) {		// 中距離
				MATK_FIX( 1200 + 300 * skill_lv, 100 );
			} else if(flag == 1) {		// 近距離
				MATK_FIX( 1600 + 400 * skill_lv, 100 );
			} else {		// 中心
				MATK_FIX( 2500 + 500 * skill_lv, 100 );
			}
			break;
		case WL_CHAINLIGHTNING:		// チェーンライトニング
		case WL_CHAINLIGHTNING_ATK:	// チェーンライトニング(連鎖)
			MATK_FIX( (300 + 100 * skill_lv) + (500 + 100 * skill_lv) * status_get_lv(bl) / 100 - (flag - 1) * 100, 100 );		// flag:連鎖回数
			break;
		case WL_EARTHSTRAIN:	// アースストレイン
			MATK_FIX( (2000 + 100 * skill_lv) * status_get_lv(bl) / 100, 100 );
			break;
		case WL_TETRAVORTEX_FIRE:		/* テトラボルテックス(火) */
		case WL_TETRAVORTEX_WATER:		/* テトラボルテックス(水) */
		case WL_TETRAVORTEX_WIND:		/* テトラボルテックス(風) */
		case WL_TETRAVORTEX_GROUND:		/* テトラボルテックス(地) */
			MATK_FIX( 500 + 500 * skill_lv, 100 );
			break;
		case WL_SUMMON_ATK_FIRE:		/* サモンファイアボール(攻撃) */
		case WL_SUMMON_ATK_WIND:		/* サモンボールライトニング(攻撃) */
		case WL_SUMMON_ATK_WATER:		/* サモンウォーターボール(攻撃) */
		case WL_SUMMON_ATK_GROUND:		/* サモンストーン(攻撃) */
			MATK_FIX( (status_get_lv(bl) + status_get_jlv(bl)) * ((1 + skill_lv) / 2) * status_get_lv(bl) / 100, 100 );
			break;
		case LG_SHIELDSPELL:	// シールドスペル
			if(sd) {
				int idx = sd->equip_index[EQUIP_INDEX_LARM];
				if(idx >= 0 && sd->inventory_data[idx] && itemdb_isarmor(sd->inventory_data[idx]->nameid)) {
					MATK_FIX( status_get_lv(bl) * 4 + sd->inventory_data[idx]->mdef * 100 + status_get_int(bl) * 2, 100 );
				}
			}
			else {
				MATK_FIX( status_get_lv(bl) * 4 + status_get_int(bl) * 2, 100 );
			}
			break;
		case LG_RAYOFGENESIS:			/* レイオブジェネシス */
			MATK_FIX( (200 * skill_lv) * status_get_lv(bl) / 100, 100 );
			break;
		case WM_METALICSOUND:	/* メタリックサウンド */
			MATK_FIX( ( ( 120 * skill_lv ) + ( (sd)? pc_checkskill(sd,WM_LESSON): 0 ) * 60 ) * status_get_lv(bl) / 100, 100 );
			if(t_sc && (t_sc->data[SC_SLEEP].timer != -1 || t_sc->data[SC_DEEP_SLEEP].timer != -1)) {
				MATK_FIX( 150, 100 );
			}
			break;
		case WM_REVERBERATION_MAGIC:	/* 振動残響(魔法) */
			MATK_FIX( (100 + 100 * skill_lv) * status_get_lv(bl) / 100, 100 );
			if(flag > 1) {
				MATK_FIX( 1, flag );
			}
			break;
		case SO_FIREWALK:		/* ファイアーウォーク */
			if(sc && sc->data[SC_HEATER].timer != -1) {
				MATK_FIX( 60 * skill_lv * status_get_lv(bl) / 100 + sc->data[SC_HEATER].val3, 100 );
			} else {
				MATK_FIX( 60 * skill_lv * status_get_lv(bl) / 100, 100 );
			}
			break;
		case SO_ELECTRICWALK:	/* エレクトリックウォーク */
			if(sc && sc->data[SC_BLAST].timer != -1) {
				MATK_FIX( 90 * skill_lv + sc->data[SC_BLAST].val3, 100 );
			} else {
				MATK_FIX( 90 * skill_lv, 100 );
			}
			break;
		case SO_EARTHGRAVE:		/* アースグレイブ */
			if(sc && sc->data[SC_CURSED_SOIL].timer != -1) {
				MATK_FIX( ( ( (sd)? pc_checkskill(sd,SA_SEISMICWEAPON): 1 ) * 200 + skill_lv * status_get_int(bl) ) * status_get_lv(bl) / 100 + sc->data[SC_CURSED_SOIL].val3, 100 );
			} else {
				MATK_FIX( ( ( (sd)? pc_checkskill(sd,SA_SEISMICWEAPON): 1 ) * 200 + skill_lv * status_get_int(bl) ) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case SO_DIAMONDDUST:	/* ダイヤモンドダスト */
			if(sc && sc->data[SC_COOLER].timer != -1) {
				MATK_FIX( ( ( (sd)? pc_checkskill(sd,SA_FROSTWEAPON): 1 ) * 200 + skill_lv * status_get_int(bl) ) * status_get_lv(bl) / 100 + sc->data[SC_COOLER].val3, 100 );
			} else {
				MATK_FIX( ( ( (sd)? pc_checkskill(sd,SA_FROSTWEAPON): 1 ) * 200 + skill_lv * status_get_int(bl) ) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case SO_POISON_BUSTER:	/* ポイズンバスター */
			if(sc && sc->data[SC_CURSED_SOIL].timer != -1) {
				MATK_FIX( ( 1000 + 300 * skill_lv ) * status_get_lv(bl) / 120 + sc->data[SC_CURSED_SOIL].val3, 100 );
			} else {
				MATK_FIX( ( 1000 + 300 * skill_lv ) * status_get_lv(bl) / 120, 100 );
			}
			break;
		case SO_PSYCHIC_WAVE:	/* サイキックウェーブ */
			if(sc) {
				if(sc->data[SC_HEATER].timer != -1 || sc->data[SC_FLAMETECHNIC].timer != -1) {
					ele = ELE_FIRE;
				} else if(sc->data[SC_COOLER].timer != -1 || sc->data[SC_COLD_FORCE].timer != -1) {
					ele = ELE_WATER;
				} else if(sc->data[SC_BLAST].timer != -1 || sc->data[SC_GRACE_BREEZE].timer != -1) {
					ele = ELE_WIND;
				} else if(sc->data[SC_CURSED_SOIL].timer != -1 || sc->data[SC_EARTH_CARE].timer != -1) {
					ele = ELE_EARTH;
				} else if(sc->data[SC_DEEP_POISONING].timer != -1) {
					ele = ELE_POISON;
				}
			}
			MATK_FIX( ( 70 * skill_lv + status_get_int(bl) * 3 ) * status_get_lv(bl) / 100, 100 );
			break;
		case SO_CLOUD_KILL:		/* クラウドキル */
			if(sc && sc->data[SC_CURSED_SOIL].timer != -1) {
				MATK_FIX( ( 40 * skill_lv ) * status_get_lv(bl) / 100 + sc->data[SC_CURSED_SOIL].val3, 100 );
			} else {
				MATK_FIX( ( 40 * skill_lv ) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case SO_VARETYR_SPEAR:	/* ヴェラチュールスピア */
			if(sc && sc->data[SC_BLAST].timer != -1) {
				MATK_FIX( ( 120 * ( (sd)? (pc_checkskill(sd,SA_LIGHTNINGLOADER) + pc_checkskill(sd,SO_STRIKING)): 1 ) + status_get_int(bl) * skill_lv / 2) * status_get_lv(bl) / 100 + sc->data[SC_BLAST].val3, 100 );
			} else {
				MATK_FIX( ( 120 * ( (sd)? (pc_checkskill(sd,SA_LIGHTNINGLOADER) + pc_checkskill(sd,SO_STRIKING)): 1 ) + status_get_int(bl) * skill_lv / 2) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case GN_DEMONIC_FIRE:	// デモニックファイアー
			if(flag == 1) {		// 油
				MATK_FIX( (200 * skill_lv) + status_get_jlv(bl) + status_get_int(bl), 100 );
			} else if(flag == 2) {	// 爆発
				MATK_FIX( 200 * skill_lv + status_get_int(bl) * 10, 100 );
			} else {
				MATK_FIX( 200 * skill_lv, 100 );
			}
			break;
		case KO_KAIHOU:	/* 術式 -解放- */
			MATK_FIX( 200 * status_get_lv(bl) / 100, 100 );
			if(sd) {
				// 召喚中の球体の属性を適用する
				ele = sd->elementball.ele;
				// 召喚中の球体の数に応じてHITが変化する
				mgd.div_ = sd->elementball.num;
			} else {
				mgd.div_ = 10;
			}
			break;
		case SP_CURSEEXPLOSION:	// 死霊爆発
			if(t_sc && t_sc->data[SC_SOULCURSE].timer != -1) {
				MATK_FIX( (2500 + 250 * skill_lv) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (2300 + 50 * skill_lv) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case SP_SPA:	// エスパ
			MATK_FIX( (500 + 250 * skill_lv) * status_get_lv(bl) / 100, 100 );
			ele = status_get_attack_element_nw(bl);
			break;
		case SP_SHA:	//エスハ
			MATK_FIX( 2000 + 100 * skill_lv, 100 );
			ele = status_get_attack_element_nw(bl);
			break;
		case SP_SWHOO:	//エスフ
			MATK_FIX( (1500 + 250 * skill_lv) * status_get_lv(bl) / 100, 100 );
			ele = status_get_attack_element_nw(bl);
			break;
		case SU_SV_STEMSPEAR:	/* マタタビランス */
			{
				const int ele_type[5] = { ELE_GHOST, ELE_WATER, ELE_WIND, ELE_EARTH, ELE_FIRE };
				if(skill_lv <= 5)
					ele = ele_type[skill_lv-1];
				MATK_FIX( 5000, 100 );
			}
			break;
		case SU_CN_METEOR:	/* イヌハッカメテオ */
		case SU_CN_METEOR2:	/* イヌハッカメテオ(呪い) */
			MATK_FIX( 400 * (status_get_lv(bl) >= 100? status_get_lv(bl): 100) / 100, 100 );
			if(sc) {
				if(sc->data[SC_COLORS_OF_HYUN_ROK_1].timer != -1)
					ele = ELE_WATER;
				else if(sc->data[SC_COLORS_OF_HYUN_ROK_2].timer != -1)
					ele = ELE_WIND;
				else if(sc->data[SC_COLORS_OF_HYUN_ROK_3].timer != -1)
					ele = ELE_EARTH;
				else if(sc->data[SC_COLORS_OF_HYUN_ROK_4].timer != -1)
					ele = ELE_FIRE;
				else if(sc->data[SC_COLORS_OF_HYUN_ROK_5].timer != -1)
					ele = ELE_DARK;
				else if(sc->data[SC_COLORS_OF_HYUN_ROK_6].timer != -1)
					ele = ELE_HOLY;
			}
			break;
		case AG_DEADLY_PROJECTION:	/* デッドリープロジェクション */
			MATK_FIX( (2000 + 500 * skill_lv + status_get_spl(bl) * 15) * status_get_lv(bl) / 100, 100 );
			break;
		case AG_DESTRUCTIVE_HURRICANE:		/* ディストラクティブハリケーン */
			if(flag == 0x10) {		// クライマックスLv1：追加風属性魔法ダメージ
				MATK_FIX( 5000, 100 );
			} else {
				MATK_FIX( (8500 + 2500 * skill_lv + status_get_spl(bl) * 70) * status_get_lv(bl) / 100, 100 );
			}
			if(flag != 2) {		// クライマックスLv2：ノックバック効果
				mgd.blewcount = 0;
			}
			if(flag == 3) {		// クライマックスLv3：ダメージ+100%
				MATK_FIX( 200, 100 );
			} else if(flag == 5) {		// クライマックスLv5：ダメージ+50%
				MATK_FIX( 150, 100 );
			}
			break;
		case AG_RAIN_OF_CRYSTAL:		/* レインオブクリスタル */
			MATK_FIX( (2000 + 200 * skill_lv + status_get_spl(bl) * 10) * status_get_lv(bl) / 100, 100 );
			break;
		case AG_MYSTERY_ILLUSION:		/* ミステリーイリュージョン */
			MATK_FIX( (900 + 300 * skill_lv + status_get_spl(bl) * 8) * status_get_lv(bl) / 100, 100 );
			break;
		case AG_VIOLENT_QUAKE_ATK:		/* バイオレントクエイク(攻撃) */
			MATK_FIX( (2000 + 200 * skill_lv + status_get_spl(bl) * 10) * status_get_lv(bl) / 100, 100 );
			if(flag == 1) {			// クライマックスLv1：ダメージ-50%
				MATK_FIX( 50, 100 );
			} else if(flag == 3) {	// クライマックスLv3：ダメージ+50%
				MATK_FIX( 150, 100 );
			}
			break;
		case AG_SOUL_VC_STRIKE:		/* ソウルバルカンストライク */
			MATK_FIX( (350 + 50 * skill_lv + status_get_spl(bl) * 2) * status_get_lv(bl) / 100, 100 );
			break;
		case AG_STRANTUM_TREMOR:		/* ストラタムトレマー */
			MATK_FIX( (900 + 300 * skill_lv + status_get_spl(bl) * 8) * status_get_lv(bl) / 100, 100 );
			break;
		case AG_ALL_BLOOM_ATK:		/* オールブルーム(攻撃) */
			MATK_FIX( (2000 + 200 * skill_lv + status_get_spl(bl) * 10) * status_get_lv(bl) / 100, 100 );
			if(flag == 2) {			// クライマックスLv2：ダメージ-50%
				MATK_FIX( 50, 100 );
			} else if(flag == 3) {	// クライマックスLv3：ダメージ+50%
				MATK_FIX( 150, 100 );
			}
			break;
		case AG_ALL_BLOOM_ATK2:		/* オールブルーム(追撃) */
			MATK_FIX( 25000 + 5000 * skill_lv, 100 );
			break;
		case AG_CRYSTAL_IMPACT:		/* クリスタルインパクト */
			MATK_FIX( (8500 + 2500 * skill_lv + status_get_spl(bl) * 70) * status_get_lv(bl) / 100, 100 );
			if(flag == 2) {			// クライマックスLv2：2回連続攻撃
				mgd.div_ = 2;
			} else if(flag == 3) {	// クライマックスLv3：ダメージ+50%
				MATK_FIX( 150, 100 );
			}
			break;
		case AG_CRYSTAL_IMPACT_ATK:		/* クリスタルインパクト(追撃) */
			if(flag == 4) {			// クライマックスLv4：ダメージ+5000%
				MATK_FIX( (5100 + status_get_spl(bl)) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (100 + status_get_spl(bl)) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case AG_TORNADO_STORM:		/* トルネードストーム */
			MATK_FIX( (900 + 300 * skill_lv + status_get_spl(bl) * 8) * status_get_lv(bl) / 100, 100 );
			break;
		case AG_FLORAL_FLARE_ROAD:	/* フローラルフレアロード */
			MATK_FIX( (900 + 300 * skill_lv + status_get_spl(bl) * 8) * status_get_lv(bl) / 100, 100 );
			break;
		case AG_ASTRAL_STRIKE:		/* アストラルストライク */
			MATK_FIX( (1500 + 150 * skill_lv + (skill_lv > 5)? status_get_spl(bl) * 10: status_get_spl(bl) * 5) * status_get_lv(bl) / 100, 100 );
			break;
		case AG_ASTRAL_STRIKE_ATK:	/* アストラルストライク(初撃) */
			MATK_FIX( (7000 + 2000 * skill_lv + status_get_spl(bl) * 90) * status_get_lv(bl) / 100, 100 );
			break;
		case AG_ROCK_DOWN:		/* ロックダウン */
			if(sc && sc->data[SC_CLIMAX].timer != -1) {
				MATK_FIX( (5000 + 2500 * skill_lv + status_get_spl(bl) * 45) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (4250 + 1250 * skill_lv + status_get_spl(bl) * 35) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case AG_STORM_CANNON:		/* ストームキャノン */
			if(sc && sc->data[SC_CLIMAX].timer != -1) {
				MATK_FIX( (5000 + 2500 * skill_lv + status_get_spl(bl) * 45) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (4250 + 1250 * skill_lv + status_get_spl(bl) * 35) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case AG_CRIMSON_ARROW:		/* クリムゾンアロー(攻撃) */
			MATK_FIX( (100 * skill_lv + status_get_spl(bl) * 10) * status_get_lv(bl) / 100, 100 );
			break;
		case AG_CRIMSON_ARROW_ATK:		/* クリムゾンアロー(攻撃) */
			MATK_FIX( (1500 + 500 * skill_lv + status_get_spl(bl) * 15) * status_get_lv(bl) / 100, 100 );
			if(sc && sc->data[SC_CLIMAX].timer != -1) {
				mgd.div_ += 1;
			}
			break;
		case AG_FROZEN_SLASH:		/* フローズンスラッシュ */
			if(sc && sc->data[SC_CLIMAX].timer != -1) {
				MATK_FIX( (5000 + 2500 * skill_lv + status_get_spl(bl) * 45) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (4250 + 1250 * skill_lv + status_get_spl(bl) * 35) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case IG_JUDGEMENT_CROSS:	/* ジャッジメントクロス */
			MATK_FIX( (7000 + 2000 * skill_lv + status_get_spl(bl) * 60) * status_get_lv(bl) / 100, 100 );
			break;
		case IG_CROSS_RAIN:		/* クロスレイン */
			if(sc && sc->data[SC_HOLY_S].timer != -1) {
				MATK_FIX( (150 * skill_lv + ((sd)? pc_checkskill(sd,IG_SPEAR_SWORD_M): 0) * (skill_lv * 15) + status_get_spl(bl) * 10) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (120 * skill_lv + ((sd)? pc_checkskill(sd,IG_SPEAR_SWORD_M): 0) * (skill_lv * 5) + status_get_spl(bl) * 5) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case CD_ARBITRIUM:	/* アルビトリウム */
			MATK_FIX( (50 * skill_lv + ((sd)? pc_checkskill(sd,CD_FIDUS_ANIMUS): 0) * (skill_lv * 4) +  status_get_spl(bl) * 3) * status_get_lv(bl) / 100, 100 );
			break;
		case CD_ARBITRIUM_ATK:	/* アルビトリウム(追撃) */
			MATK_FIX( (450 * skill_lv + ((sd)? pc_checkskill(sd,CD_FIDUS_ANIMUS): 0) * (skill_lv * 30) +  status_get_spl(bl) * 25) * status_get_lv(bl) / 100, 100 );
			break;
		case CD_PNEUMATICUS_PROCELLA:	/* ニューマティックプロセラ */
			if(t_race == RCT_UNDEAD || t_race == RCT_DEMON) {
				MATK_FIX( (6000 + 1500 * skill_lv + ((sd)? pc_checkskill(sd,CD_FIDUS_ANIMUS): 0) * 5 +  status_get_spl(bl) * 70) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (5500 + 1250 * skill_lv + ((sd)? pc_checkskill(sd,CD_FIDUS_ANIMUS): 0) * 3 +  status_get_spl(bl) * 60) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case CD_FRAMEN:	/* フレーメン */
			if(t_race == RCT_UNDEAD || t_race == RCT_DEMON) {
				MATK_FIX( (900 * skill_lv + ((sd)? pc_checkskill(sd,CD_FIDUS_ANIMUS): 0) * (skill_lv * 60) +  status_get_spl(bl) * 50) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (600 * skill_lv + ((sd)? pc_checkskill(sd,CD_FIDUS_ANIMUS): 0) * (skill_lv * 30) +  status_get_spl(bl) * 30) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case ABC_FROM_THE_ABYSS_ATK:	/* フロムジアビス(攻撃) */
			MATK_FIX( (1500 + 1500 * skill_lv + status_get_spl(bl) * 30) * status_get_lv(bl) / 100, 100 );
			break;
		case ABC_ABYSS_STRIKE:	/* オメガアビスストライク */
			MATK_FIX( (7000 + 2000 * skill_lv + status_get_spl(bl) * 90) * status_get_lv(bl) / 100, 100 );
			break;
		case ABC_ABYSS_SQUARE:	/* アビススクエア */
			MATK_FIX( (150 * skill_lv + ((sd)? pc_checkskill(sd,ABC_MAGIC_SWORD_M): 0) * (skill_lv * 15) +  status_get_spl(bl) * 5) * status_get_lv(bl) / 100, 100 );
			if(flag) {
				mgd.div_ += 1;
			}
			break;
		case TR_ROSEBLOSSOM:	/* ロゼブロッサム */
			if(t_sc && t_sc->data[SC_SOUNDBLEND].timer != -1) {
				MATK_FIX( (1250 + 350 * skill_lv + status_get_con(bl) * 10) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (750 + 150 * skill_lv + status_get_con(bl) * 5) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case TR_ROSEBLOSSOM_ATK:	/* ロゼブロッサム(攻撃) */
			if(t_sc && t_sc->data[SC_SOUNDBLEND].timer != -1) {
				MATK_FIX( (2500 + 700 * skill_lv + status_get_con(bl) * 20) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (1250 + 350 * skill_lv + status_get_con(bl) * 10) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case TR_METALIC_FURY:	/* メタリックフューリー */
			if(t_sc && t_sc->data[SC_SOUNDBLEND].timer != -1) {
				MATK_FIX( (4000 + 1000 * skill_lv + status_get_spl(bl) * 30) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (1250 + 350 * skill_lv + status_get_spl(bl) * 10) * status_get_lv(bl) / 100, 100 );
			}
			if(sd) {
				int e;
				if((e = status_get_attack_element_nw(&sd->bl)) != ELE_NEUTRAL)	// 属性付与
					ele = e;
				else if(sd->arrow_ele > ELE_NEUTRAL)	// 矢の属性
					ele = sd->arrow_ele;
				else if((e = status_get_attack_element(&sd->bl)) != ELE_NEUTRAL) // 武器属性
					ele = e;
			}
			break;
		case TR_SOUNDBLEND:	/* サウンドブレンド */
			MATK_FIX( (2000 + 500 * skill_lv + status_get_spl(bl) * 15) * status_get_lv(bl) / 100, 100 );
			if(sd) {
				int e;
				if((e = status_get_attack_element_nw(&sd->bl)) != ELE_NEUTRAL)	// 属性付与
					ele = e;
				else if(sd->arrow_ele > ELE_NEUTRAL)	// 矢の属性
					ele = sd->arrow_ele;
				else if((e = status_get_attack_element(&sd->bl)) != ELE_NEUTRAL) // 武器属性
					ele = e;
			}
			break;
		case EM_DIAMOND_STORM:	/* ダイヤモンドストーム */
			if(t_sc && t_sc->data[SC_SUMMON_ELEMENTAL_DILUVIO].timer != -1) {
				MATK_FIX( (3000 + 1500 * skill_lv + 300 * skill_lv + status_get_spl(bl) * 45) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (3750 + 750 * skill_lv + status_get_spl(bl) * 25) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case EM_LIGHTNING_LAND:	/* ライトニングランド */
			if(t_sc && t_sc->data[SC_SUMMON_ELEMENTAL_PROCELLA].timer != -1) {
				MATK_FIX( (1000 + 400 * skill_lv + status_get_spl(bl) * 10) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (900 + 300 * skill_lv + status_get_spl(bl) * 8) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case EM_VENOM_SWAMP:	/* ベナムスワンプ */
			if(t_sc && t_sc->data[SC_SUMMON_ELEMENTAL_SERPENS].timer != -1) {
				MATK_FIX( (1000 + 400 * skill_lv + status_get_spl(bl) * 10) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (900 + 300 * skill_lv + status_get_spl(bl) * 8) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case EM_CONFLAGRATION:	/* コンフラグレーション */
			if(t_sc && t_sc->data[SC_SUMMON_ELEMENTAL_ARDOR].timer != -1) {
				MATK_FIX( (1000 + 400 * skill_lv + status_get_spl(bl) * 10) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (900 + 300 * skill_lv + status_get_spl(bl) * 8) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case EM_TERRA_DRIVE:	/* テラドライブ */
			if(t_sc && t_sc->data[SC_SUMMON_ELEMENTAL_TERREMOTUS].timer != -1) {
				MATK_FIX( (3000 + 1500 * skill_lv + 300 * skill_lv + status_get_spl(bl) * 45) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (3750 + 750 * skill_lv + status_get_spl(bl) * 25) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case EM_ELEMENTAL_BUSTER:			/* エレメンタルバスター */
		case EM_ELEMENTAL_BUSTER_FIRE:		/* エレメンタルバスター(火) */
		case EM_ELEMENTAL_BUSTER_WATER:		/* エレメンタルバスター(水) */
		case EM_ELEMENTAL_BUSTER_WIND:		/* エレメンタルバスター(風) */
		case EM_ELEMENTAL_BUSTER_GROUND:	/* エレメンタルバスター(地) */
		case EM_ELEMENTAL_BUSTER_POISON:	/* エレメンタルバスター(毒) */
			MATK_FIX( (10500 + 3000 * skill_lv + status_get_spl(bl) * 135) * status_get_lv(bl) / 100, 100 );
			break;
		case NPC_BO_HELL_HOWLING:	/* ヘルハウリング */
			MATK_FIX( 15000 * status_get_lv(bl) / 100, 100 );
			break;
		case NPC_BO_FAIRY_DUSTY:	/* フェアリーダスティ */
			MATK_FIX( 12500 * status_get_lv(bl) / 100, 100 );
			break;
		case SOA_TALISMAN_OF_SOUL_STEALING:		/* 霊道符 */
			MATK_FIX( (2000 + skill_lv * 200 + status_get_spl(bl) * 3 + ((sd)? (pc_checkskill(sd,SOA_TALISMAN_MASTERY) + pc_checkskill(sd,SOA_SOUL_MASTERY)) * 7 * skill_lv: 0)) * status_get_lv(bl) / 100, 100 );
			if(sc && sc->data[SC_SEVENWIND].timer != -1) {
				ele = sc->data[SC_SEVENWIND].val3;
			}
			break;
		case SOA_EXORCISM_OF_MALICIOUS_SOUL:	/* 死霊浄化 */
			if(t_sc && t_sc->data[SC_SOULCURSE].timer != -1) {
				MATK_FIX( (400 + skill_lv * 100 + status_get_spl(bl) + ((sd)? pc_checkskill(sd,SOA_SOUL_MASTERY) * 2: 0)) * ((sd)? sd->soulenergy.num: 1) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (400 + skill_lv * 50 + status_get_spl(bl) + ((sd)? pc_checkskill(sd,SOA_SOUL_MASTERY) * 2: 0)) * ((sd)? sd->soulenergy.num: 1) * status_get_lv(bl) / 100, 100 );
			}
			if(sc && sc->data[SC_SEVENWIND].timer != -1) {
				ele = sc->data[SC_SEVENWIND].val3;
			}
			break;
		case SOA_TALISMAN_OF_BLUE_DRAGON:		/* 青龍符 */
			if(sc && sc->data[SC_T_FIVETH_GOD].timer != -1) {
				MATK_FIX( (5500 + skill_lv * 1000 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SOA_TALISMAN_MASTERY) * 15 * skill_lv: 0)) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (4250 + skill_lv * 750 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SOA_TALISMAN_MASTERY) * 15 * skill_lv: 0)) * status_get_lv(bl) / 100, 100 );
			}
			if(sc && sc->data[SC_SEVENWIND].timer != -1) {
				ele = sc->data[SC_SEVENWIND].val3;
			}
			break;
		case SOA_TALISMAN_OF_WHITE_TIGER:		/* 白虎符 */
			if(sc && sc->data[SC_T_FIVETH_GOD].timer != -1) {
				MATK_FIX( (4250 + skill_lv * 500 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SOA_TALISMAN_MASTERY) * 15 * skill_lv: 0)) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (3000 + skill_lv * 400 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SOA_TALISMAN_MASTERY) * 15 * skill_lv: 0)) * status_get_lv(bl) / 100, 100 );
			}
			if(sc && sc->data[SC_SEVENWIND].timer != -1) {
				ele = sc->data[SC_SEVENWIND].val3;
			}
			break;
		case SOA_TALISMAN_OF_RED_PHOENIX:		/* 朱雀符 */
			if(sc && sc->data[SC_T_FIVETH_GOD].timer != -1) {
				MATK_FIX( (5500 + skill_lv * 650 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SOA_TALISMAN_MASTERY) * 15 * skill_lv: 0)) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (4250 + skill_lv * 500 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SOA_TALISMAN_MASTERY) * 15 * skill_lv: 0)) * status_get_lv(bl) / 100, 100 );
			}
			if(sc && sc->data[SC_SEVENWIND].timer != -1) {
				ele = sc->data[SC_SEVENWIND].val3;
			}
			break;
		case SOA_TALISMAN_OF_BLACK_TORTOISE:	/* 玄武符 */
			if(sc && sc->data[SC_T_FIVETH_GOD].timer != -1) {
				MATK_FIX( (4250 + skill_lv * 500 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SOA_TALISMAN_MASTERY) * 15 * skill_lv: 0)) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (3000 + skill_lv * 400 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SOA_TALISMAN_MASTERY) * 15 * skill_lv: 0)) * status_get_lv(bl) / 100, 100 );
			}
			if(sc && sc->data[SC_SEVENWIND].timer != -1) {
				ele = sc->data[SC_SEVENWIND].val3;
			}
			break;
		case SOA_TALISMAN_OF_FOUR_BEARING_GOD:	/* 四方神符 */
			MATK_FIX( (500 + skill_lv * 50 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SOA_TALISMAN_MASTERY) * 15 * skill_lv: 0)) * status_get_lv(bl) / 100, 100 );
			if(sc) {
				if(sc->data[SC_T_FIRST_GOD].timer != -1) {
					mgd.div_ += 1;
				} else if(sc->data[SC_T_SECOND_GOD].timer != -1){
					mgd.div_ += 2;
				} else if(sc->data[SC_T_THIRD_GOD].timer != -1){
					mgd.div_ += 3;
				} else if(sc->data[SC_T_FOURTH_GOD].timer != -1){
					mgd.div_ += 4;
				} else if(sc->data[SC_T_FIVETH_GOD].timer != -1){
					mgd.div_ += 5;
				}
				if(sc->data[SC_SEVENWIND].timer != -1) {
					ele = sc->data[SC_SEVENWIND].val3;
				}
			}
			break;
		case SOA_CIRCLE_OF_DIRECTIONS_AND_ELEMENTALS:	/* 四方五行陣 */
			MATK_FIX( (2200 + skill_lv * 600 + status_get_spl(bl) * 5 + ((sd)? (pc_checkskill(sd,SOA_TALISMAN_MASTERY) + pc_checkskill(sd,SOA_SOUL_MASTERY)) * 15 * skill_lv: 0)) * status_get_lv(bl) / 100, 100 );
			if(sc && sc->data[SC_SEVENWIND].timer != -1) {
				ele = sc->data[SC_SEVENWIND].val3;
			}
			break;
		case SH_HYUN_ROKS_BREEZE:	/* ディアーブリーズ */
			if((sd && pc_checkskill(sd,SH_COMMUNE_WITH_HYUN_ROK)) || (sc && sc->data[SC_TEMPORARY_COMMUNION].timer != -1)) {
				MATK_FIX( (1600 + skill_lv * 200 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SH_MYSTICAL_CREATURE_MASTERY) * 40: 0)) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (800 + skill_lv * 100 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SH_MYSTICAL_CREATURE_MASTERY) * 20: 0)) * status_get_lv(bl) / 100, 100 );
			}
			if(sc) {
				if(sc->data[SC_COLORS_OF_HYUN_ROK_1].timer != -1)
					ele = ELE_WATER;
				else if(sc->data[SC_COLORS_OF_HYUN_ROK_2].timer != -1)
					ele = ELE_WIND;
				else if(sc->data[SC_COLORS_OF_HYUN_ROK_3].timer != -1)
					ele = ELE_EARTH;
				else if(sc->data[SC_COLORS_OF_HYUN_ROK_4].timer != -1)
					ele = ELE_FIRE;
				else if(sc->data[SC_COLORS_OF_HYUN_ROK_5].timer != -1)
					ele = ELE_DARK;
				else if(sc->data[SC_COLORS_OF_HYUN_ROK_6].timer != -1)
					ele = ELE_HOLY;
			}
			break;
		case SH_HYUN_ROK_CANNON:	/* ディアーキャノン */
			if((sd && pc_checkskill(sd,SH_COMMUNE_WITH_HYUN_ROK)) || (sc && sc->data[SC_TEMPORARY_COMMUNION].timer != -1)) {
				MATK_FIX( (5200 + skill_lv * 800 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SH_MYSTICAL_CREATURE_MASTERY) * 250: 0)) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (2400 + skill_lv * 300 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SH_MYSTICAL_CREATURE_MASTERY) * 125: 0)) * status_get_lv(bl) / 100, 100 );
			}
			if(sc) {
				if(sc->data[SC_COLORS_OF_HYUN_ROK_1].timer != -1)
					ele = ELE_WATER;
				else if(sc->data[SC_COLORS_OF_HYUN_ROK_2].timer != -1)
					ele = ELE_WIND;
				else if(sc->data[SC_COLORS_OF_HYUN_ROK_3].timer != -1)
					ele = ELE_EARTH;
				else if(sc->data[SC_COLORS_OF_HYUN_ROK_4].timer != -1)
					ele = ELE_FIRE;
				else if(sc->data[SC_COLORS_OF_HYUN_ROK_5].timer != -1)
					ele = ELE_DARK;
				else if(sc->data[SC_COLORS_OF_HYUN_ROK_6].timer != -1)
					ele = ELE_HOLY;
			}
			break;
		case HN_METEOR_STORM_BUSTER:	/* メテオストームバスター */
			{
				int ss_lv = (sd)? pc_checkskill(sd,HN_SELFSTUDY_SOCERY): 0;
				int rate;
				if(flag) {		// 爆発ダメージ
					rate = (1175 + skill_lv * 25 + ss_lv * 5 * skill_lv + status_get_spl(bl) * 3) * status_get_lv(bl) / 100;
					// 爆発は独学のダメージ補正は掛からない
					mgd.div_ = 1;
				} else {		// 初撃ダメージ
					static const int ss_rate[11] = { 100, 101, 103, 105, 107, 109, 111, 113, 115, 120, 125 };
					rate = (1750 + skill_lv * 50 + ss_lv * 5 * skill_lv + status_get_spl(bl) * 3) * status_get_lv(bl) / 100;

					// 独学のダメージ倍増計算
					if(ss_lv > 10) ss_lv = 10;
					rate = rate * ss_rate[ss_lv] / 100;
				}
				// ルールブレイク
				if(sc && sc->data[SC_RULEBREAK].timer != -1)
					rate = rate * 300 / 100;

				MATK_FIX( rate, 100 );
			}
			break;
		case HN_JUPITEL_THUNDER_STORM:	/* ユピテルサンダーストーム */
			{
				static const int ss_rate[11] = { 100, 101, 103, 105, 107, 109, 111, 113, 115, 120, 125 };
				int ss_lv = (sd)? pc_checkskill(sd,HN_SELFSTUDY_SOCERY): 0;
				int rate = (2700 + skill_lv * 150 + ss_lv * 3 * skill_lv + status_get_spl(bl) * 3) * status_get_lv(bl) / 100;

				// 独学のダメージ倍増計算
				if(ss_lv > 10) ss_lv = 10;
				rate = rate * ss_rate[ss_lv] / 100;

				// ルールブレイク
				if(sc && sc->data[SC_RULEBREAK].timer != -1)
					rate = rate * 300 / 100;

				MATK_FIX( rate, 100 );
			}
			break;
		case HN_JACK_FROST_NOVA:	/* ジャックフロストノヴァ */
			{
				int ss_lv = (sd)? pc_checkskill(sd,HN_SELFSTUDY_SOCERY): 0;
				int rate;

				if(flag) {		// 設置ダメージ
					static const int ss_rate[11] = { 100, 101, 103, 105, 107, 109, 111, 113, 115, 120, 125 };
					rate = (650 + skill_lv * 25 + ss_lv * 3 * skill_lv + status_get_spl(bl) * 4) * status_get_lv(bl) / 100;

					// 独学のダメージ倍増計算
					if(ss_lv > 10) ss_lv = 10;
					rate = rate * ss_rate[ss_lv] / 100;
				} else {		// 初撃ダメージ
					rate = (100 + skill_lv * 20 + ss_lv * 3 * skill_lv + status_get_spl(bl) * 2) * status_get_lv(bl) / 100;
					// 初撃は独学のダメージ補正は掛からない
					mgd.div_ = 1;
				}
				// ルールブレイク
				if(sc && sc->data[SC_RULEBREAK].timer != -1)
					rate = rate * 300 / 100;

				MATK_FIX( rate, 100 );
			}
			break;
		case HN_HELLS_DRIVE:	/* ヘルズドライブ */
			{
				static const int ss_rate[11] = { 100, 101, 103, 105, 107, 109, 111, 113, 115, 120, 125 };
				int ss_lv = (sd)? pc_checkskill(sd,HN_SELFSTUDY_SOCERY): 0;
				int rate = (2600 + skill_lv * 150 + ss_lv * 4 * skill_lv + status_get_spl(bl) * 3) * status_get_lv(bl) / 100;

				// 独学のダメージ倍増計算
				if(ss_lv > 10) ss_lv = 10;
				rate = rate * ss_rate[ss_lv] / 100;

				// ルールブレイク
				if(sc && sc->data[SC_RULEBREAK].timer != -1)
					rate = rate * 300 / 100;

				MATK_FIX( rate, 100 );
			}
			break;
		case HN_GROUND_GRAVITATION:		/* グラウンドグラビテーション */
			{
				int ss_lv = (sd)? pc_checkskill(sd,HN_SELFSTUDY_SOCERY): 0;
				int rate;

				if(flag&2) {	// 設置ダメージ
					static const int ss_rate[11] = { 100, 101, 103, 105, 107, 109, 111, 113, 115, 120, 125 };
					rate = (300 + skill_lv * 10 + ss_lv * 2 * skill_lv + status_get_spl(bl) * 2) * status_get_lv(bl) / 100;

					// 独学のダメージ倍増計算
					if(ss_lv > 10) ss_lv = 10;
					rate = rate * ss_rate[ss_lv] / 100;

					mgd.div_ = 1;
				} else {		// 初撃ダメージ
					rate = (600 + skill_lv * 50 + ss_lv * 4 * skill_lv + status_get_spl(bl) * 5) * status_get_lv(bl) / 100;
					// 初撃は独学のダメージ補正は掛からない
				}
				// ルールブレイク
				if(sc && sc->data[SC_RULEBREAK].timer != -1)
					rate = rate * 300 / 100;

				MATK_FIX( rate, 100 );
			}
			break;
		case HN_NAPALM_VULCAN_STRIKE:	/* ナパームバルカンストライク */
			{
				static const int ss_rate[11] = { 100, 101, 103, 105, 107, 109, 111, 113, 115, 120, 125 };
				int ss_lv = (sd)? pc_checkskill(sd,HN_SELFSTUDY_SOCERY): 0;
				int rate = (2600 + skill_lv * 150 + ss_lv * 4 * skill_lv + status_get_spl(bl) * 3) * status_get_lv(bl) / 100;

				// 独学のダメージ倍増計算
				if(ss_lv > 10) ss_lv = 10;
				rate = rate * ss_rate[ss_lv] / 100;

				// ルールブレイク
				if(sc && sc->data[SC_RULEBREAK].timer != -1)
					rate = rate * 300 / 100;

				MATK_FIX( rate, 100 );
			}
			break;
		case SS_TOKEDASU:		/* 影溶き */
			MATK_FIX( (3200 + skill_lv * 500 + status_get_con(bl) * 5) * status_get_lv(bl) / 100, 100 );
			mgd.blewcount = 0;
			break;
		case SS_SEKIENHOU:		/* 赤炎砲 */
			MATK_FIX( (4000 + skill_lv * 300 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SS_ANTENPOU) * 70 * skill_lv: 0)) * status_get_lv(bl) / 100, 100 );
			break;
		case SS_REIKETSUHOU:	/* 冷血砲 */
			MATK_FIX( (4000 + skill_lv * 300 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SS_ANTENPOU) * 70 * skill_lv: 0)) * status_get_lv(bl) / 100, 100 );
			break;
		case SS_RAIDENPOU:		/* 雷電砲 */
			MATK_FIX( (4000 + skill_lv * 300 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SS_ANTENPOU) * 70 * skill_lv: 0)) * status_get_lv(bl) / 100, 100 );
			break;
		case SS_KINRYUUHOU:		/* 金龍砲 */
			MATK_FIX( (4000 + skill_lv * 300 + status_get_spl(bl) * 5 + ((sd)? pc_checkskill(sd,SS_ANTENPOU) * 70 * skill_lv: 0)) * status_get_lv(bl) / 100, 100 );
			break;
		case SS_ANTENPOU:		/* 暗転砲 */
			MATK_FIX( (5750 + skill_lv * 350 + status_get_spl(bl) * 5) * status_get_lv(bl) / 100, 100 );
			if(flag&2) {	// 分身攻撃
				mgd.damage = mgd.damage * 30 / 100;
			}
			break;
		case SS_ANKOKURYUUAKUMU:	/* 幻術 -暗黒龍- */
			if(flag&2) {	// 追加ダメージ
				ele = ELE_FIRE;
				MATK_FIX( (17000 + status_get_spl(bl) * 10) * status_get_lv(bl) / 100, 100 );
			} else {
				MATK_FIX( (17000 + status_get_spl(bl) * 10) * status_get_lv(bl) / 100, 100 );
			}
			break;
		case EL_FIRE_MANTLE:	/* ファイアーマントル */
			if((t_ele == ELE_FIRE || battle_check_undead(t_race,t_ele)) && target->type != BL_PC)
				mgd.blewcount = 0;
			else
				mgd.blewcount |= SAB_REVERSEBLOW;
			MATK_FIX( 1000, 100 );
			break;
		case EL_FIRE_ARROW:		/* ファイアーアロー */
			MATK_FIX( 300, 100 );
			break;
		case EL_FIRE_BOMB:		/* ファイアーボム */
			MATK_FIX( 500, 100 );
			break;
		case EL_FIRE_WAVE:		/* ファイアーウェーブ */
			MATK_FIX( 1200, 100 );
			break;
		case EL_WATER_SCREW:	/* ウォータースクリュー */
			MATK_FIX( 1000, 100 );
			break;
		case EL_HURRICANE_ATK:	/* ハリケーンレイジ(攻撃) */
			MATK_FIX( 500, 100 );
			break;
		case EL_TYPOON_MIS_ATK:	/* タイフーンミサイル(攻撃) */
			MATK_FIX( 1200, 100 );
			break;
		case EL_ROCK_CRUSHER_ATK:	/* ロックランチャー(攻撃) */
			MATK_FIX( 300, 100 );
			break;
		case EM_EL_FLAMEROCK:	/* フレイムロック */
			MATK_FIX( 25000 * status_get_lv(bl) / 100, 100 );
			mdef1 = 0;		// 装備Mdef無視
			break;
		case EM_EL_AGE_OF_ICE:	/* エイジオブアイス */
			MATK_FIX( 35000 * status_get_lv(bl) / 100, 100 );
			mdef1 = 0;		// 装備Mdef無視
			break;
		case EM_EL_STORM_WIND:	/* ストームウインド */
			MATK_FIX( 25000 * status_get_lv(bl) / 100, 100 );
			mdef1 = 0;		// 装備Mdef無視
			break;
		case EM_EL_AVALANCHE:	/* アバランチェ */
			MATK_FIX( 5000 * status_get_lv(bl) / 100, 100 );
			mdef1 = 0;		// 装備Mdef無視
			break;
		case EM_EL_DEADLY_POISON:	/* デッドリーポイズン */
			MATK_FIX( 7000 * status_get_lv(bl) / 100, 100 );
			mdef1 = 0;		// 装備Mdef無視
			break;
	}

	/* ４．一般魔法ダメージ計算 */
	if(normalmagic_flag) {
		int rate = 100;

		// S.Matkによる魔法攻撃ダメージ増加
		MATK_FIX( 100 + status_get_smatk(bl), 100 );

		if(sd) {
			rate = rate - sd->ignore_mdef_ele[t_ele] - sd->ignore_mdef_race[t_race] - sd->ignore_mdef_enemy[t_enemy];
			if(t_mode & MD_BOSS)
				rate -= sd->ignore_mdef_race[RCT_BOSS];
			else
				rate -= sd->ignore_mdef_race[RCT_NONBOSS];
		}
		if(rate > 0) {
			mdef1 = mdef1 * rate / 100;
			mdef2 = mdef2 * rate / 100;

#ifdef PRE_RENEWAL
			// 恋人たちの為のシンフォニー
			if(t_sc && t_sc->data[SC_SYMPHONY_LOVE].timer != -1) {
				// 実際には除算MDEF増加だが、暫定で減算MDEF
				mdef2 += mdef2 * t_sc->data[SC_SYMPHONY_LOVE].val4 / 100;
			}
#else
			if(t_sc) {
				short reduce = 100;
				// アスムプティオ
				if(t_sc->data[SC_ASSUMPTIO].timer != -1) {
					if(map[target->m].flag.pvp || map[target->m].flag.gvg)
						reduce += 35;	// シーズは35%
					else
						reduce += 100;	// それ以外は100%
				}
				// 恋人たちの為のシンフォニー
				if(t_sc->data[SC_SYMPHONY_LOVE].timer != -1) {
					reduce += t_sc->data[SC_SYMPHONY_LOVE].val4;
				}
				// 除算MDEF貫通の特殊計算準備
				if(skill_num == MG_NAPALMBEAT || skill_num == WZ_FIREPILLAR || skill_num == HW_NAPALMVULCAN)
					mdef2 = (mdef1 + mdef2) * reduce / 100;
				else
					mdef1 = mdef1 * reduce / 100;
			}
#endif

			// Mresによる魔法ダメージ減少
			if(mres > 0) {
				int mres_rate = 100;
				if(sc) {
					if(sc->data[SC_A_VITA].timer != -1) {		// アルグトゥスヴィタ
						mres_rate -= sc->data[SC_A_VITA].val2;
					}
				}

				if(mres_rate < 50) {
					mres_rate = 50;
				}
				if(mres_rate != 100) {
					mres = mres * mres_rate / 100;
				}
				mgd.damage = mgd.damage * (2000 + mres) / (2000 + mres * 5);
			}

			if(battle_config.magic_defense_type) {
				mgd.damage = mgd.damage - (mdef1 * battle_config.magic_defense_type) - mdef2;
			} else {
#ifdef PRE_RENEWAL
				mgd.damage = (mgd.damage*(100-mdef1))/100 - mdef2;
#else
				if(skill_num == RK_ENCHANTBLADE) {
					// エンチャントブレイドは除算MDEFを減算処理させる
					mgd.damage = mgd.damage - mdef1;
				} else if(skill_num != MG_NAPALMBEAT && skill_num != WZ_FIREPILLAR && skill_num != HW_NAPALMVULCAN){
					// ナパームビート,ナパームバルカン,ファイアーピラーは除算MDEF貫通
					mgd.damage = (mgd.damage*(1000+mdef1))/(1000+mdef1*10);
				}
#endif
			}
		}
		// ペインキラー
		if(t_sc && t_sc->data[SC_PAIN_KILLER].timer != -1)
			mgd.damage -= t_sc->data[SC_PAIN_KILLER].val4;
		if(mgd.damage < 1)	// プレイヤーの魔法スキルは1ダメージ保証無し
			mgd.damage = (!battle_config.skill_min_damage && bl->type == BL_PC)? 0: 1;
	}

#ifdef PRE_RENEWAL
	/* ５．カードによるダメージ追加処理 */
	if(sd && mgd.damage > 0 && skill_num != NPC_EARTHQUAKE && skill_num != NPC_CANE_OF_EVIL_EYE) {
		cardfix = 100;
		cardfix = cardfix*(100+sd->magic_addrace[t_race])/100;
		cardfix = cardfix*(100+sd->magic_addele[t_ele])/100;
		cardfix = cardfix*(100+sd->magic_addenemy[t_enemy])/100;
		cardfix = cardfix*(100+sd->magic_addsize[t_size])/100;
		if(t_mode & MD_BOSS)
			cardfix = cardfix*(100+sd->magic_addrace[RCT_BOSS])/100;
		else
			cardfix = cardfix*(100+sd->magic_addrace[RCT_NONBOSS])/100;
		cardfix = cardfix*(100+sd->magic_addgroup[t_group])/100;
		t_class = status_get_class(target);
		for(i=0; i<sd->add_magic_damage_class_count; i++) {
			if(sd->add_magic_damage_classid[i] == t_class) {
				cardfix = cardfix*(100+sd->add_magic_damage_classrate[i])/100;
				break;
			}
		}
		if(skill_num > 0 && mgd.damage > 0) {
			int rate = 100;

			// カード効果による特定スキルのダメージ増幅（魔法スキル）
			if(sd->skill_dmgup.count > 0) {
				for(i=0; i<sd->skill_dmgup.count; i++) {
					if(skill_num == sd->skill_dmgup.id[i]) {
						rate += sd->skill_dmgup.rate[i];
						break;
					}
				}
			}
			// カード効果による特定属性スキルのダメージ増幅（魔法スキル）
			rate += sd->skill_elemagic_dmgup[ele];
			cardfix = cardfix*rate/100;
		}
		mgd.damage = mgd.damage*cardfix/100;
	}
#endif

	/* ６．カードによるダメージ減衰処理 */
	if(tsd && mgd.damage > 0 && skill_num != NPC_EARTHQUAKE && skill_num != NPC_CANE_OF_EVIL_EYE) {
		int s_class = status_get_class(bl);
		cardfix = 100;
		cardfix = cardfix*(100-tsd->subele[ele])/100;				// 属性によるダメージ耐性
		cardfix = cardfix*(100-tsd->def_eleenemy[status_get_elem_type(bl)])/100;				// 敵属性によるダメージ耐性
		cardfix = cardfix*(100-tsd->subenemy[status_get_enemy_type(bl)])/100;	// 敵タイプによるダメージ耐性
		cardfix = cardfix*(100-tsd->magic_subsize[status_get_size(bl)])/100;		// サイズによるダメージ耐性
		cardfix = cardfix*(100-tsd->magic_subrace[race])/100;
		cardfix = cardfix*(100-tsd->long_attack_def_rate)/100;	// 遠距離耐性によるダメージ減少
		if(status_get_mode(bl) & MD_BOSS)
			cardfix = cardfix*(100-tsd->magic_subrace[RCT_BOSS])/100;
		else
			cardfix = cardfix*(100-tsd->magic_subrace[RCT_NONBOSS])/100;
		for(i=0; i<tsd->add_mdef_class_count; i++) {
			if(tsd->add_mdef_classid[i] == s_class) {
				cardfix = cardfix*(100-tsd->add_mdef_classrate[i])/100;
				break;
			}
		}
		cardfix = cardfix*(100-tsd->magic_def_rate)/100;
		mgd.damage = mgd.damage*cardfix/100;
	}

#ifndef PRE_RENEWAL
	if(normalmagic_flag && mgd.damage > 0) {
		mgd.damage -= mdef2;
		if(mgd.damage < 1)	// プレイヤーの魔法スキルは1ダメージ保証無し
			mgd.damage = (!battle_config.skill_min_damage && bl->type == BL_PC)? 0: 1;
	}
#endif

	if(mgd.damage < 0)
		mgd.damage = 0;

#ifndef PRE_RENEWAL
	if(t_sc && t_sc->data[SC_SPIDERWEB].timer != -1 && ele == ELE_FIRE && mgd.damage > 0 && normalmagic_flag) {	// スパイダーウェブ
		mgd.damage <<= 1;
		status_change_end(bl, SC_SPIDERWEB, -1);
	}
#endif

	/* ７．属性の適用 */
	mgd.damage = battle_attr_fix(mgd.damage, ele, status_get_element(target));

	/* ８．対象にステータス異常がある場合のダメージ減算処理 */
	if( t_sc && mgd.damage > 0 && normalmagic_flag ) {
		cardfix = 100;

#ifndef PRE_RENEWAL
		// ボルケーノ
		if(t_sc->data[SC_VOLCANO].timer != -1 && ele == ELE_FIRE)
			cardfix += t_sc->data[SC_VOLCANO].val4;
		// バイオレントゲイル
		if(t_sc->data[SC_VIOLENTGALE].timer != -1 && ele == ELE_WIND)
			cardfix += t_sc->data[SC_VIOLENTGALE].val4;
		// デリュージ
		if(t_sc->data[SC_DELUGE].timer != -1 && ele == ELE_FIRE)
			cardfix += t_sc->data[SC_DELUGE].val4;
#endif
		if(t_sc->data[SC_VENOMIMPRESS].timer != -1 && ele == ELE_POISON)		// ベナムインプレス
			cardfix += t_sc->data[SC_VENOMIMPRESS].val2;
		if(t_sc->data[SC_ORATIO].timer != -1 && ele == ELE_HOLY)		// オラティオ
			cardfix += t_sc->data[SC_ORATIO].val1;
		if(t_sc->data[SC_SOULCURSE].timer != -1 && ele == ELE_DARK)		// 死霊憑依
			cardfix += t_sc->data[SC_SOULCURSE].val2;
		if(t_sc->data[SC_HANDICAPSTATE_LIGHTNINGSTRIKE].timer != -1 && ele == ELE_EARTH)		// 激流
			cardfix += t_sc->data[SC_HANDICAPSTATE_LIGHTNINGSTRIKE].val2;
		if(t_sc->data[SC_HANDICAPSTATE_DEADLYPOISON].timer != -1 && ele == ELE_POISON)			// 強毒
			cardfix += t_sc->data[SC_HANDICAPSTATE_DEADLYPOISON].val2;
		if(t_sc->data[SC_CLIMAX_EARTH].timer != -1 && ele == ELE_EARTH)		// クライマックスクエイク
			cardfix += t_sc->data[SC_CLIMAX_EARTH].val2;
		if(t_sc->data[SC_CLIMAX_BLOOM].timer != -1 && ele == ELE_FIRE)		// クライマックスブルーム
			cardfix += t_sc->data[SC_CLIMAX_BLOOM].val2;
		if(t_sc->data[SC_MISTYFROST].timer != -1 && ele == ELE_WATER)		// ジャックフロストノヴァ
			cardfix += t_sc->data[SC_MISTYFROST].val2;

		if(t_sc->data[SC_DEEP_SLEEP].timer != -1)		// 安息の子守唄（加算でいいのかな？）
			cardfix += 50;
		if(cardfix != 100) {
			mgd.damage = mgd.damage*cardfix/100;	// ステータス異常補正によるダメージ減少
		}
	}

#ifndef PRE_RENEWAL
	if(sd && mgd.damage > 0 && normalmagic_flag) {
		cardfix = 100;
		// （RE）カード効果による特定スキルのダメージ増幅（魔法スキル）
		if(sd->skill_dmgup.count > 0 && skill_num > 0) {
			for(i=0; i<sd->skill_dmgup.count; i++) {
				if(skill_num == sd->skill_dmgup.id[i]) {
					cardfix += sd->skill_dmgup.rate[i];
					break;
				}
			}
		}
		// （RE）カード効果による特定属性スキルのダメージ増幅（魔法スキル）
		cardfix += sd->skill_elemagic_dmgup[ele];
		mgd.damage = mgd.damage*cardfix/100;
	}
#endif
#ifdef PRE_RENEWAL
	/* ９．スキル修正１ */
	if(skill_num == CR_GRANDCROSS || skill_num == NPC_GRANDDARKNESS) {	// グランドクロス
		static struct Damage wd = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		wd = battle_calc_weapon_attack(bl,target,skill_num,skill_lv,flag);
		mgd.damage = (mgd.damage + wd.damage) * (100 + 40*skill_lv)/100;
		if(battle_config.gx_dupele)
			mgd.damage = battle_attr_fix(mgd.damage, ele, status_get_element(target));	// 属性2回かかる
		if(bl == target) {
			if(bl->type & (BL_MOB | BL_HOM | BL_MERC | BL_ELEM))
				mgd.damage = 0;		// MOB,HOM,MERC,ELEMが使う場合は反動無し
			else
				mgd.damage /= 2;	// 反動は半分
		}
	}
#endif

	if(skill_num == NPC_RAYOFGENESIS && mgd.damage < 1)	// Mレイオブジェネシス
		mgd.damage = 1;

	if(skill_num == WZ_WATERBALL)
		mgd.div_ = 1;

	/* 10．対象にステータス異常がある場合 */
	if(tsd && tsd->special_state.no_magic_damage)
		mgd.damage = 0;	// 黄金蟲カード（魔法ダメージ０)

	if(t_sc && t_sc->data[SC_HERMODE].timer != -1 && t_sc->data[SC_HERMODE].val1 == 1)	// ヘルモードなら魔法ダメージなし
		mgd.damage = 0;

	if(skill_num == WL_SOULEXPANSION && t_sc && t_sc->data[SC_WHITEIMPRISON].timer != -1) {	// ソウルエクスパンション
		status_change_end(target,SC_WHITEIMPRISON,-1);
		mgd.damage += mgd.damage;
	}

	/* 11．固定ダメージ */
	if(skill_num == HW_GRAVITATION) {	// グラビテーションフィールド
#ifdef PRE_RENEWAL
		mgd.damage = 200+200*skill_lv;
#else
		mgd.damage = 500+100*skill_lv;
#endif
	}

	/* 12．ヒット回数によるダメージ倍加 */
	if(mgd.damage > 0) {
		switch(skill_num) {
			case MG_SOULSTRIKE:
			case MG_COLDBOLT:
			case MG_FIREBOLT:
			case MG_LIGHTNINGBOLT:
			case MG_THUNDERSTORM:
			case PR_MAGNUS:
			case WZ_METEOR:
			case WZ_JUPITEL:
			case WZ_EARTHSPIKE:
			case WZ_HEAVENDRIVE:
			case NPC_DARKSTRIKE:
			case NPC_DARKTHUNDER:
			case HW_NAPALMVULCAN:
			case SL_SMA:
			case NJ_KOUENKA:
			case NJ_HYOUSENSOU:
			case NJ_HUUJIN:
			case NPC_RAYOFGENESIS:	/* Mレイオブジェネシス */
			case KO_KAIHOU:	/* 術式 -解放- */
			case AG_SOUL_VC_STRIKE:		/* ソウルバルカンストライク */
			case AG_CRYSTAL_IMPACT:		/* クリスタルインパクト */
			case AG_CRIMSON_ARROW_ATK:		/* クリムゾンアロー(攻撃) */
			case ABC_ABYSS_SQUARE:		/* アビススクエア */
			case EM_EL_AVALANCHE:		/* アバランチェ */
			case EM_EL_DEADLY_POISON:	/* デッドリーポイズン */
			case SOA_TALISMAN_OF_FOUR_BEARING_GOD:	/* 四方神符 */
			case SOA_CIRCLE_OF_DIRECTIONS_AND_ELEMENTALS:	/* 四方五行陣 */
				if(t_mode&MD_PLANT) // 草・きのこ等
					mgd.damage = mgd.div_;
				else
					mgd.damage *= mgd.div_;
				break;
			default:
				if(t_mode&MD_PLANT) {	 // 草・きのこ等
					if(battle_config.skill_min_damage)
						mgd.damage = mgd.div_;
					else
						mgd.damage = 1;
				}
				break;
		}
	}

	/* 13．ダメージ最終計算 */
	if(skill_num != ASC_BREAKER && skill_num != CR_ACIDDEMONSTRATION && skill_num != GN_FIRE_EXPANSION_ACID && skill_num != NPC_EARTHQUAKE)
		mgd.damage = battle_calc_damage(bl,target,mgd.damage,mgd.div_,skill_num,skill_lv,mgd.flag);

	/* 14．魔法でもオートスペル発動(item_bonus) */
	if(sd && target != &sd->bl && mgd.damage > 0)
	{
		unsigned int asflag = EAS_ATTACK;
		unsigned int tick = gettick();

		if(battle_config.magic_attack_autospell)
			asflag += EAS_SHORT|EAS_LONG;
		else
			asflag += EAS_MAGIC;

		bonus_autospell_start(&sd->bl,target,asflag,tick,0);
		bonus_activeitem_start(sd,EAS_MAGIC,tick);
	}

	/* 15．魔法でもHP/SP回復(月光剣など) */
	if(battle_config.magic_attack_drain && bl != target)
		battle_attack_drain(bl,mgd.damage,0,battle_config.magic_attack_drain_enable_type);

	/* 16．計算結果の最終補正 */
	mgd.amotion = status_get_amotion(bl);
	mgd.dmotion = status_get_dmotion(target);
	mgd.damage2 = 0;
	mgd.type    = 0;

	return mgd;
}

/*==========================================
 * その他ダメージ計算
 *------------------------------------------
 */
static struct Damage battle_calc_misc_attack(struct block_list *bl,struct block_list *target,int skill_num,int skill_lv,int flag)
{
	struct Damage mid = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	struct map_session_data *sd = NULL, *tsd = NULL;
	struct mob_data *md = NULL;
	struct status_change    *t_sc = NULL;
	struct skill_unit       *unit = NULL;
	int int_, dex, race, ele;
	int skill;
	int damagefix = 1;

	// return前の処理があるので情報出力部のみ変更
	if( bl == NULL || target == NULL || target->type == BL_PET ) {
		nullpo_info(NLP_MARK);
		return mid;
	}

	// グラウンドドリフトのときはblを設置者に置換する
	if(bl->type == BL_SKILL) {
		unit = (struct skill_unit *)bl;
		if(unit && unit->group)
			bl = map_id2bl(unit->group->src_id);
	}

	sd  = BL_DOWNCAST( BL_PC, bl );
	tsd = BL_DOWNCAST( BL_PC, target );
	md  = BL_DOWNCAST( BL_MOB, bl );

	// アタッカー
	int_ = status_get_int(bl);
	dex  = status_get_dex(bl);
	race = status_get_race(bl);
	ele  = skill_get_pl(skill_num);

	// ターゲット
	t_sc = status_get_sc(target);		// 対象のステータス異常

	if(sd) {
		sd->state.attack_type = BF_MISC;
		sd->state.arrow_atk = 0;
	}

	// キングスグレイス
	if(skill_num != PA_PRESSURE && t_sc && t_sc->data[SC_KINGS_GRACE].timer != -1)
		return mid;

	/* １．mid構造体の初期設定 */
	mid.div_      = skill_get_num(skill_num,skill_lv);
	mid.blewcount = skill_get_blewcount(skill_num,skill_lv);
	mid.flag      = BF_MISC|BF_SHORT|BF_SKILL;

	/* ２．基本ダメージ計算(スキルごとに処理) */
	switch(skill_num)
	{
#ifdef PRE_RENEWAL
	case HT_LANDMINE:	// ランドマイン
	case MA_LANDMINE:
		mid.damage = skill_lv*(dex+75)*(100+int_)/100;
		break;
	case HT_BLASTMINE:	// ブラストマイン
		mid.damage = skill_lv*(dex/2+50)*(100+int_)/100;
		break;
	case HT_CLAYMORETRAP:	// クレイモアトラップ
		mid.damage = skill_lv*(dex/2+75)*(100+int_)/100;
		break;
#else
	case HT_LANDMINE:	// ランドマイン
	case MA_LANDMINE:
	case HT_BLASTMINE:	// ブラストマイン
	case HT_CLAYMORETRAP:	// クレイモアトラップ
		mid.damage = (int)(dex * (3+status_get_lv(bl)/(float)100) * (1+int_/(float)35) * skill_lv);
		mid.damage = (mid.damage * 90 / 100) + (mid.damage * (atn_rand()%20 + 1) / 100);
		if(sd)
			mid.damage += 40 * pc_checkskill(sd,RA_RESEARCHTRAP);
		break;
#endif

	case HT_BLITZBEAT:	// ブリッツビート
		if(sd == NULL || (skill = pc_checkskill(sd,HT_STEELCROW)) <= 0)
			skill = 0;
		mid.damage = (dex/10 + int_/2 + skill*3 + 40)*2;
		if(flag > 1)
			mid.damage /= flag;
		flag &= ~(BF_SKILLMASK|BF_RANGEMASK|BF_WEAPONMASK);
		mid.flag = flag|(mid.flag&~BF_RANGEMASK)|BF_LONG;
		break;

	case TF_THROWSTONE:	// 石投げ
		mid.damage = 50;
		damagefix = 0;
		flag &= ~(BF_SKILLMASK|BF_RANGEMASK|BF_WEAPONMASK);
		mid.flag = flag|(mid.flag&~BF_RANGEMASK)|BF_LONG;
		break;

	case BA_DISSONANCE:	// 不協和音
		mid.damage = skill_lv * 20;
		if(sd)
			mid.damage += pc_checkskill(sd,BA_MUSICALLESSON)*3;
		break;
	case NPC_SELFDESTRUCTION:	// 自爆
	case NPC_SELFDESTRUCTION2:	// 自爆2
		mid.damage = status_get_hp(bl)-((bl == target)? 1: 0);
		break;

	case NPC_DARKBREATH:
		{
#ifdef PRE_RENEWAL
			int hitrate = status_get_hit(bl) - status_get_flee(target) + 80;
#else
			int hitrate = status_get_hit(bl) - status_get_flee(target);
#endif
			int t_hp = status_get_hp(target);
			hitrate = (hitrate > 95)? 95: (hitrate < 5)? 5: hitrate;
			if(t_sc && (t_sc->data[SC_SLEEP].timer != -1 || t_sc->data[SC_STUN].timer != -1 ||
				t_sc->data[SC_FREEZE].timer != -1 || (t_sc->data[SC_STONE].timer != -1 && t_sc->data[SC_STONE].val2 == 0) ) )
				hitrate = 1000000;
			if(atn_rand()%100 < hitrate)
				mid.damage = t_hp*(skill_lv*6)/100;
		}
		break;
	case NPC_MAXPAIN_ATK:		/* マックスペイン */
		{
			struct status_change *sc = status_get_sc(bl);
			if(sc && sc->data[SC_MAXPAIN].timer != -1)
				mid.damage = sc->data[SC_MAXPAIN].val2;
			else
				mid.damage = 0;
			damagefix = 0;
		}
		break;
	case NPC_MAGMA_ERUPTION_DOTDAMAGE:	/* Mマグマイラプション(追撃) */
		mid.damage = 1000 * skill_lv;
		damagefix = 0;
		flag &= ~(BF_SKILLMASK|BF_RANGEMASK|BF_WEAPONMASK);
		mid.flag = flag|(mid.flag&~BF_RANGEMASK)|BF_SHORT|BF_WEAPON;
		break;
	case PA_PRESSURE:		// プレッシャー
		mid.damage = 500 + 300 * skill_lv;
		damagefix = 0;
		break;
	case NPC_KILLING_AURA:	// キリングオーラ
		mid.damage = 100000;
		damagefix = 0;
		mid.damage = battle_attr_fix(mid.damage, ele, status_get_element(target));
		break;
	case SN_FALCONASSAULT:		// ファルコンアサルト
		if(sd == NULL || (skill = pc_checkskill(sd,HT_STEELCROW)) <= 0)
			skill = 0;
		if (md && mobdb_search(md->class_)->mode_opt[MDOPT_STEELCROW])
			skill = mobdb_search(md->class_)->mode_opt[MDOPT_STEELCROW];
		mid.damage = ((dex/10+int_/2+skill*3+40)*2*(150+skill_lv*70)/100)*5;
		if(sd) {
			if(battle_config.allow_falconassault_elemet) {
				ele = sd->atk_ele;
			} else {
				// 一時的にファルコンアサルトの属性を付与属性に変更する
				int e;
				if((e = status_get_attack_element_nw(&sd->bl)) != ELE_NEUTRAL)	// 属性付与
					ele = e;
				else if(sd->arrow_ele > ELE_NEUTRAL)	// 矢の属性
					ele = sd->arrow_ele;
				else if((e = status_get_attack_element(&sd->bl)) != ELE_NEUTRAL) // 武器属性
					ele = e;
			}
		}
		flag &= ~(BF_WEAPONMASK|BF_RANGEMASK|BF_WEAPONMASK);
		mid.flag = flag|(mid.flag&~BF_RANGEMASK)|BF_LONG;
		break;
	case GS_GROUNDDRIFT:		// グラウンドドリフト
		if(unit && unit->group)
		{
			ele = unit->group->val2;
			mid.damage = status_get_baseatk(bl);
		}
		break;
	case RA_CLUSTERBOMB:		// クラスターボム
		{
			static struct Damage wd = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
			wd = battle_calc_weapon_attack(bl,target,skill_num,skill_lv,flag);
			mid.damage = wd.damage;
			mid.damage += ((dex * skill_lv + int_ * 5) * (150 + status_get_lv(bl)) / 100) * ((sd? pc_checkskill(sd,RA_RESEARCHTRAP): 5) * 20 / 50);
			mid.flag |= wd.flag;
			if(sd)
				mid.damage += 40 * pc_checkskill(sd,RA_RESEARCHTRAP);
		}
		break;
	case RA_FIRINGTRAP:			// ファイアリングトラップ
	case RA_ICEBOUNDTRAP:		// アイスバウンドトラップ
		{
			static struct Damage wd = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
			wd = battle_calc_weapon_attack(bl,target,skill_num,skill_lv,flag);
			mid.damage = wd.damage;
			mid.damage += ((dex * skill_lv + int_ * 5) * (150 + status_get_lv(bl)) / 100) * ((sd? pc_checkskill(sd,RA_RESEARCHTRAP): 5) * 20 / 100);
			mid.flag |= wd.flag;
			if(sd)
				mid.damage += 40 * pc_checkskill(sd,RA_RESEARCHTRAP);
		}
		break;
	case NC_MAGMA_ERUPTION_DOTDAMAGE:	/* マグマイラプション(追撃) */
		mid.damage = 800 + 200 * skill_lv;
		damagefix = 0;
		flag &= ~(BF_SKILLMASK|BF_RANGEMASK|BF_WEAPONMASK);
		mid.flag = flag|(mid.flag&~BF_RANGEMASK)|BF_SHORT|BF_WEAPON;
		break;
	case GN_THORNS_TRAP:		// ソーントラップ
		mid.damage = 100 + 200 * skill_lv + int_;
		damagefix = 0;
		break;
	case GN_BLOOD_SUCKER:		// ブラッドサッカー
		mid.damage = 200 + 100 * skill_lv + int_;
		damagefix = 0;
		break;
	case GN_HELLS_PLANT_ATK:	// ヘルズプラント
		mid.damage = 10 * skill_lv * status_get_lv(target) + 7 * int_ / 2 * (18 + status_get_jlv(target) / 4) * 5 / (10 - ((sd)? pc_checkskill(sd,AM_CANNIBALIZE) : 5));
		mid.damage = battle_attr_fix(mid.damage, ELE_NEUTRAL, status_get_element(target));
		damagefix = 0;
		break;
	case RL_B_FLICKER_ATK:		// バインドトラップ(爆発)
		mid.damage = status_get_dex(bl) * 10 + status_get_hp(target) * 3 * skill_lv / 100;
		break;
	case SP_SOULEXPLOSION:		// 魂の崩壊
		mid.damage = status_get_hp(target)*80/100;
		damagefix = 0;
		break;
	case SU_SV_ROOTTWIST_ATK:	// マタタビの根っこ(攻撃)
		mid.damage = 100;
		damagefix = 0;
		break;
	case HVAN_EXPLOSION:		// バイオエクスプロージョン
		mid.damage = status_get_hp(bl)*(50+50*skill_lv)/100;
		break;
	case NPC_DISSONANCE:
		mid.damage = 30 + skill_lv * 10;
		break;
	default:
		mid.damage = status_get_baseatk(bl);
		break;
	}

	if(damagefix) {
		if(mid.damage < 1 && skill_num != NPC_DARKBREATH)
			mid.damage = 1;

		/* ３．カードによるダメージ減衰処理 */
		if(tsd && mid.damage > 0) {
			int cardfix = 100;
			cardfix = cardfix*(100-tsd->subele[ele])/100;	// 属性によるダメージ耐性
			cardfix = cardfix*(100-tsd->def_eleenemy[status_get_elem_type(bl)])/100;	// 敵属性によるダメージ耐性
			cardfix = cardfix*(100-tsd->subrace[race])/100;	// 種族によるダメージ耐性
			cardfix = cardfix*(100-tsd->subenemy[status_get_enemy_type(bl)])/100;	// 敵タイプによるダメージ耐性
			cardfix = cardfix*(100-tsd->subsize[status_get_size(bl)])/100;	// サイズによるダメージ耐性
			cardfix = cardfix*(100-tsd->misc_def_rate)/100;
			mid.damage = mid.damage*cardfix/100;
		}
		if(mid.damage < 0)
			mid.damage = 0;

		if(skill_num == SN_FALCONASSAULT) {
			if(sd && !battle_config.allow_falconassault_elemet) {
				// ファルコンアサルトの属性を元に戻す
				ele = skill_get_pl(skill_num);
			}
		}

		/* ４．属性の適用 */
		mid.damage = battle_attr_fix(mid.damage, ele, status_get_element(target));

		/* ５．スキル修正 */
		if(skill_num == GS_GROUNDDRIFT) {	// 固定ダメージを加算してさらに無属性として属性計算する
			mid.damage += skill_lv*50;
			mid.damage = battle_attr_fix(mid.damage, ELE_NEUTRAL, status_get_element(target));
		}

	}

	/* ６．ヒット回数によるダメージ倍加 */
	if(mid.div_ > 1)
		mid.damage *= mid.div_;
	if( mid.damage > 0 && (mid.damage < mid.div_ || (status_get_def(target) >= 1000000 && status_get_mdef(target) >= 1000000)) ) {
		mid.damage = mid.div_;
	}

	/* ７．固定ダメージ */
	if(status_get_mode(target)&MD_PLANT && mid.damage > 0)	// 草・きのこ等
		mid.damage = 1;

	/* ８．カードによるダメージ追加処理 */
	if(sd && sd->skill_dmgup.count > 0 && skill_num > 0 && mid.damage > 0) {	// カード効果による特定スキルのダメージ増幅
		int i;
		for(i=0; i<sd->skill_dmgup.count; i++) {
			if(skill_num == sd->skill_dmgup.id[i]) {
				mid.damage += mid.damage * sd->skill_dmgup.rate[i] / 100;
				break;
			}
		}
	}

	/* ９．ダメージ最終計算 */
	mid.damage = battle_calc_damage(bl,target,mid.damage,mid.div_,skill_num,skill_lv,mid.flag);

	/* 10．miscでもオートスペル発動(bonus) */
	if(sd && target != &sd->bl && mid.damage > 0)
	{
		unsigned int asflag = EAS_ATTACK;
		unsigned int tick = gettick();

		if(battle_config.misc_attack_autospell)
			asflag += EAS_SHORT|EAS_LONG;
		else
			asflag += EAS_MISC;

		bonus_autospell_start(&sd->bl,target,asflag,tick,0);
		bonus_activeitem_start(sd,asflag,tick);
	}

	/* 11．miscでもHP/SP回復(月光剣など) */
	if(bl != target)
	{
		// ファイアリングトラップとアイスバウンドトラップは回復する
		if(skill_num == RA_FIRINGTRAP || skill_num == RA_ICEBOUNDTRAP)
			battle_attack_drain(bl,mid.damage,0,3);
		// その他は設定次第
		else if(battle_config.misc_attack_drain)
			battle_attack_drain(bl,mid.damage,0,battle_config.misc_attack_drain_enable_type);
	}

	/* 12．計算結果の最終補正 */
	mid.amotion = status_get_amotion(bl);
	mid.dmotion = status_get_dmotion(target);
	mid.damage2 = 0;
	mid.type    = 0;

	return mid;
}

/*==========================================
 * ダメージ計算一括処理用
 *------------------------------------------
 */
static struct Damage battle_calc_attack(int attack_type,struct block_list *bl,struct block_list *target,int skill_num,int skill_lv,int flag)
{
	static struct Damage wd = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	switch(attack_type) {
		case BF_WEAPON:
			return battle_calc_weapon_attack(bl,target,skill_num,skill_lv,flag);
		case BF_MAGIC:
			return battle_calc_magic_attack(bl,target,skill_num,skill_lv,flag);
		case BF_MISC:
			return battle_calc_misc_attack(bl,target,skill_num,skill_lv,flag);
		default:
			if(battle_config.error_log)
				printf("battle_calc_attack: unknwon attack type ! %d\n",attack_type);
			break;
	}
	return wd;
}

/*==========================================
 * 通常攻撃処理まとめ
 *------------------------------------------
 */
int battle_weapon_attack( struct block_list *src,struct block_list *target,unsigned int tick,int flag)
{
	struct map_session_data *sd = NULL, *tsd = NULL;
	struct status_change *sc, *t_sc;
	atn_bignumber damage,rsdamage = 0,ridamage = 0;
	static struct Damage wd = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	nullpo_retr(0, src);
	nullpo_retr(0, target);

	if(src->prev == NULL || target->prev == NULL)
		return 0;
	if(unit_isdead(src) || unit_isdead(target))
		return 0;

	sd  = BL_DOWNCAST( BL_PC, src );
	tsd = BL_DOWNCAST( BL_PC, target );

	sc   = status_get_sc(src);
	t_sc = status_get_sc(target);

	if(sc && sc->opt1 > OPT1_NORMAL && sc->opt1 != OPT1_BURNNING) {
		unit_stopattack(src);
		return 0;
	}

	// ウォーグライダー中
	if(sd && pc_iswolfmount(sd)) {
		unit_stopattack(src);
		clif_msgstringtable(sd, 0x75d);	// 騎乗生物に騎乗中は、攻撃ができません。
		return 0;
	}

	// 自分の状態異常
	if(sc && (
		sc->data[SC_BLADESTOP].timer != -1 ||		// 白羽取り
		sc->data[SC_FORCEWALKING].timer != -1 ||	// 強制移動
		(sc->data[SC_WINKCHARM].timer != -1 && sc->data[SC_WINKCHARM].val2 == target->id) ||	// 魅惑のウィンク
		sc->data[SC__SHADOWFORM].timer != -1 ||		// シャドウフォーム
		sc->data[SC__MANHOLE].timer != -1 ||		// マンホール
		sc->data[SC_CURSEDCIRCLE_USER].timer != -1 ||	// 呪縛陣(使用者)
		sc->data[SC_CURSEDCIRCLE].timer != -1 ||		// 呪縛陣
		(sc->data[SC_SIREN].timer != -1 && sc->data[SC_SIREN].val2 == target->id) ||		// セイレーンの声
		sc->data[SC_DEEP_SLEEP].timer != -1 ||		// 安らぎの子守唄
		sc->data[SC_DIAMONDDUST].timer != -1 ||		// ダイヤモンドダスト
		sc->data[SC_SUHIDE].timer != -1			// かくれる
	)) {
		unit_stopattack(src);
		return 0;
	}

	// 相手の状態異常
	if(t_sc && (
		t_sc->data[SC_FORCEWALKING].timer != -1 ||	// 強制移動
		t_sc->data[SC__MANHOLE].timer != -1			// マンホール
	)) {
		unit_stopattack(src);
		return 0;
	}

	if(battle_check_target(src,target,BCT_ENEMY) <= 0 && !battle_check_range(src,target,0))
		return 0;	// 攻撃対象外

	// ターゲットがMOB GMハイド中で、コンフィグでハイド中攻撃不可 GMレベルが指定より大きい場合
	if(battle_config.hide_attack == 0 && target->type == BL_MOB && sd && sd->sc.option&OPTION_SPECIALHIDING && pc_isGM(sd) < battle_config.gm_hide_attack_lv)
		return 0;

	if(sd) {
		if(!battle_delarrow(sd,1,0))
			return 0;
	}

	if(flag&0x8000) {
		if(sd && battle_config.pc_attack_direction_change)
			sd->dir = sd->head_dir = path_calc_dir(src, target->x,target->y);
		else if(src->type == BL_MOB && battle_config.monster_attack_direction_change)
			((struct mob_data *)src)->dir = path_calc_dir(src, target->x,target->y);
		else if(src->type == BL_HOM && battle_config.monster_attack_direction_change)	// homun_attack_direction_change
			((struct homun_data *)src)->dir = path_calc_dir(src, target->x,target->y);
		else if(src->type == BL_MERC && battle_config.monster_attack_direction_change)	// merc_attack_direction_change
			((struct merc_data *)src)->dir = path_calc_dir(src, target->x,target->y);
		else if(src->type == BL_ELEM && battle_config.monster_attack_direction_change)	// elem_attack_direction_change
			((struct elem_data *)src)->dir = path_calc_dir(src, target->x,target->y);
		wd = battle_calc_weapon_attack(src,target,KN_AUTOCOUNTER,flag&0xff,0);
	} else {
		wd = battle_calc_weapon_attack(src,target,0,0,0);
	}

	// 三段掌
	if(sd) {
		int skill = pc_checkskill(sd,MO_TRIPLEATTACK);
		if(skill > 0) {
			int triple_rate = 30 - skill;
			if(sc && sc->data[SC_TRIPLEATTACK_RATE_UP].timer != -1) {
				triple_rate += triple_rate * (50 + 50 * sc->data[SC_TRIPLEATTACK_RATE_UP].val1) / 100;
				status_change_end(src,SC_TRIPLEATTACK_RATE_UP,-1);
			}
			if(atn_rand()%100 < triple_rate) {
				battle_skill_attack(BF_WEAPON,src,src,target,MO_TRIPLEATTACK,skill,tick,0);
				return wd.dmg_lv;
			}
		}
	}

	if((damage = wd.damage + wd.damage2) > 0 && src != target && (t_sc && t_sc->data[SC_KYOMU].timer == -1)) {
		if(t_sc && t_sc->data[SC_MAXPAIN].timer != -1) {
			t_sc->data[SC_MAXPAIN].val2 = BIGNUM2INT(damage * (t_sc->data[SC_MAXPAIN].val1 * 10) / 100);
			skill_castend_damage_id(target, target, NPC_MAXPAIN_ATK, t_sc->data[SC_MAXPAIN].val1, tick, 0);
			t_sc->data[SC_MAXPAIN].val2 = 0;
			wd.damage  = 0;
			wd.damage2 = 0;
		}
		else if(t_sc && t_sc->data[SC_REFLECTDAMAGE].timer != -1 && atn_rand()%100 < 30 + t_sc->data[SC_REFLECTDAMAGE].val1 * 10) {	// リフレクトダメージ反射
			atn_bignumber maxdamage, rddamage;
			int ar = skill_get_area(LG_REFLECTDAMAGE,t_sc->data[SC_REFLECTDAMAGE].val1);
			maxdamage = (atn_bignumber)status_get_max_hp(target) * status_get_lv(target) / 100;
			rddamage = damage * t_sc->data[SC_REFLECTDAMAGE].val3 / 100;
			rddamage = (rddamage > maxdamage) ? maxdamage: rddamage;
			// 反射耐性、位置は仮です
			// 攻撃者の反射耐性参照で拡散ダメージを決定する？
			if(sd && sd->sub_return_damage && rddamage > 0)
				rddamage = rddamage * (100 - sd->sub_return_damage) / 100;

			map_foreachinarea(battle_damage_area,target->m,
				target->x-ar,target->y-ar,target->x+ar,target->y+ar,BL_CHAR,
				target,rddamage,LG_REFLECTDAMAGE,t_sc->data[SC_REFLECTDAMAGE].val1,flag|BCT_ENEMY|1,tick);

			if(--t_sc->data[SC_REFLECTDAMAGE].val3 <= 0)
				status_change_end(target,SC_REFLECTDAMAGE,-1);

			if(wd.flag&BF_SHORT && tsd && tsd->short_weapon_damage_return > 0) {
				ridamage += damage * tsd->short_weapon_damage_return / 100;
			}
		} else if(wd.flag&BF_SHORT) {
			if(tsd && tsd->short_weapon_damage_return > 0) {
				ridamage += damage * tsd->short_weapon_damage_return / 100;
			}
			if(t_sc &&
			   t_sc->data[SC_REFLECTSHIELD].timer != -1 &&
			   (sd || t_sc->data[SC_DEVOTION].timer == -1))	// 被ディボーション者ならPCから以外は反応しない
			{
				rsdamage += damage * t_sc->data[SC_REFLECTSHIELD].val2 / 100;
#ifndef PRE_RENEWAL
				if(rsdamage > status_get_max_hp(target))
					rsdamage = status_get_max_hp(target);
#endif
				// 反射耐性
				if(sd && sd->sub_return_damage && rsdamage > 0)
					rsdamage = rsdamage * (100 - sd->sub_return_damage) / 100;
			}
			// デスバウンド反射
			if(t_sc && t_sc->data[SC_DEATHBOUND].timer != -1 && !(status_get_mode(src)&MD_BOSS) && path_check_dir(path_calc_dir(src,target->x,target->y),status_get_dir(target)))
			{
				rsdamage += (damage * t_sc->data[SC_DEATHBOUND].val2 * 7) / 1000;	// 7割反射ダメージ
				wd.damage = (damage * t_sc->data[SC_DEATHBOUND].val2 * 3) / 1000;	// 3割自身が受ける
				clif_skill_damage(target, src, tick, wd.amotion, wd.dmotion, rsdamage, 0, RK_DEATHBOUND, t_sc->data[SC_DEATHBOUND].val1, 1);
				clif_skill_nodamage(target, target, RK_DEATHBOUND, t_sc->data[SC_DEATHBOUND].val1, 1);
				skill_blown(target, src, skill_get_blewcount(RK_DEATHBOUND, t_sc->data[SC_DEATHBOUND].val1) | SAB_NODAMAGE);
				status_change_end(target,SC_DEATHBOUND,-1);
			}
			// シールドスペル(DEF)
			if(t_sc && t_sc->data[SC_SHIELDSPELL_DEF].timer != -1 && t_sc->data[SC_SHIELDSPELL_DEF].val2 == 1)
			{
				rsdamage += damage * t_sc->data[SC_SHIELDSPELL_DEF].val3 / 100;
			}
			// インスピレーション
			if(sc && sc->data[SC_INSPIRATION].timer != -1)
			{
				rsdamage += damage / 100;
			}
			// 破碎柱反射
			if(t_sc && t_sc->data[SC_CRESCENTELBOW].timer != -1 && !(status_get_mode(src)&MD_BOSS) && atn_rand()%100 < t_sc->data[SC_CRESCENTELBOW].val2)
			{
				static struct Damage tmpwd = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
				int lv = t_sc->data[SC_CRESCENTELBOW].val1;
				int dist = unit_distance(target,src);
				status_change_end(target,SC_CRESCENTELBOW,-1);
				rsdamage += damage + (damage * 20 * lv / 100);
				tmpwd = battle_calc_weapon_attack(target,src,SR_CRESCENTELBOW_AUTOSPELL,lv,flag);
				rsdamage += tmpwd.damage;
				wd.damage = rsdamage / 10;
				// 反射耐性
				if(tsd && tsd->sub_return_damage && wd.damage > 0)
					wd.damage = wd.damage * (100 - tsd->sub_return_damage) / 100;
				skill_blown(target,src,skill_get_blewcount(SR_CRESCENTELBOW_AUTOSPELL,lv)|SAB_NODAMAGE);
				if(unit_distance(target,src) < skill_get_blewcount(SR_CRESCENTELBOW_AUTOSPELL,lv) + dist) {
					skill_addtimerskill(target,tick+200,src->id,0,0,SR_CRESCENTELBOW,lv,tick,(0x0f<<20)|0x0500);
				}
			}
			// ヴィゴール
			if(sc && sc->data[SC_VIGOR].timer != -1 && wd.flag&BF_SHORT) {
				rsdamage += 100 * sc->data[SC_VIGOR].val1;
			}
		} else if(wd.flag&BF_LONG) {
			if(tsd && tsd->long_weapon_damage_return > 0) {
				ridamage += damage * tsd->long_weapon_damage_return / 100;
			}
		}
		// アイテム効果に対する反射耐性
		if(sd && sd->sub_return_damage && ridamage > 0)
			ridamage = ridamage * (100 - sd->sub_return_damage) / 100;
		if(rsdamage > 0)
			clif_damage(src,src,tick,wd.amotion,wd.dmotion,rsdamage,1,4,0,0);
		if(ridamage > 0)
			clif_damage(src,src,tick,wd.amotion,wd.dmotion,ridamage,1,4,0,0);
	}
#ifndef PRE_RENEWAL
	if(t_sc && t_sc->data[SC_STEELBODY].timer != -1 && (wd.damage > 0 || wd.damage2 > 0)) {
		wd.damage  = wd.damage  * 10 / 100;
		wd.damage2 = wd.damage2 * 10 / 100;
	}
#endif

	clif_damage(src, target, tick, wd.amotion, wd.dmotion, wd.damage, wd.div_, wd.type, wd.damage2, 0);

	// 二刀流左手とカタール追撃のミス表示(無理やり〜)
	if(sd && (sd->status.weapon >= WT_DOUBLE_DD || sd->status.weapon == WT_KATAR) && wd.damage2 == 0)
		clif_damage(src, target, tick+10, wd.amotion, wd.dmotion, 0, 1, 0, 0, 0);

	if(sd && sd->splash_range > 0 && (wd.damage > 0 || wd.damage2 > 0) && (!sc || sc->data[SC_SACRIFICE].timer == -1))
		skill_castend_damage_id(src,target,0,-1,tick,0);

	map_freeblock_lock();
	battle_delay_damage(tick+wd.amotion,src,target,(wd.damage+wd.damage2),0,0,wd.flag);

	if(target->prev != NULL && !unit_isdead(target)) {
		if(wd.damage > 0 || wd.damage2 > 0
#ifndef PRE_RENEWAL
			|| wd.dmg_lv == ATK_DEF
#endif
		) {
			skill_additional_effect(src,target,0,0,BF_WEAPON,tick);
			if(sd && tsd) {
				if(sd->break_weapon_rate > 0 && atn_rand()%10000 < sd->break_weapon_rate)
					pc_break_equip(tsd, LOC_RARM);
				if(sd->break_armor_rate > 0 && atn_rand()%10000 < sd->break_armor_rate)
					pc_break_equip(tsd, LOC_BODY);
			}
		}
	}

	/* 状態異常 */
	if(sc) {
		// オートスペル
		if(sc->data[SC_AUTOSPELL].timer != -1 && atn_rand()%100 < sc->data[SC_AUTOSPELL].val4) {
			int spellid = sc->data[SC_AUTOSPELL].val2;
			int spelllv = sc->data[SC_AUTOSPELL].val3;
			int r = atn_rand()%100;
			int sp = 0, fail = 0;

			if(r >= 50)
				spelllv -= 2;
			else if(r >= 15)
				spelllv--;
			if(spelllv < 1)
				spelllv = 1;

			if(sd) {
				if(sc->data[SC_SAGE].timer != -1)	// セージの魂
					spelllv = pc_checkskill(sd,spellid);
				sp = skill_get_sp(spellid,spelllv)*2/3;
				if(sd->status.sp < sp)
					fail = 1;
			}
			if(!fail) {
				if(battle_config.skill_autospell_delay_enable) {
					struct unit_data *ud = unit_bl2ud(src);
					if(ud) {
						int delay = skill_delayfix(src, spellid, spelllv);
						ud->canact_tick = tick + delay;
					}
				}
				if(skill_get_inf(spellid) & INF_GROUND) {
					fail = skill_castend_pos2(src,target->x,target->y,spellid,spelllv,tick,flag);
				} else {
					switch(skill_get_nk(spellid) & 3) {
					case 0:
					case 2:	/* 攻撃系 */
						fail = skill_castend_damage_id(src,target,spellid,spelllv,tick,flag);
						break;
					case 1:	/* 支援系 */
						if( (spellid == AL_HEAL || (spellid == ALL_RESURRECTION && target->type != BL_PC)) &&
						    battle_check_undead(status_get_race(target),status_get_elem_type(target)) )
							fail = skill_castend_damage_id(src,target,spellid,spelllv,tick,flag);
						else
							fail = skill_castend_nodamage_id(src,target,spellid,spelllv,tick,flag);
						break;
					}
				}
				if(sd && !fail)
					pc_heal(sd,0,-sp,0,0);
			}
		}

		// デュプレライト
		if(sc->data[SC_DUPLELIGHT].timer != -1) {
			int skill;
			// ペティティオ
			if(sd && (skill = pc_checkskill(sd,CD_PETITIO)) > 0 && (sd->status.weapon == WT_MACE || sd->status.weapon == WT_BOOK)) {
				if(atn_rand()%100 < skill) {
					skill_castend_damage_id(src,target,CD_PETITIO,skill,tick,flag);
				}
			}
			if(atn_rand()%100 < sc->data[SC_DUPLELIGHT].val2) {
				skill_addtimerskill(src,tick+status_get_adelay(src) / 2,target->id,0,0,AB_DUPLELIGHT_MELEE,sc->data[SC_DUPLELIGHT].val1,tick,flag);
			}
			else if(atn_rand()%100 < sc->data[SC_DUPLELIGHT].val3) {
				skill_addtimerskill(src,tick+status_get_adelay(src) / 2,target->id,0,0,AB_DUPLELIGHT_MAGIC,sc->data[SC_DUPLELIGHT].val1,tick,flag);
			}
		}

		// シャドウオートスペル
		if(sc->data[SC__AUTOSHADOWSPELL].timer != -1 && (wd.flag&BF_SHORT) && (wd.damage > 0 || wd.damage2 > 0) && atn_rand()%100 < sc->data[SC__AUTOSHADOWSPELL].val4) {
			int spellid = sc->data[SC__AUTOSHADOWSPELL].val2;
			int spelllv = sc->data[SC__AUTOSHADOWSPELL].val3;

			if(skill_get_inf(spellid) & INF_GROUND) {
				skill_castend_pos2(src,target->x,target->y,spellid,spelllv,tick,flag);
			} else {
				switch(skill_get_nk(spellid) & 3) {
				case 0:
				case 2:	/* 攻撃系 */
					skill_castend_damage_id(src,target,spellid,spelllv,tick,flag);
					break;
				case 1:	/* 支援系 */
					if( (spellid == AL_HEAL || (spellid == ALL_RESURRECTION && target->type != BL_PC)) &&
					    battle_check_undead(status_get_race(target),status_get_elem_type(target)) )
						skill_castend_damage_id(src,target,spellid,spelllv,tick,flag);
					else
						skill_castend_nodamage_id(src,target,spellid,spelllv,tick,flag);
					break;
				}
			}
		}

		// デッドリーインフェクト
		if(sc->data[SC__DEADLYINFECT].timer != -1 && (wd.damage > 0 || wd.damage2 > 0)) {
			status_change_copy(src,target);
		}

		// 点穴 -球-
		if(sd && sc->data[SC_GENTLETOUCH_ENERGYGAIN].timer != -1 && atn_rand()%100 < sc->data[SC_GENTLETOUCH_ENERGYGAIN].val2 && wd.flag&BF_SHORT && (wd.damage > 0 || wd.damage2 > 0)) {
			int max = (sd->s_class.job == PC_JOB_MO || sd->s_class.job == PC_JOB_SR)? pc_checkskill(sd,MO_CALLSPIRITS): skill_get_max(MO_CALLSPIRITS);
			if(sc->data[SC_RAISINGDRAGON].timer != -1)
				max += sc->data[SC_RAISINGDRAGON].val1;
			if(sd->spiritball.num < max)
				pc_addspiritball(sd,skill_get_time2(SR_GENTLETOUCH_ENERGYGAIN,sc->data[SC_GENTLETOUCH_ENERGYGAIN].val1),1);
		}

		// トロピック
		if(sc->data[SC_TROPIC].timer != -1 && (wd.flag&BF_SHORT) && (wd.damage > 0 || wd.damage2 > 0) && atn_rand()%10000 < 2500) {
			skill_castend_damage_id(src,target,MG_FIREBOLT,5,tick,flag);
		}

		// クールエアー
		if(sc->data[SC_CHILLY_AIR].timer != -1 && (wd.flag&BF_SHORT) && (wd.damage > 0 || wd.damage2 > 0) && atn_rand()%10000 < 2500) {
			skill_castend_damage_id(src,target,MG_COLDBOLT,5,tick,flag);
		}

		// ワイルドストーム
		if(sc->data[SC_WILD_STORM].timer != -1 && (wd.flag&BF_SHORT) && (wd.damage > 0 || wd.damage2 > 0) && atn_rand()%10000 < 2500) {
			skill_castend_damage_id(src,target,MG_LIGHTNINGBOLT,5,tick,flag);
		}

		// アップヘイバル
		if(sc->data[SC_UPHEAVAL].timer != -1 && (wd.flag&BF_SHORT) && (wd.damage > 0 || wd.damage2 > 0) && atn_rand()%10000 < 2500) {
			skill_castend_damage_id(src,target,WZ_EARTHSPIKE,5,tick,flag);
		}
		// 流星落下
		if (sc->data[SC_FALLINGSTAR].timer != -1 && (wd.flag&BF_WEAPON) && (wd.damage > 0 || wd.damage2 > 0) && atn_rand()%100 < sc->data[SC_FALLINGSTAR].val2) {
			skill_castend_damage_id(src, target, SJ_FALLINGSTAR_ATK, sc->data[SC_FALLINGSTAR].val1, tick, flag);
		}
		// 魂の収穫
		if(sd && sc->data[SC_SOULREAPER].timer != 1 && (wd.damage > 0 || wd.damage2 > 0) && atn_rand()%100 < sc->data[SC_SOULREAPER].val2) {
			pc_addsoulenergy(sd,600000,1);
		}
		// サーヴァントウェポン
		if(sd && sc->data[SC_SERVANTWEAPON].timer != -1 && sd->ball.num > 0 && atn_rand()%100 < sc->data[SC_SERVANTWEAPON].val2) {
			skill_castend_damage_id(src,target,DK_SERVANTWEAPON_ATK,sc->data[SC_SERVANTWEAPON].val1,tick,flag);
			pc_delball(sd,1,0);
		}
		// フロムジアビス
		if(sd && sc->data[SC_ABYSSFORCEWEAPON].timer != -1) {
			int skill;
			if(sd->ball.num > 0 && atn_rand()%100 < sc->data[SC_ABYSSFORCEWEAPON].val2) {
				skill_castend_damage_id(src,target,ABC_FROM_THE_ABYSS_ATK,sc->data[SC_ABYSSFORCEWEAPON].val1,tick,flag);
				pc_delball(sd,1,0);
			}
			if((skill = pc_checkskill(sd,ABC_ABYSS_SQUARE)) > 0 && atn_rand()%100 < sc->data[SC_ABYSSFORCEWEAPON].val1) {
				skill_castend_pos2(src,target->x,target->y,ABC_ABYSS_SQUARE,skill,tick,flag);
			}
		}
		// フレイムテクニック
		if(sc->data[SC_FLAMETECHNIC].timer != -1 && (wd.flag&BF_SHORT) && (wd.damage > 0 || wd.damage2 > 0) && atn_rand()%10000 < 2500) {
			int skill;
			if(sd && ((skill = pc_checkskill(sd,MG_FIREBOLT)) > 0)) {
				skill_castend_damage_id(src,target,MG_FIREBOLT,skill,tick,flag);
			} else {
				skill_castend_damage_id(src,target,MG_FIREBOLT,1,tick,flag);
			}
		}
		// コールドフォース
		if(sc->data[SC_COLD_FORCE].timer != -1 && (wd.flag&BF_SHORT) && (wd.damage > 0 || wd.damage2 > 0) && atn_rand()%10000 < 2500) {
			int skill;
			if(sd && ((skill = pc_checkskill(sd,MG_COLDBOLT)) > 0)) {
				skill_castend_damage_id(src,target,MG_COLDBOLT,skill,tick,flag);
			} else {
				skill_castend_damage_id(src,target,MG_COLDBOLT,1,tick,flag);
			}
		}
		// グレイスブリーズ
		if(sc->data[SC_GRACE_BREEZE].timer != -1 && (wd.flag&BF_SHORT) && (wd.damage > 0 || wd.damage2 > 0) && atn_rand()%10000 < 2500) {
			int skill;
			if(sd && ((skill = pc_checkskill(sd,MG_LIGHTNINGBOLT)) > 0)) {
				skill_castend_damage_id(src,target,MG_LIGHTNINGBOLT,skill,tick,flag);
			} else {
				skill_castend_damage_id(src,target,MG_LIGHTNINGBOLT,1,tick,flag);
			}
		}
		// アースケア
		if(sc->data[SC_EARTH_CARE].timer != -1 && (wd.flag&BF_SHORT) && (wd.damage > 0 || wd.damage2 > 0) && atn_rand()%10000 < 2500) {
			int skill;
			if(sd && ((skill = pc_checkskill(sd,WZ_EARTHSPIKE)) > 0)) {
				skill_castend_damage_id(src,target,WZ_EARTHSPIKE,skill,tick,flag);
			} else {
				skill_castend_damage_id(src,target,WZ_EARTHSPIKE,1,tick,flag);
			}
		}
		// ディープポイズニング
		if(sc->data[SC_DEEP_POISONING].timer != -1 && (wd.flag&BF_SHORT) && (wd.damage > 0 || wd.damage2 > 0) && atn_rand()%10000 < 2500) {
			int skill;
			if(sd && ((skill = pc_checkskill(sd,SO_POISON_BUSTER)) > 0)) {
				skill_castend_damage_id(src,target,SO_POISON_BUSTER,skill,tick,flag);
			} else {
				skill_castend_damage_id(src,target,SO_POISON_BUSTER,1,tick,flag);
			}
		}
		// オートファイアリングランチャー
		if(sd && sc->data[SC_AUTO_FIRING_LAUNCHEREFST].timer != -1 && (wd.flag&BF_WEAPON) && (wd.damage > 0 || wd.damage2 > 0)) {
			int skill;
			if((skill = pc_checkskill(sd,NW_BASIC_GRENADE)) > 0 && atn_rand()%10000 < sc->data[SC_AUTO_FIRING_LAUNCHEREFST].val2) {
				skill_castend_damage_id(src,target,NW_BASIC_GRENADE,skill,tick,flag);
			} else if((skill = pc_checkskill(sd,NW_HASTY_FIRE_IN_THE_HOLE)) > 0 && atn_rand()%10000 < sc->data[SC_AUTO_FIRING_LAUNCHEREFST].val3) {
				skill_castend_pos2(src,target->x,target->y,NW_HASTY_FIRE_IN_THE_HOLE,skill,tick,flag);
			} else if((skill = pc_checkskill(sd,NW_GRENADES_DROPPING)) > 0 && atn_rand()%10000 < sc->data[SC_AUTO_FIRING_LAUNCHEREFST].val4) {
				skill_castend_pos2(src,target->x,target->y,NW_GRENADES_DROPPING,skill,tick,flag);
			}
		}
	}

	// カードによるオートスペル
	if(sd && target != &sd->bl && (wd.damage > 0 || wd.damage2 > 0))
	{
		unsigned int asflag = EAS_ATTACK | EAS_NORMAL;
		unsigned int astick = gettick();

		if(wd.flag&BF_LONG)
			asflag += EAS_LONG;
		else
			asflag += EAS_SHORT;

		bonus_autospell_start(&sd->bl,target,asflag,astick,0);
		bonus_activeitem_start(sd,asflag,astick);
	}

	if(sd && target != &sd->bl && wd.flag&BF_WEAPON && (wd.damage > 0 || wd.damage2 > 0))
	{
		// SP消失
		if(tsd && atn_rand()%100 < sd->sp_vanish.rate)
		{
			int sp = status_get_sp(target) * sd->sp_vanish.per/100;
			if(sp > 0)
				pc_heal(tsd, 0, -sp,0,0);
		}
	}

	if(sd && target != &sd->bl && wd.flag&BF_WEAPON && (wd.damage > 0 || wd.damage2 > 0)) {
		// ％吸収、一定吸収ともに
		battle_attack_drain(src, wd.damage, wd.damage2, 3);
	}

	if(rsdamage > 0) {
		battle_delay_damage(tick+wd.amotion,target,src,rsdamage,0,0,0);

		// スキルの反射ダメージのオートスペル
		if(battle_config.weapon_reflect_autospell && target->type == BL_PC && atn_rand()%2)
			bonus_autospell_start(target,src,EAS_ATTACK,tick,0);

		if(battle_config.weapon_reflect_drain && src != target)
			battle_attack_drain(target,rsdamage,0,battle_config.weapon_reflect_drain_enable_type);

		// スキルの反射ダメージのアクティブアイテム
		if(battle_config.weapon_reflect_autospell && tsd)
			bonus_activeitem_start(tsd,EAS_ATTACK,tick);
	}
	if(ridamage > 0) {
		battle_delay_damage(tick+wd.amotion,target,src,ridamage,0,0,0);

		// アイテムの反射ダメージのオートスペル
		if(battle_config.weapon_reflect_autospell && target->type == BL_PC && atn_rand()%2)
			bonus_autospell_start(target,src,EAS_ATTACK,tick,0);

		if(battle_config.weapon_reflect_drain && src != target)
			battle_attack_drain(target,ridamage,0,battle_config.weapon_reflect_drain_enable_type);

		// アイテムの反射ダメージのアクティブアイテム
		if(battle_config.weapon_reflect_autospell && tsd)
			bonus_activeitem_start(tsd,EAS_ATTACK,tick);
	}

	// 対象にステータス異常がある場合
	if(t_sc && t_sc->count > 0)
	{
		if(t_sc->data[SC_AUTOCOUNTER].timer != -1 && t_sc->data[SC_AUTOCOUNTER].val4 > 0) {
			if(t_sc->data[SC_AUTOCOUNTER].val3 == src->id)
				battle_weapon_attack(target,src,tick,0x8000|t_sc->data[SC_AUTOCOUNTER].val1);
			status_change_end(target,SC_AUTOCOUNTER,-1);
		}
		if(t_sc->data[SC_BLADESTOP_WAIT].timer != -1 &&
		   !(status_get_mode(src)&MD_BOSS) &&
		   (map[target->m].flag.pvp || unit_distance(src,target) <= 2)) {	// PvP以外での有効射程は2セル
			int lv  = t_sc->data[SC_BLADESTOP_WAIT].val1;
			int sec = skill_get_time2(MO_BLADESTOP,lv);
			status_change_end(target,SC_BLADESTOP_WAIT,-1);
			status_change_start(src,SC_BLADESTOP,lv,1,src->id,target->id,sec,0);
			status_change_start(target,SC_BLADESTOP,lv,2,target->id,src->id,sec,0);
		}
		if(t_sc->data[SC_POISONREACT].timer != -1) {
			if( (src->type == BL_MOB && status_get_elem_type(src) == ELE_POISON) || status_get_attack_element(src) == ELE_POISON ) {
				// 毒属性mobまたは毒属性による攻撃ならば反撃
				if( battle_check_range(target,src,status_get_range(target)+1) ) {
					t_sc->data[SC_POISONREACT].val2 = 0;
					battle_skill_attack(BF_WEAPON,target,target,src,AS_POISONREACT,t_sc->data[SC_POISONREACT].val1,tick,0);
				}
			} else {
				// それ以外の通常攻撃に対するインベ反撃（射線チェックなし）
				--t_sc->data[SC_POISONREACT].val2;
				if(atn_rand()&1) {
					if( tsd == NULL || pc_checkskill(tsd,TF_POISON) >= 5 )
						battle_skill_attack(BF_WEAPON,target,target,src,TF_POISON,5,tick,flag);
				}
			}
			if(t_sc->data[SC_POISONREACT].val2 <= 0)
				status_change_end(target,SC_POISONREACT,-1);
		}
		if(t_sc->data[SC_STONEHARDSKIN].timer != -1 && wd.flag&BF_WEAPON) {	// ストーンハードスキン
			t_sc->data[SC_STONEHARDSKIN].val3 -= BIGNUM2INT(wd.damage + wd.damage2);
			if(t_sc->data[SC_STONEHARDSKIN].val3 <= 0)
				status_change_end(target,SC_STONEHARDSKIN,-1);
			if(sd && atn_rand() % 10000 < 500) {
				pc_break_equip(sd, LOC_RARM);
			} else {
				status_change_start(src,SC_STRIPWEAPON,1,0,0,0,10000,0);
			}
		}
		if(t_sc->data[SC__DEADLYINFECT].timer != -1 && wd.flag&BF_SHORT && (wd.damage > 0 || wd.damage2 > 0)) {	// デッドリーインフェクト
			status_change_copy(target,src);
		}
		if(t_sc->data[SC_CIRCLE_OF_FIRE].timer != -1 && wd.flag&BF_SHORT && (wd.damage > 0 || wd.damage2 > 0)) {	// サークルオブファイア
			battle_skill_attack(BF_WEAPON,target,target,src,EL_CIRCLE_OF_FIRE,t_sc->data[SC_CIRCLE_OF_FIRE].val1,tick,(0x0f<<20)|0x0500);
		}
		if(t_sc->data[SC_WINDSIGN].timer != -1 && atn_rand()%100 < t_sc->data[SC_WINDSIGN].val2 && sd && sd->s_class.job == PC_JOB_WH && sd->status.weapon == WT_BOW && (wd.damage > 0 || wd.damage2 > 0)) {	// ウィンドサイン
			pc_heal(sd,0,0,1,0);
		}
	}

	map_freeblock_unlock();
	return wd.dmg_lv;
}

/*=========================================================================
 * スキル攻撃効果処理まとめ
 * flagの説明。16進図
 * 	0XYRTTff
 *  ff = battle_calc_attackで各種計算に利用
 *  TT = パケットのtype部分（0でデフォルト）
 *   R = 予約（skill_area_subで使用されたBCT_*）
 *   Y = パケットのスキルLv（fのときは-1に変換）
 *   X = エフェクトのみでダメージなしフラグ
 *-------------------------------------------------------------------------
 */
int battle_skill_attack(int attack_type,struct block_list* src,struct block_list *dsrc,
	struct block_list *bl,int skillid,int skilllv,unsigned int tick,int flag)
{
	struct Damage dmg;
	struct map_session_data *sd = NULL, *tsd = NULL;
	struct status_change *sc;
	struct status_change *ssc;
	atn_bignumber damage, rdamage = 0;
	int type, lv;

	nullpo_retr(0, src);
	nullpo_retr(0, dsrc);
	nullpo_retr(0, bl);

	sc  = status_get_sc(bl);
	ssc = status_get_sc(src);

	sd  = BL_DOWNCAST( BL_PC, src );
	tsd = BL_DOWNCAST( BL_PC, bl );

	if(dsrc->m != bl->m)	// 対象が同じマップにいなければ何もしない
		return 0;
	if(src->prev == NULL || dsrc->prev == NULL || bl->prev == NULL)
		return 0;
	if(unit_isdead(src) || unit_isdead(dsrc) || unit_isdead(bl))	// すでに死んでいたら何もしない
		return 0;

	if(ssc) {		// 自分の状態異常
		if( ssc->data[SC_FORCEWALKING].timer != -1 ||
			ssc->data[SC_WINKCHARM].timer != -1 ||
			ssc->data[SC__MANHOLE].timer != -1 ||
			ssc->data[SC_SIREN].timer != -1 ||
			ssc->data[SC_DEEP_SLEEP].timer != -1 ||
			ssc->data[SC_DIAMONDDUST].timer != -1 ||
			(ssc->opt1 > OPT1_NORMAL && ssc->opt1 != OPT1_BURNNING))
			return 0;
	}
	if(sc) {
		if(sc->data[SC_HIDING].timer != -1 && !(status_get_mode(src)&MD_BOSS) && skill_get_pl(skillid) != ELE_EARTH && skillid != HW_GRAVITATION)	// ハイディング状態でBOSSでなくスキルの属性が地属性でなくグラビテーションフィールドでないなら何もしない
			return 0;
		if(sc->data[SC_CHASEWALK].timer != -1 && skillid == AL_RUWACH)	// チェイスウォーク状態でルアフ無効
			return 0;
		if(sc->data[SC_TRICKDEAD].timer != -1) 				// 死んだふり中は何もしない
			return 0;
		if(sc->data[SC_FORCEWALKING].timer != -1) 			// 強制移動中は何もしない
			return 0;
		// 凍結状態でストームガスト、フロストノヴァ、氷衝落は無効
		if(sc->data[SC_FREEZE].timer != -1 && (skillid == WZ_STORMGUST || skillid == WZ_FROSTNOVA || skillid == NJ_HYOUSYOURAKU))
			return 0;
		if(sc->data[SC__MANHOLE].timer != -1) 			// マンホール中は何もしない
			return 0;
	}
	if(sd && sd->chatID)	// 発動元がPCでチャット中なら何もしない
		return 0;

	type = (flag >> 8) & 0xff;
	if(skillid == 0)
		type = 5;

	/* 矢の消費 */
	if(sd) {
		int cost = skill_get_arrow_cost(skillid,skilllv);
		if(cost > 0) {
			switch(skillid) {
				case AC_SHOWER:
				case MA_SHOWER:
				case GS_DESPERADO:
				case GS_GROUNDDRIFT:
				case RA_ARROWSTORM:
				case RA_AIMEDBOLT:
				case NC_ARMSCANNON:
				case RL_S_STORM:	/* シャッターストーム */
				case RL_QD_SHOT:	/* クイックドローショット */
				case RL_FIREDANCE:	/* ファイアーダンス */
				case RL_R_TRIP:		/* ラウンドトリップ */
				case KO_HAPPOKUNAI:
				case ABC_CHAIN_REACTION_SHOT:	/* チェーンリアクションショット */
				case ABC_CHAIN_REACTION_SHOT_ATK:	/* チェーンリアクションショット(攻撃) */
				case WH_CALAMITYGALE:		/* カラミティゲイル */
				case NW_THE_VIGILANTE_AT_NIGHT:		/* ビジラントアットナイト */
				case NW_SPIRAL_SHOOTING:	/* スパイラルシューティング */
				case NW_WILD_FIRE:			/* ワイルドファイア */
				case SS_KUNAIWAIKYOKU:		/* 苦無 -歪曲- */
				case SS_KUNAIKAITEN:		/* 苦無 -回転- */
				// 既に消費処理は完了しているので除外
					break;
				case SN_SHARPSHOOTING:
				case GS_SPREADATTACK:
					if( type == 0 && !battle_delarrow(sd, cost, skillid) )
						return 0;
					break;
				default:
					if( !battle_delarrow(sd, cost, skillid) )
						return 0;
					break;
			}
		}
	}

	/* フラグ値チェック */
	lv = (flag >> 20) & 0x0f;
	if(lv == 0)
		lv = skilllv;
	else if(lv == 0x0f)
		lv = -1;

	if(flag & 0x01000000) {	// エフェクトだけ出してダメージなしで終了
		clif_skill_damage(dsrc, bl, tick, status_get_amotion(src), 0, -1, 1, skillid, lv, type);
		return -1;
	}

	/* ダメージ計算 */
#ifdef PRE_RENEWAL
	dmg = battle_calc_attack(attack_type,(skillid == GS_GROUNDDRIFT)? dsrc: src,bl,skillid,skilllv,flag&0xff);
#else
	dmg = battle_calc_attack(attack_type,src,bl,skillid,skilllv,flag&0xff);
#endif

	/* マジックロッド */
	if(attack_type&BF_MAGIC && sc && sc->data[SC_MAGICROD].timer != -1 && src == dsrc) {
		dmg.damage = dmg.damage2 = 0;
		if(tsd) {	// 対象がPCの場合
			int sp = skill_get_sp(skillid,skilllv);		// 使用されたスキルのSPを吸収
			sp = sp * sc->data[SC_MAGICROD].val2 / 100;
			if(skillid == WZ_WATERBALL && skilllv > 1)
				sp = sp/((skilllv|1)*(skilllv|1));	// ウォーターボールはさらに計算？
			if(sp > 0x7fff)
				sp = 0x7fff;
			else if(sp < 1)
				sp = 1;
			if(tsd->status.sp + sp > tsd->status.max_sp) {
				sp = tsd->status.max_sp - tsd->status.sp;
				tsd->status.sp = tsd->status.max_sp;
			} else {
				tsd->status.sp += sp;
			}
			clif_heal(tsd->fd,SP_SP,sp);
			tsd->ud.canact_tick = tick + skill_delayfix(&tsd->bl, SA_MAGICROD, sc->data[SC_MAGICROD].val1);
		}
		clif_skill_nodamage(bl,bl,SA_MAGICROD,sc->data[SC_MAGICROD].val1,1);	// マジックロッドエフェクトを表示
	}

	damage = dmg.damage + dmg.damage2;

	if(damage <= 0 || damage < dmg.div_)	// 吹き飛ばし判定
		dmg.blewcount = 0;

	if(skillid == CR_GRANDCROSS || skillid == NPC_GRANDDARKNESS) {	// グランドクロス
		if(battle_config.gx_disptype)
			dsrc = src;	// 敵ダメージ白文字表示
		if(src == bl)
			type = 4;	// 反動はダメージモーションなし
	}

	// ダメージ計算後スキルごとの付与処理
	switch(skillid) {
	case KN_PIERCE:				// ピアース
	case LK_SPIRALPIERCE:		// スパイラルピアース
	case RK_HUNDREDSPEAR:		// ハンドレッドスピア
	case DK_MADNESS_CRUSHER:	// マッドネスクラッシャー
		if(ssc && ssc->data[SC_CHARGINGPIERCE].timer != -1) {		// チャージングスピア
			if(ssc->data[SC_CHARGINGPIERCE_COUNT].timer == -1) {
				status_change_start(src,SC_CHARGINGPIERCE_COUNT,1,1,1,1,skill_get_time2(DK_CHARGINGPIERCE,1),0);
			} else if(ssc->data[SC_CHARGINGPIERCE_COUNT].val1 < 10) {
				status_change_start(src,SC_CHARGINGPIERCE_COUNT,ssc->data[SC_CHARGINGPIERCE_COUNT].val1+1,1,1,1,skill_get_time2(DK_CHARGINGPIERCE,1),0);
			} else {
				status_change_end(src,SC_CHARGINGPIERCE_COUNT,-1);
			}
		}
		break;
	case DK_SERVANT_W_DEMOL:	// サーヴァントウェポン：デモリッション
		if(sd)
			pc_addball(sd,600000,1);
		break;
	}

	/* コンボ */
	if(sd) {
		int delay = 0;

		switch(skillid) {
		case MO_TRIPLEATTACK:	// 三段掌
			delay = 1000 - 4 * status_get_agi(src) - 2 * status_get_dex(src);
			if(damage < status_get_hp(bl)) {
				// 連打掌、伏虎拳、連柱崩撃取得時は+300ms
				if((pc_checkskill(sd, MO_CHAINCOMBO) > 0) ||
				   (pc_checkskill(sd, CH_TIGERFIST) > 0 && sd->spiritball.num > 0) ||
				   (pc_checkskill(sd, CH_CHAINCRUSH) > 0 && sd->spiritball.num > 1))
				{
					delay += 300 * battle_config.combo_delay_rate /100;
					// コンボ入力時間の最低保障追加
					if(delay < battle_config.combo_delay_lower_limits)
						delay = battle_config.combo_delay_lower_limits;
				}
				status_change_start(src,SC_COMBO,MO_TRIPLEATTACK,skilllv,0,0,delay,0);
			}
			if(delay > 0) {
				sd->ud.attackabletime = sd->ud.canmove_tick = tick + delay;
				clif_combo_delay(src,delay);
			}
			break;
		case MO_CHAINCOMBO:	// 連打掌
			delay = 1000 - 4 * status_get_agi(src) - 2 * status_get_dex(src);
			if(damage < status_get_hp(bl)) {
				// 猛龍拳、伏虎拳、連柱崩撃取得時は+300ms
				if((pc_checkskill(sd, MO_COMBOFINISH) > 0 && sd->spiritball.num > 0) ||
				   (pc_checkskill(sd, CH_TIGERFIST) > 0 && sd->spiritball.num > 0) ||
				   (pc_checkskill(sd, CH_CHAINCRUSH) > 0 && sd->spiritball.num > 1))
				{
					delay += 300 * battle_config.combo_delay_rate /100;
					// コンボ入力時間の最低保障追加
					if(delay < battle_config.combo_delay_lower_limits)
						delay = battle_config.combo_delay_lower_limits;
				}
				status_change_start(src,SC_COMBO,MO_CHAINCOMBO,skilllv,0,0,delay,0);
			}
			if(delay > 0) {
				sd->ud.attackabletime = sd->ud.canmove_tick = tick + delay;
				clif_combo_delay(src,delay);
			}
			break;
		case MO_COMBOFINISH:	// 猛龍拳
			delay = 700 - 4 * status_get_agi(src) - 2 * status_get_dex(src);
			if(damage < status_get_hp(bl)) {
				// 阿修羅覇凰拳取得＆気球4個保持＆爆裂波動状態時は+300ms
				// 伏虎拳、連柱崩撃取得時も+300ms
				if((pc_checkskill(sd, MO_EXTREMITYFIST) > 0 && sd->spiritball.num >= 4 && sd->sc.data[SC_EXPLOSIONSPIRITS].timer != -1) ||
				   (pc_checkskill(sd, CH_TIGERFIST) > 0 && sd->spiritball.num > 0) ||
				   (pc_checkskill(sd, CH_CHAINCRUSH) > 0 && sd->spiritball.num > 1))
				{
					delay += 300 * battle_config.combo_delay_rate /100;
					// コンボ入力時間最低保障追加
					if(delay < battle_config.combo_delay_lower_limits)
						delay = battle_config.combo_delay_lower_limits;
				}
				status_change_start(src,SC_COMBO,MO_COMBOFINISH,skilllv,0,0,delay,0);
			}
			if(delay > 0) {
				sd->ud.attackabletime = sd->ud.canmove_tick = tick + delay;
				clif_combo_delay(src,delay);
			}
			break;
		case CH_TIGERFIST:	// 伏虎拳
			delay = 1000 - 4 * status_get_agi(src) - 2 * status_get_dex(src);
			if(damage < status_get_hp(bl)) {
				// 連柱崩撃取得時は+300ms
				if(pc_checkskill(sd, CH_CHAINCRUSH) > 0) {
					delay += 300 * battle_config.combo_delay_rate /100;
					// コンボ入力時間最低保障追加
					if(delay < battle_config.combo_delay_lower_limits)
						delay = battle_config.combo_delay_lower_limits;
				}
				status_change_start(src,SC_COMBO,CH_TIGERFIST,skilllv,0,0,delay,0);
			}
			if(delay > 0) {
				sd->ud.attackabletime = sd->ud.canmove_tick = tick + delay;
				clif_combo_delay(src,delay);
			}
			break;
		case CH_CHAINCRUSH:	// 連柱崩撃
			delay = 1000 - 4 * status_get_agi(src) - 2 * status_get_dex(src);
			if(damage < status_get_hp(bl)) {
				// 阿修羅習得＆気球1個保持＆爆裂波動時ディレイ
				if(pc_checkskill(sd, MO_EXTREMITYFIST) > 0 && sd->spiritball.num >= 1 && sd->sc.data[SC_EXPLOSIONSPIRITS].timer != -1)
				{
					delay += (600+(skilllv/5)*200) * battle_config.combo_delay_rate /100;
					// コンボ入力時間最低保障追加
					if(delay < battle_config.combo_delay_lower_limits)
						delay = battle_config.combo_delay_lower_limits;
				}
				status_change_start(src,SC_COMBO,CH_CHAINCRUSH,skilllv,0,0,delay,0);
			}
			if(delay > 0) {
				sd->ud.attackabletime = sd->ud.canmove_tick = tick + delay;
				clif_combo_delay(src,delay);
			}
			break;
		case TK_STORMKICK:
		case TK_DOWNKICK:
		case TK_TURNKICK:
		case TK_COUNTER:
			if(ranking_get_pc_rank(sd,RK_TAEKWON) > 0) {	// テコンランカーはコンボ続行
				delay = status_get_adelay(src);
				if(damage < status_get_hp(bl)) {
					delay += 2000 - 4 * status_get_agi(src) - 2 * status_get_dex(src);	// eA方式
					// TKコンボ入力時間最低保障追加
					if(delay < battle_config.tkcombo_delay_lower_limits)
						delay = battle_config.tkcombo_delay_lower_limits;
					status_change_start(src,SC_TKCOMBO,skillid,0,0,TK_MISSION,delay,0);
				}
				if(delay > 0)
					sd->ud.attackabletime = tick + delay;
			} else {
				status_change_end(src,SC_TKCOMBO,-1);
			}
			break;
		case SR_DRAGONCOMBO:	// 双龍脚
			delay = 1000 - 4 * status_get_agi(src) - 2 * status_get_dex(src);
			if(damage < status_get_hp(bl)) {
				 // 天羅地網、大纒崩捶を習得時ディレイ
				if(pc_checkskill(sd, SR_SKYNETBLOW) > 0 || pc_checkskill(sd, SR_FALLENEMPIRE) > 0)
				{
					delay += 300 * battle_config.combo_delay_rate /100;
					// コンボ入力時間の最低保障追加
					if(delay < battle_config.combo_delay_lower_limits)
						delay = battle_config.combo_delay_lower_limits;
				}
				status_change_start(src,SC_COMBO,SR_DRAGONCOMBO,skilllv,0,0,delay,0);
			}
			if(delay > 0) {
				// 固有ディレイ追加
				int nameid = skill_get_skilldb_id(SR_DRAGONCOMBO);
				if(nameid > 0) {
					sd->skillcooldown[nameid] = tick + delay;
					clif_skill_cooldown(sd, SR_DRAGONCOMBO, delay);
				}
				sd->ud.attacktarget = bl->id;
				sd->ud.attackabletime = sd->ud.canmove_tick = tick + delay;
				clif_combo_delay(src,delay);
			}
			break;
		case SR_FALLENEMPIRE:	// 大纏崩捶
			delay = 1000 - 4 * status_get_agi(src) - 2 * status_get_dex(src);
			if(damage < status_get_hp(bl))
			{
				 // 號砲取得＆気球2個保持時＆爆裂波動状態　または　羅刹破凰撃取得＆気球5個保持
				if( (pc_checkskill(sd, SR_TIGERCANNON) > 0 && sd->spiritball.num >= 2 && sd->sc.data[SC_EXPLOSIONSPIRITS].timer != -1) ||
					(pc_checkskill(sd, SR_GATEOFHELL)  > 0 && sd->spiritball.num >= 5))
				{
					delay += 300 * battle_config.combo_delay_rate /100;
					// コンボ入力時間の最低保障追加
					if(delay < battle_config.combo_delay_lower_limits)
						delay = battle_config.combo_delay_lower_limits;
				}
				status_change_start(src,SC_COMBO,SR_FALLENEMPIRE,skilllv,0,0,delay,0);
			}
			if(delay > 0)
			{
				sd->ud.attackabletime = sd->ud.canmove_tick = tick + delay;
				clif_combo_delay(src,delay);
			}
			break;
		case SJ_PROMINENCEKICK:	// 紅焔脚
			delay = 1000 - 4 * status_get_agi(src) - 2 * status_get_dex(src);
			if( pc_checkskill(sd, SJ_SOLARBURST) )
			{
				delay += 300 * battle_config.combo_delay_rate /100;
				// コンボ入力時間の最低保障追加
				if(delay < battle_config.combo_delay_lower_limits)
					delay = battle_config.combo_delay_lower_limits;
			}
			status_change_start(src,SC_COMBO,skillid,skilllv,0,0,delay,0);
			if(delay > 0)
			{
				sd->ud.attackabletime = sd->ud.canmove_tick = tick + delay;
				clif_combo_delay(src,delay);
			}
			break;
		}
	}
	if(src->type == BL_HOM) {
		struct homun_data *hd = (struct homun_data *)src;

		if(hd) {
			switch(skillid) {
			case MH_SONIC_CRAW:			/* ソニッククロー */
			case MH_SILVERVEIN_RUSH:	/* シルバーベインラッシュ */
			case MH_TINDER_BREAKER:		/* ティンダーブレイカー */
				if(hd->spiritball >= 1) {
					int delay = 1000;
					if(damage < status_get_hp(bl))
						status_change_start(src,SC_COMBO,skillid,skilllv,0,0,delay,0);
					clif_combo_delay(src,delay);
				}
				break;
			case MH_MIDNIGHT_FRENZY:	/* ミッドナイトフレンジ */
			case MH_EQC:				/* E.Q.C */
				if(hd->spiritball >= 2) {
					int delay = 1000;
					if(damage < status_get_hp(bl))
						status_change_start(src,SC_COMBO,skillid,skilllv,0,0,delay,0);
					clif_combo_delay(src,delay);
				}
				break;
			}
		}
	}

	/* ダメージ反射 */
	if(sc && sc->data[SC_MAXPAIN].timer != -1 && damage > 0) {
		sc->data[SC_MAXPAIN].val2 = BIGNUM2INT(damage * (sc->data[SC_MAXPAIN].val1 * 10) / 100);
		skill_castend_damage_id(bl, bl, NPC_MAXPAIN_ATK, sc->data[SC_MAXPAIN].val1, tick, 0);
		sc->data[SC_MAXPAIN].val2 = 0;
		damage = 0;
		dmg.blewcount = 0;
	}
	if(attack_type&BF_WEAPON && damage > 0 && src != bl && (sc && sc->data[SC_KYOMU].timer == -1)) {	// 武器スキル＆ダメージあり＆使用者と対象者が違う＆虚無の影ではない
		if(src == dsrc || (dsrc->type == BL_SKILL && (skillid == SG_SUN_WARM || skillid == SG_MOON_WARM || skillid == SG_STAR_WARM || skillid == GS_DESPERADO))) {
			if(dmg.flag&BF_SHORT) {	// 近距離攻撃時
				if(tsd) {	// 対象がPCの時
					if(tsd->short_weapon_damage_return > 0) {	// 近距離攻撃跳ね返し
						rdamage += damage * tsd->short_weapon_damage_return / 100;
						if(rdamage < 1) rdamage = 1;
					}
				}

				// リフレクトシールド時
				if(sc &&
				   sc->data[SC_REFLECTSHIELD].timer != -1 &&
				   (sd || sc->data[SC_DEVOTION].timer == -1) &&	// 被ディボーション者ならPCから以外は反応しない
				   skillid != WS_CARTTERMINATION &&
				   skillid != CR_ACIDDEMONSTRATION &&
				   skillid != GN_FIRE_EXPANSION_ACID)
				{
					rdamage += damage * sc->data[SC_REFLECTSHIELD].val2 / 100;	// 跳ね返し計算
#ifndef PRE_RENEWAL
					if(rdamage > status_get_max_hp(bl))
						rdamage = status_get_max_hp(bl);
#endif
					if(rdamage < 1) rdamage = 1;
				}
				// 反射耐性
				if(sd && sd->sub_return_damage && rdamage > 0) {
					rdamage = rdamage * (100 - sd->sub_return_damage) / 100;
					if(rdamage < 1) rdamage = 1;
				}
				// デスバウンド時
				if(sc && sc->data[SC_DEATHBOUND].timer != -1 && !(status_get_mode(src)&MD_BOSS) && path_check_dir(path_calc_dir(src,bl->x,bl->y),status_get_dir(bl)))
				{
					rdamage += (damage * sc->data[SC_DEATHBOUND].val2 * 7) / 1000;	// 7割反射ダメージ
					damage = (damage * sc->data[SC_DEATHBOUND].val2 * 3) / 1000;	// 3割自身が受ける
					clif_skill_damage(bl, src, tick, dmg.amotion, dmg.dmotion, rdamage, 0, RK_DEATHBOUND, sc->data[SC_DEATHBOUND].val1, 1);
					clif_skill_nodamage(bl, dsrc, RK_DEATHBOUND, sc->data[SC_DEATHBOUND].val1, 1);
					skill_blown(bl, src, skill_get_blewcount(RK_DEATHBOUND, sc->data[SC_DEATHBOUND].val1) | SAB_NODAMAGE);
					status_change_end(bl,SC_DEATHBOUND,-1);
				}
				// シールドスペル(DEF)
				if(sc && sc->data[SC_SHIELDSPELL_DEF].timer != -1 && sc->data[SC_SHIELDSPELL_DEF].val2 == 1)
				{
					rdamage += damage * sc->data[SC_SHIELDSPELL_DEF].val3 / 100;
				}
				// インスピレーション
				if(ssc && ssc->data[SC_INSPIRATION].timer != -1)
				{
					rdamage += damage / 100;
				}
				// 破碎柱反射
				if(sc && sc->data[SC_CRESCENTELBOW].timer != -1 && !(status_get_mode(src)&MD_BOSS) && atn_rand()%100 < sc->data[SC_CRESCENTELBOW].val2)
				{
					static struct Damage tmpwd = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
					int lv = sc->data[SC_CRESCENTELBOW].val1;
					int dist = unit_distance(bl,src);
					status_change_end(bl,SC_CRESCENTELBOW,-1);
					rdamage += damage + (damage * 20 * lv / 100);
					tmpwd = battle_calc_weapon_attack(bl,src,SR_CRESCENTELBOW_AUTOSPELL,lv,flag);
					rdamage += tmpwd.damage;
					damage = rdamage / 10;
					skill_blown(bl,src,skill_get_blewcount(SR_CRESCENTELBOW_AUTOSPELL,lv)|SAB_NODAMAGE);
					if(unit_distance(bl,src) < skill_get_blewcount(SR_CRESCENTELBOW_AUTOSPELL,lv) + dist) {
						skill_addtimerskill(bl,tick+200,src->id,0,0,SR_CRESCENTELBOW,lv,tick,(0x0f<<20)|0x0500);
					}
				}
			} else if(dmg.flag&BF_LONG) {	// 遠距離攻撃時
				if(tsd) {		// 対象がPCの時
					if(tsd->long_weapon_damage_return > 0) { // 遠距離攻撃跳ね返し
						rdamage += damage * tsd->long_weapon_damage_return / 100;
						if(rdamage < 1) rdamage = 1;
					}
				}
				// 反射耐性
				if(sd && sd->sub_return_damage && rdamage > 0) {
					rdamage = rdamage * (100 - sd->sub_return_damage) / 100;
					if(rdamage < 1) rdamage = 1;
				}
			}
			if(rdamage > 0)
				clif_damage(src,src,tick, dmg.amotion,0,rdamage,1,4,0,0);
		}
	}
	if(attack_type&BF_MAGIC && damage > 0 && src != bl && (sc && sc->data[SC_KYOMU].timer == -1)) {	// 魔法スキル＆ダメージあり＆使用者と対象者が違う＆虚無の影状態ではない
		if(tsd && src == dsrc) {	// 対象がPCの時
			if(tsd->magic_damage_return > 0 && atn_rand()%100 < tsd->magic_damage_return) {	// 魔法攻撃跳ね返し？※
				rdamage = damage;
				damage  = -1;	// ダメージ0だがmissを出さない
			}
		}
		// カイト
		if(damage > 0 && sc && sc->data[SC_KAITE].timer != -1 && skillid != HW_GRAVITATION
#ifndef PRE_RENEWAL
			 && src == dsrc && atn_rand()%100 < 50
#endif
		) {
			if(src->type == BL_PC || (status_get_lv(src) < 80 && !(status_get_mode(src)&MD_BOSS)))
			{
				int idx;
				clif_misceffect2(bl,438);
				clif_skill_nodamage(bl,src,skillid,skilllv,1);
				if(--sc->data[SC_KAITE].val2 == 0)
					status_change_end(bl,SC_KAITE,-1);

				if( sd && ssc && ssc->data[SC_WIZARD].timer != -1 &&
				    (idx = pc_search_inventory(sd,7321)) >= 0 ) {
					pc_delitem(sd,idx,1,0,1);
				} else {
					rdamage += damage;
				}
				damage = -1;	// ダメージ0だがmissを出さない
			}
		}
		// マジックミラー
		if(damage > 0 && sc && sc->data[SC_MAGICMIRROR].timer != -1) {
			if(atn_rand()%100 < sc->data[SC_MAGICMIRROR].val1 * 20) {
				clif_misceffect2(bl,675);
				clif_skill_nodamage(bl,src,skillid,skilllv,1);
				rdamage += damage;
				damage = -1;	// ダメージ0だがmissを出さない
			}
		}
		// 反射耐性
		if(sd && sd->sub_return_damage && rdamage > 0) {
			rdamage = rdamage * (100 - sd->sub_return_damage) / 100;
			if(rdamage < 1) rdamage = 1;
		}
		// 幻術 -朧幻想-
		if(damage > 0 && sc && sc->data[SC_GENSOU].timer != -1) {
			int odamage = BIGNUM2INT(damage * sc->data[SC_GENSOU].val2 / 100);
			int ar = skill_get_area(OB_OBOROGENSOU,sc->data[SC_GENSOU].val1);

			map_foreachinarea(battle_damage_area,bl->m,
				bl->x-ar,bl->y-ar,bl->x+ar,bl->y+ar,BL_CHAR,
				bl,odamage,OB_OBOROGENSOU,sc->data[SC_GENSOU].val1,flag|BCT_ENEMY|1,tick);
		}
		if(rdamage > 0) {
			clif_skill_damage(src, src, tick, dmg.amotion, dmg.dmotion, rdamage, dmg.div_, skillid, ((src == dsrc)? lv: -1), type);
			if(dmg.blewcount > 0 && !map[src->m].flag.gvg) {
				int dir = path_calc_dir(src,bl->x,bl->y);
				if(dir == 0)
					dir = 8;
				skill_blown(src,src,dmg.blewcount|(dir<<20));	// 対象に対する向きと逆方向に飛ばす
			}
			memset(&dmg,0,sizeof(dmg));
		}
	}
#ifndef PRE_RENEWAL
	if(sc && sc->data[SC_STEELBODY].timer != -1 && damage > 0) {
		damage = damage * 10 / 100;
	}
#endif

	/* ダメージパケット送信 */
	if(damage != -1) {
		switch(skillid) {
		case NPC_SELFDESTRUCTION:
		case NPC_SELFDESTRUCTION2:
			dmg.blewcount |= SAB_NODAMAGE;
			break;
		case WL_TETRAVORTEX_FIRE:	// テトラボルテックス(火)
		case WL_TETRAVORTEX_WATER:	// テトラボルテックス(水)
		case WL_TETRAVORTEX_WIND:	// テトラボルテックス(風)
		case WL_TETRAVORTEX_GROUND:	// テトラボルテックス(地)
			clif_skill_damage(dsrc, bl, tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, WL_TETRAVORTEX, lv, type);
			break;
		case SU_LUNATICCARROTBEAT2:
		case SU_PICKYPECK_DOUBLE_ATK:
			clif_skill_damage(dsrc, bl, tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skillid, -1, type);
			break;
		case RL_FIRE_RAIN:
		case HN_GROUND_GRAVITATION:
			clif_skill_damage(src, bl, tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skillid, lv, type);
			break;
		default:
			clif_skill_damage(dsrc, bl, tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skillid, lv, type);
			break;
		}
	} else {	// ダメージ消失時はパケット送信しない
		damage = 0;
	}

	/* 吹き飛ばし補正 */
	if(skillid == WZ_STORMGUST) {
		// アンデッド以外の通常MOBはストームガストの3HIT目で弾かれない(滑りの布石)
		if(sc && sc->data[SC_FREEZE].val3 == 2 && !(status_get_mode(bl)&MD_BOSS) && !battle_check_undead(status_get_race(bl), status_get_elem_type(bl))) {
			dmg.blewcount = 0;
		}
	}

	/* 吹き飛ばし処理とそのパケット */
	if(dmg.blewcount > 0 && !map[src->m].flag.gvg) {
		skill_blown(dsrc,bl,dmg.blewcount);
	}
	/* 吹き飛ばし処理とそのパケット カード効果 */
	if(dsrc->type == BL_PC && bl->type != BL_SKILL && !map[src->m].flag.gvg) {
		skill_add_blown(dsrc,bl,skillid,SAB_REVERSEBLOW);
	}

	map_freeblock_lock();

	/* リプロデュース & クローンスキル */
	if(damage > 0 && dmg.flag&BF_SKILL && tsd && sc) {
		/* リプロデュース判定 */
		if(sc->data[SC__REPRODUCE].timer != -1 && pc_checkskill(tsd,SC_REPRODUCE)) {
			skill_reproduce(tsd,skillid,skilllv);
		}
		/* クローンスキル判定 */
		else if(sc->data[SC_PRESERVE].timer == -1 && pc_checkskill(tsd,RG_PLAGIARISM)) {
			skill_clone(tsd,skillid,skilllv);
		}
	}

	/* 実際にダメージ処理を行う */
	if(skillid || flag) {
		switch(skillid) {
		case WZ_METEOR:
		case WZ_VERMILION:
		case WZ_STORMGUST:
		case NPC_SELFDESTRUCTION:
		case NPC_SELFDESTRUCTION2:
			battle_damage(src,bl,damage,skillid,skilllv,dmg.flag);
			break;
		case SR_TIGERCANNON:
			{
				int ar = skill_get_area(skillid,skilllv);
				/* 対象周辺にも同ダメージ */
				map_foreachinarea(battle_damage_area,bl->m,
					bl->x-ar,bl->y-ar,bl->x+ar,bl->y+ar,BL_CHAR,
					src,damage,skillid,skilllv,flag|BCT_ENEMY|1,tick);
			}
			break;
		default:
			battle_delay_damage(tick+dmg.amotion,src,bl,damage,skillid,skilllv,dmg.flag);
		}
	}

	if(skillid == RG_INTIMIDATE) {
		/* インティミデイト */
		if(damage > 0 && !(status_get_mode(bl)&MD_BOSS) && !map[src->m].flag.gvg) {
			int rate = 90 + status_get_lv(src) - status_get_lv(bl);
			if(atn_rand()%100 < rate)
				skill_addtimerskill(src,tick + status_get_amotion(src) + 500,bl->id,0,0,skillid,skilllv,0,flag);
		}
	} else if(skillid == NPC_EXPULSION) {
		/* エクスパルシオン */
		if(damage > 0 && !map[src->m].flag.gvg)
			skill_addtimerskill(src,tick + status_get_amotion(src) + 500,bl->id,0,0,skillid,skilllv,0,flag);
	}

	/* ダメージがあるなら追加効果判定 */
	if(bl->prev != NULL && !unit_isdead(bl)) {
		if((damage > 0 || skillid == SL_STUN || skillid == AG_DEADLY_PROJECTION
#ifndef PRE_RENEWAL
			|| dmg.dmg_lv == ATK_DEF
#endif
		) && skilllv >= 0) {
			// グラウンドドリフトはdsrcを引数として渡す
			if(skillid == GS_GROUNDDRIFT)
				skill_additional_effect(dsrc,bl,skillid,skilllv,attack_type,tick);
			else if(skillid != TK_TURNKICK)
				skill_additional_effect(src,bl,skillid,skilllv,attack_type,tick);

			if(tsd) {
				// 號砲のSP消費
				if(skillid == SR_TIGERCANNON) {
					pc_heal(tsd,0,BIGNUM2INT(-(damage / 10)),0,0);
				}
			}
		}

		if(bl->type == BL_MOB && src != bl)	// スキル使用条件のMOBスキル
		{
			struct mob_data *md = (struct mob_data *)bl;
			if(md) {
				int target = md->target_id;
				if(battle_config.mob_changetarget_byskill == 1 || target == 0)
				{
					if(src->type & (BL_PC | BL_HOM | BL_MERC | BL_ELEM))
						md->target_id = src->id;
				}
				mobskill_use(md,tick,MSC_SKILLUSED|(skillid<<16));
				md->target_id = target;
			}
		}
	}

	/* HP,SP吸収 */
	if(sd && dmg.flag&BF_WEAPON && src != bl && damage > 0) {
		if(src == dsrc || (dsrc->type == BL_SKILL && (skillid == SG_SUN_WARM || skillid == SG_MOON_WARM || skillid == SG_STAR_WARM || skillid == GS_DESPERADO || skillid == KO_HUUMARANKA))) {
			// ％吸収、一定吸収ともに
			battle_attack_drain(src, dmg.damage, dmg.damage2, 3);
		}
	}

	/* 反射ダメージの実際の処理 */
	if((skillid || flag) && rdamage > 0) {
		unsigned int asflag = EAS_WEAPON | EAS_ATTACK | EAS_NORMAL;

		if(attack_type&BF_WEAPON) {
			battle_delay_damage(tick+dmg.amotion,src,src,rdamage,skillid,skilllv,0);
			if(sd) {
				// 反射ダメージのオートスペル
				if(battle_config.weapon_reflect_autospell) {
					bonus_autospell_start(bl,src,asflag,tick,0);
				}
				if(battle_config.weapon_reflect_drain && src != bl)
					battle_attack_drain(bl,rdamage,0,battle_config.weapon_reflect_drain_enable_type);
			}
			if(tsd) {
				// 反射ダメージのアクティブアイテム
				if(battle_config.weapon_reflect_autospell)
					bonus_activeitem_start(tsd,asflag,tick);
			}
		} else {
			battle_damage(src,src,rdamage,skillid,skilllv,0);
			if(sd) {
				// 反射ダメージのオートスペル
				if(battle_config.magic_reflect_autospell) {
					bonus_autospell_start(bl,src,asflag,tick,0);
				}
				if(battle_config.magic_reflect_drain && src != bl)
					battle_attack_drain(bl,rdamage,0,battle_config.magic_reflect_drain_enable_type);
			}
			if(tsd) {
				// 反射ダメージのアクティブアイテム
				if(battle_config.magic_reflect_autospell)
					bonus_activeitem_start(tsd,asflag,tick);
			}
		}
	}

	/* オートカウンター */
	if(attack_type&BF_WEAPON && sc && sc->data[SC_AUTOCOUNTER].timer != -1 && sc->data[SC_AUTOCOUNTER].val4 > 0) {
		if(sc->data[SC_AUTOCOUNTER].val3 == dsrc->id)
			battle_weapon_attack(bl,dsrc,tick,0x8000|sc->data[SC_AUTOCOUNTER].val1);
		status_change_end(bl,SC_AUTOCOUNTER,-1);
	}
	/* ダブルキャスティング */
	if ((skillid == MG_COLDBOLT || skillid == MG_FIREBOLT || skillid == MG_LIGHTNINGBOLT) &&
		ssc && ssc->data[SC_DOUBLECASTING].timer != -1 &&
		atn_rand() % 100 < 30+10*ssc->data[SC_DOUBLECASTING].val1) {
		if (!(flag & 1)) {
			//skill_castend_delay (src, bl, skillid, skilllv, tick + dmg.div_*dmg.amotion, flag|1);
			skill_castend_delay (src, bl, skillid, skilllv, tick + 100, flag|1);
		}
	}
	/* ブラッドラスト */
	if(src->type == BL_HOM && ssc && ssc->data[SC_BLOODLUST].timer != -1 && dmg.flag&BF_WEAPON && src != bl && src == dsrc && damage > 0)
	{
		struct homun_data *hd = (struct homun_data *)src;
		if(hd && atn_rand()%100 < ssc->data[SC_BLOODLUST].val1*9)
		{
			homun_heal(hd,BIGNUM2INT(damage/5),0);
		}
	}
	/* ストーンハードスキン */
	if(attack_type&BF_WEAPON && sc && sc->data[SC_STONEHARDSKIN].timer != -1) {
		sc->data[SC_STONEHARDSKIN].val3 -= BIGNUM2INT(dmg.damage + dmg.damage2);
		if(sc->data[SC_STONEHARDSKIN].val3 <= 0)
			status_change_end(bl,SC_STONEHARDSKIN,-1);
		if(sd && atn_rand() % 10000 < 500) {
			pc_break_equip(sd, LOC_RARM);
		} else {
			status_change_start(src,SC_STRIPWEAPON,1,0,0,0,10000,0);
		}
	}
	/* デッドリーインフェクト */
	if(ssc && ssc->data[SC__DEADLYINFECT].timer != -1 && damage > 0) {
		status_change_copy(src,bl);
	}
	if(dmg.flag&BF_SHORT && sc && sc->data[SC__DEADLYINFECT].timer != -1 && damage > 0) {
		status_change_copy(bl,src);
	}
	/* サークルオブファイア */
	if(dmg.flag&BF_SHORT && sc && sc->data[SC_CIRCLE_OF_FIRE].timer != -1 && src != bl && damage > 0) {
		battle_skill_attack(BF_WEAPON,bl,bl,src,EL_CIRCLE_OF_FIRE,sc->data[SC_CIRCLE_OF_FIRE].val1,tick,(0x0f<<20)|0x0500);
	}
	// 魂の収穫
	if(sd && ssc && ssc->data[SC_SOULREAPER].timer != 1 && damage > 0 && atn_rand()%100 < ssc->data[SC_SOULREAPER].val2) {
		pc_addsoulenergy(sd,600000,1);
	}

	map_freeblock_unlock();

	return BIGNUM2INT(dmg.damage+dmg.damage2);	/* 与ダメを返す */
}

/*==========================================
 *
 *------------------------------------------
 */
int battle_skill_attack_area(struct block_list *bl,va_list ap)
{
	struct block_list *src,*dsrc;
	int atk_type,skillid,skilllv,flag,type;
	unsigned int tick;

	nullpo_retr(0, bl);
	nullpo_retr(0, ap);

	atk_type = va_arg(ap,int);
	if((src = va_arg(ap,struct block_list*)) == NULL)
		return 0;
	if((dsrc = va_arg(ap,struct block_list*)) == NULL)
		return 0;

	skillid = va_arg(ap,int);
	skilllv = va_arg(ap,int);
	tick    = va_arg(ap,unsigned int);
	flag    = va_arg(ap,int);
	type    = va_arg(ap,int);

	if(battle_check_target(dsrc,bl,type) > 0)
		battle_skill_attack(atk_type,src,dsrc,bl,skillid,skilllv,tick,flag);

	return 0;
}

/*==========================================
 * 不死判定
 *------------------------------------------
 */
int battle_check_undead(int race,int element)
{
	// element に属性値＋lv(status_get_element の戻り値)が渡されるミスに
	// 対応する為、elementから属性タイプだけを抜き出す。
	element %= 20;

	if(battle_config.undead_detect_type == 0) {
		if(element == ELE_UNDEAD)
			return 1;
	}
	else if(battle_config.undead_detect_type == 1) {
		if(race == RCT_UNDEAD)
			return 1;
	}
	else {
		if(element == ELE_UNDEAD || race == RCT_UNDEAD)
			return 1;
	}
	return 0;
}

/*==========================================
 * 敵味方判定(1=肯定, 0=否定, -1=エラー)
 * flag&0xf0000
 *   0x00000 BCT_NOENEMY: 敵じゃないか判定（ret:1＝敵ではない）
 *   0x10000 BCT_PARTY:   パーティー判定（ret:1=パーティーメンバ)
 *   0x20000 BCT_ALL:     全て(ret:1=敵味方両方)
 *   0x40000 BCT_ENEMY:   敵か判定(ret:1=敵)
 *   0x50000 BCT_NOPARTY: パーティーじゃないか判定(ret:1=パーティでない)
 *------------------------------------------
 */
int battle_check_target( struct block_list *src, struct block_list *target, int flag)
{
	int s_p, s_g, t_p, t_g;
	struct block_list *ss = src;

	nullpo_retr(-1, src);
	nullpo_retr(-1, target);

	if( flag & BCT_ENEMY ) {	// 反転フラグ
		int ret = battle_check_target(src, target, flag & (BCT_PARTY | BCT_ALL));
		if(ret != -1)
			return !ret;
		return -1;
	}

	if( flag & BCT_ALL ) {
		if( target->type == BL_MOB || (target->type == BL_PC && !pc_isinvisible((struct map_session_data *)target)) )
			return 1;
		if( target->type == BL_HOM && src->type != BL_SKILL )	// ホムはスキルユニットの影響を受けない
			return 1;
		else
			return -1;
	}

	switch(target->type) {
		case BL_PC:
			if(((struct map_session_data *)target)->invincible_timer != -1)
				return -1;
			break;
		case BL_PET:
			return -1;
		case BL_SKILL:
			if(src->type == BL_SKILL)
				return -1;	// スキルユニット同士なら無条件肯定

			switch(((struct skill_unit *)target)->group->unit_id) {
				case UNT_ICEWALL:
				case UNT_BLASTMINE:
				case UNT_CLAYMORETRAP:
				case UNT_REVERBERATION:
				case UNT_WALLOFTHORN:
					return 0;
			}
			break;
	}

	// スキルユニットの場合、親を求める
	if( src->type == BL_SKILL) {
		struct skill_unit_group *sg = ((struct skill_unit *)src)->group;
		int inf2;
		if( sg == NULL )
			return -1;
		inf2 = skill_get_inf2(sg->skill_id);
		if( (ss = map_id2bl(sg->src_id)) == NULL )
			return -1;
		if(ss->prev == NULL)
			return -1;
		if( target->type != BL_PC || !pc_isinvisible((struct map_session_data *)target) ) {
			if(inf2&INF2_DMG_PK && map[src->m].flag.pk)
				return 0;
			if(inf2&INF2_DMG_GVG && map[src->m].flag.gvg)
				return 0;
			if(inf2&INF2_PVP_DMG   && map[src->m].flag.pvp)
				return 0;
		}
		if(ss == target) {
			if(inf2&INF2_DMG_SELF)
				return 0;
			if(inf2&INF2_NO_SELF)
				return -1;
		}
	}

	// Mobでmaster_idがあってspecial_mob_aiなら、召喚主を求める
	if( src->type == BL_MOB ) {
		struct mob_data *md = (struct mob_data *)src;
		if(md) {
			if(md->master_id > 0) {
				if(md->master_id == target->id)	// 主なら肯定
					return 1;
				if(md->state.special_mob_ai != MOB_AI_NONE) {
					if(target->type == BL_MOB) {	// special_mob_aiで対象がMob
						struct mob_data *tmd = (struct mob_data *)target;
						if(tmd) {
							if(tmd->master_id != md->master_id)	// 召喚主が一緒でなければ否定
								return 0;
							else if(md->state.special_mob_ai == MOB_AI_SPHERE2)	// 召喚主が一緒なので肯定したいけど自爆は否定
								return 0;
							else
								return 1;
						}
					} else if(target->type & (BL_HOM | BL_MERC | BL_ELEM)) {
						// special_mob_aiで対象がホム、傭兵、精霊ならエラーで返す
						return -1;
					}
				}
				if((ss = map_id2bl(md->master_id)) == NULL)
					return -1;
			}
			// 通常AIモンスターで相手がモンスター
			if(md->state.special_mob_ai == MOB_AI_NONE && target->type == BL_MOB) {
				struct mob_data *tmd = (struct mob_data *)target;
				if(tmd && (tmd->state.special_mob_ai == MOB_AI_ABR || tmd->state.special_mob_ai == MOB_AI_BIONIC)) {
					// 相手がABRまたはバイオニックMOBなら否定
					return 0;
				}
			}
		}
	}

	if( src == target || ss == target )	// 同じなら肯定
		return 1;

	if(target->type == BL_PC && pc_isinvisible((struct map_session_data *)target))
		return -1;

	if( src->prev == NULL || unit_isdead(src) ) // 死んでるならエラー
		return -1;

	// PCからMOB
	if(ss->type == BL_PC && target->type == BL_MOB) {
		int guardian = 0;
		struct mob_data *md         = (struct mob_data *)target;
		struct map_session_data *sd = (struct map_session_data *)ss;
		struct guild_castle *gc = NULL;

		// レギオンモンスター
		if(md->state.special_mob_ai == MOB_AI_LEGION) {
			if(sd->hd && sd->hd->bl.id == md->master_id)
				return 1;
		}

		// ABR/バイオニックモンスター
		if(md->state.special_mob_ai == MOB_AI_ABR || md->state.special_mob_ai == MOB_AI_BIONIC) {
			// 主を求める
			struct block_list *master = map_id2bl(md->master_id);
			if(master == NULL)
				return -1;
			// 主に置き換えて再探索
			return battle_check_target(src, master, flag);
		}

		// 砦のガーディアンかどうか
		if(md->guild_id > 0) {
			gc = guild_mapid2gc(md->bl.m);
			if(gc) {
				int i;
				for(i = 0; i < sizeof(gc->guardian) / sizeof(gc->guardian[0]); i++) {
					if(gc->guardian[i].id == md->bl.id) {
						guardian = 1;
						break;
					}
				}
			}
		}

		// GvG時間外
		if(!map[ss->m].flag.gvg) {
			// ガーディアンと念のためエンペは味方
			if(md->class_ == MOBID_EMPERIUM || guardian)
				return 1;
			// それ以外は敵
			return 0;
		}

		/* ここから flag.gvg がある処理 */

		// ギルド無しPCは全部敵
		if(sd->status.guild_id <= 0)
			return 0;

		// ギルド城情報の補完
		if(md->guild_id <= 0)
			gc = guild_mapid2gc(md->bl.m);

		// 自分のギルドか同盟ギルド砦
		if(gc) {
			struct guild *g = guild_search(sd->status.guild_id);
			if(g == NULL)
				return 0;

			if(g->guild_id == gc->guild_id || guild_check_alliance(md->guild_id, sd->status.guild_id, 0)) {
				// エンペとガーディアンは味方
				if(md->class_ == MOBID_EMPERIUM || guardian)
					return 1;
				// それ以外のバイオプラント、スフィアマインなどは敵（攻撃可能）
				return 0;
			}
		}

		// 他人の砦、砦以外のGvGモードなら全部敵
		return 0;
	}
	if(ss->type == BL_MOB && target->type == BL_PC) {
		return 0;	// MOB→PCなら敵
	}

	if(ss->type == BL_PET && target->type == BL_MOB) {
		struct pet_data *pd = (struct pet_data*)ss;
		struct mob_data *md = (struct mob_data*)target;
		int mode = mobdb_search(pd->class_)->mode;
		int race = mobdb_search(pd->class_)->race;
		if(mobdb_search(pd->class_)->mexp <= 0 && !(mode&MD_BOSS) && (md->sc.option & (OPTION_HIDE | OPTION_CLOAKING) && race != RCT_INSECT && race != RCT_DEMON) ) {
			return 1; // 失敗
		} else {
			return 0; // 成功
		}
	}

	s_p = status_get_party_id(ss);
	s_g = status_get_guild_id(ss);

	t_p = status_get_party_id(target);
	t_g = status_get_guild_id(target);

	if(flag & BCT_PARTY) {
		if(s_p && t_p && s_p == t_p)
			return 1;	// 同じパーティなら肯定（味方）
		else
			return 0;	// パーティ検索なら同じパーティじゃない時点で否定
	}

	if(ss->type == BL_MOB && s_g > 0 && t_g > 0 && s_g == t_g )	// 同じギルド/mobクラスなら肯定（味方）
		return 1;

	if(ss->type == BL_PC && target->type == BL_PC) {	// 両方PVPモードなら否定（敵）
		struct skill_unit *su = NULL;
		if(src->type == BL_SKILL)
			su = (struct skill_unit *)src;
		// PK
		if(map[ss->m].flag.pk) {
			struct guild *g = NULL;
			struct map_session_data* ssd = (struct map_session_data*)ss;
			struct map_session_data* tsd = (struct map_session_data*)target;

			// battle_config.no_pk_level以下　1次は味方　転生は駄目
			if(ssd->sc.data[SC_PK_PENALTY].timer != -1)
				return 1;
			if(ssd->status.base_level <= battle_config.no_pk_level && (ssd->s_class.job <= PC_JOB_MC || ssd->s_class.job == PC_JOB_TK) && ssd->s_class.upper != PC_UPPER_HIGH)
				return 1;
			if(tsd->status.base_level <= battle_config.no_pk_level && (tsd->s_class.job <= PC_JOB_MC || tsd->s_class.job == PC_JOB_TK) && tsd->s_class.upper != PC_UPPER_HIGH)
				return 1;
			if(su && su->group && su->group->target_flag == BCT_NOENEMY)
				return 1;
			if(s_p > 0 && t_p > 0 && s_p == t_p)
				return 1;
			else if(s_g > 0 && t_g > 0 && s_g == t_g)
				return 1;
			if((g = guild_search(s_g)) != NULL) {
				int i;
				for(i=0; i<MAX_GUILDALLIANCE; i++) {
					if(g->alliance[i].guild_id > 0 && g->alliance[i].guild_id == t_g) {
						if(g->alliance[i].opposition)
							return 0;	// 敵対ギルドなら無条件に敵
						else
							return 1;	// 同盟ギルドなら無条件に味方
					}
				}
			}
			return 0;
		}
		// PVP
		if(map[ss->m].flag.pvp) {
			if(su && su->group->target_flag == BCT_NOENEMY)
				return 1;
			if(map[ss->m].flag.pvp_noparty && s_p > 0 && t_p > 0 && s_p == t_p)
				return 1;
			else if(map[ss->m].flag.pvp_noguild && s_g > 0 && t_g > 0 && s_g == t_g)
				return 1;
			return 0;
		}
		// GVG
		if(map[src->m].flag.gvg) {
			struct guild *g = NULL;
			if(su && su->group->target_flag == BCT_NOENEMY)
				return 1;
			if(s_g > 0 && s_g == t_g)
				return 1;
			if(map[src->m].flag.gvg_noparty && s_p > 0 && t_p > 0 && s_p == t_p)
				return 1;
			if((g = guild_search(s_g)) != NULL) {
				int i;
				for(i=0; i<MAX_GUILDALLIANCE; i++) {
					if(g->alliance[i].guild_id > 0 && g->alliance[i].guild_id == t_g) {
						if(g->alliance[i].opposition)
							return 0;	// 敵対ギルドなら無条件に敵
						else
							return 1;	// 同盟ギルドなら無条件に味方
					}
				}
			}
			return 0;
		}
	}

	if(ss->type == BL_MOB && (target->type & (BL_HOM | BL_MERC | BL_ELEM)))
		return 0;	// MOB vs HOM, MERC, ELEM なら敵

	if((ss->type & (BL_HOM | BL_MERC | BL_ELEM)) && target->type == BL_MOB) {
		struct mob_data *md = (struct mob_data *)target;
		if(md && md->state.special_mob_ai == MOB_AI_LEGION && md->master_id > 0) {
			if(md->master_id == ss->id)	// 主なら肯定
				return 1;
		}
		return 0;	// HOM, MERC, ELEM vs MOB なら敵
	}

	if(!(map[ss->m].flag.pvp || map[ss->m].flag.gvg)) {
		if(ss->type == BL_PC && (target->type & (BL_HOM | BL_MERC | BL_ELEM)))
			return 1;	// PvでもGvでもないなら、PC vs HOM, MERC, ELEM は味方

		if((ss->type & (BL_HOM | BL_MERC | BL_ELEM)) && target->type == BL_PC)
			return 1;	// PvでもGvでもないなら、HOM, MERC, ELEM vs PC は味方
	}

	// 同PTとか同盟Guildとかは後回し（＝＝
	if(ss->type & (BL_HOM | BL_MERC | BL_ELEM)) {
		if(map[ss->m].flag.pvp) {	// PVP
			if(target->type & (BL_HOM | BL_MERC | BL_ELEM))
				return 0;
			if(target->type == BL_PC) {
				struct map_session_data *msd = map_bl2msd(ss);

				if(msd == NULL || target != &msd->bl)
					return 0;
			}
		}
		if(map[ss->m].flag.gvg) {	// GVG
			if(target->type & (BL_HOM | BL_MERC | BL_ELEM))
				return 0;
		}
	}
	if(ss->type == BL_PC && (target->type & (BL_HOM | BL_MERC | BL_ELEM))) {
		if(map[ss->m].flag.pvp) {	// PVP
			struct map_session_data *msd = map_bl2msd(target);

			if(msd == NULL || ss != &msd->bl)
				return 0;
		}
		if(map[ss->m].flag.gvg) {	// GVG
			return 0;
		}
	}
	return 1;	// 該当しないので無関係人物（まあ敵じゃないので味方）
}

/*==========================================
 * 射程判定
 *------------------------------------------
 */
int battle_check_range(struct block_list *src,struct block_list *bl,int range)
{
	int arange;

	nullpo_retr(0, src);
	nullpo_retr(0, bl);

	arange = path_distance(src->x,src->y,bl->x,bl->y);

	if(src->m != bl->m)	// 違うマップ
		return 0;

	if(range > 0 && range < arange)	// 遠すぎる
		return 0;

	if(arange < 2)	// 同じマスか隣接
		return 1;

	// 障害物判定
	return path_search_long(NULL,src->m,src->x,src->y,bl->x,bl->y);
}

/*==========================================
 * 矢の消費
 *------------------------------------------
 */
int battle_delarrow(struct map_session_data* sd,int num,int skillid)
{
	int mask = 0, idx = -1;

	nullpo_retr(0, sd);

	if(skillid == 0) {	// 通常攻撃
		switch(sd->status.weapon) {
			case WT_BOW:
				mask = 1;
				break;
			case WT_HANDGUN:
				mask = 4;
				break;
			case WT_RIFLE:
				mask = 8;
				break;
			case WT_SHOTGUN:
				mask = 16;
				break;
			case WT_GATLING:
				mask = 32;
				break;
			case WT_GRENADE:
				mask = 64;
				break;
			default:
				return 1;
		}
	} else {		// スキル攻撃
		mask = skill_get_arrow_type(skillid);
	}

	idx = sd->equip_index[EQUIP_INDEX_ARROW];
	if(idx >= 0 && sd->status.inventory[idx].amount >= num && sd->inventory_data[idx]->arrow_type & mask) {
		if(battle_config.arrow_decrement)
			pc_delitem(sd,idx,num,0,0);
	} else {
		clif_arrow_fail(sd,0);
		return 0;
	}
	return 1;
}

/*==========================================
 * ダメージなしで共闘に参加
 *------------------------------------------
 */
void battle_join_struggle(struct mob_data *md,struct block_list *src)
{
	nullpo_retv(md);
	nullpo_retv(src);

	if(src->type == BL_PC) {
		struct map_session_data *sd = (struct map_session_data *)src;
		if(sd) {
			if(!linkdb_exists( &md->dmglog, INT2PTR(sd->status.char_id) )) {
				// ダメージなしで戦闘参加者入り
				linkdb_insert( &md->dmglog, INT2PTR(sd->status.char_id), INT2PTR(0) );
			}
		}
	}
	return;
}

/*==========================================
 * 設定ファイル読み込み用（フラグ）
 *------------------------------------------
 */
static int battle_config_switch(const char *str)
{
	if(strcmpi(str,"on") == 0 || strcmpi(str,"yes") == 0)
		return 1;
	if(strcmpi(str,"off") == 0 || strcmpi(str,"no") == 0)
		return 0;
	return atoi(str);
}

/*==========================================
 * 設定ファイルを読み込む
 *------------------------------------------
 */
int battle_config_read(const char *cfgName)
{
	int i;
	char line[1024],w1[1024],w2[1024];
	FILE *fp;
	static int count = 0;
	static const struct {
		const char *str;
		int *val;
		int defval;
	} data[] = {
		{ "warp_point_debug",                   &battle_config.warp_point_debug,                   0        },
		{ "enemy_critical",                     &battle_config.enemy_critical,                     0        },
		{ "enemy_critical_rate",                &battle_config.enemy_critical_rate,                100      },
		{ "enemy_str",                          &battle_config.enemy_str,                          1        },
		{ "enemy_perfect_flee",                 &battle_config.enemy_perfect_flee,                 0        },
		{ "casting_rate",                       &battle_config.cast_rate,                          100      },
		{ "no_casting_dex",                     &battle_config.no_cast_dex,                        150      },
		{ "delay_rate",                         &battle_config.delay_rate,                         100      },
		{ "delay_dependon_dex",                 &battle_config.delay_dependon_dex,                 0        },
		{ "no_delay_dex",                       &battle_config.no_delay_dex,                       150      },
		{ "skill_delay_attack_enable",          &battle_config.sdelay_attack_enable,               1        },
		{ "left_cardfix_to_right",              &battle_config.left_cardfix_to_right,              1        },
		{ "player_skill_add_range",             &battle_config.pc_skill_add_range,                 0        },
		{ "skill_out_range_consume",            &battle_config.skill_out_range_consume,            1        },
		{ "monster_skill_add_range",            &battle_config.mob_skill_add_range,                0        },
		{ "player_damage_delay",                &battle_config.pc_damage_delay,                    1        },
		{ "player_damage_delay_rate",           &battle_config.pc_damage_delay_rate,               10       },
		{ "defunit_not_enemy",                  &battle_config.defnotenemy,                        0        },
		{ "random_monster_checklv",             &battle_config.random_monster_checklv,             1        },
		{ "attribute_recover",                  &battle_config.attr_recover,                       1        },
		{ "flooritem_lifetime",                 &battle_config.flooritem_lifetime,                 60000    },
		{ "item_auto_get",                      &battle_config.item_auto_get,                      0        },
		{ "item_first_get_time",                &battle_config.item_first_get_time,                3000     },
		{ "item_second_get_time",               &battle_config.item_second_get_time,               1000     },
		{ "item_third_get_time",                &battle_config.item_third_get_time,                1000     },
		{ "mvp_item_first_get_time",            &battle_config.mvp_item_first_get_time,            10000    },
		{ "mvp_item_second_get_time",           &battle_config.mvp_item_second_get_time,           10000    },
		{ "mvp_item_third_get_time",            &battle_config.mvp_item_third_get_time,            2000     },
		{ "item_rate",                          &battle_config.item_rate,                          100      },
		{ "drop_rate0item",                     &battle_config.drop_rate0item,                     0        },
		{ "base_exp_rate",                      &battle_config.base_exp_rate,                      100      },
		{ "job_exp_rate",                       &battle_config.job_exp_rate,                       100      },
		{ "death_penalty_type",                 &battle_config.death_penalty_type,                 3        },
		{ "death_penalty_base",                 &battle_config.death_penalty_base,                 100      },
		{ "death_penalty_job",                  &battle_config.death_penalty_job,                  100      },
		{ "zeny_penalty",                       &battle_config.zeny_penalty,                       0        },
		{ "zeny_penalty_percent",               &battle_config.zeny_penalty_percent,               0        },
		{ "zeny_penalty_by_lvl",                &battle_config.zeny_penalty_by_lvl,                0        },
		{ "restart_hp_rate",                    &battle_config.restart_hp_rate,                    0        },
		{ "restart_sp_rate",                    &battle_config.restart_sp_rate,                    0        },
		{ "mvp_hp_rate",                        &battle_config.mvp_hp_rate,                        100      },
		{ "mvp_item_rate",                      &battle_config.mvp_item_rate,                      100      },
		{ "mvp_exp_rate",                       &battle_config.mvp_exp_rate,                       100      },
		{ "monster_hp_rate",                    &battle_config.monster_hp_rate,                    100      },
		{ "monster_max_aspd",                   &battle_config.monster_max_aspd,                   200      },
		{ "homun_max_aspd",                     &battle_config.homun_max_aspd,                     200      },
		{ "merc_max_aspd",                      &battle_config.merc_max_aspd,                      200      },
		{ "elem_max_aspd",                      &battle_config.elem_max_aspd,                      200      },
		{ "atcommand_gm_only",                  &battle_config.atc_gmonly,                         1        },
		{ "gm_all_skill",                       &battle_config.gm_allskill,                        1        },
		{ "gm_all_skill_add_abra",              &battle_config.gm_allskill_addabra,                0        },
		{ "gm_all_equipment",                   &battle_config.gm_allequip,                        0        },
		{ "gm_skill_unconditional",             &battle_config.gm_skilluncond,                     0        },
		{ "player_skillfree",                   &battle_config.skillfree,                          0        },
		{ "player_skillup_limit",               &battle_config.skillup_limit,                      0        },
		{ "weapon_produce_rate",                &battle_config.wp_rate,                            100      },
		{ "potion_produce_rate",                &battle_config.pp_rate,                            100      },
		{ "deadly_potion_produce_rate",         &battle_config.cdp_rate,                           100      },
		{ "monster_active_enable",              &battle_config.monster_active_enable,              1        },
		{ "monster_damage_delay_rate",          &battle_config.monster_damage_delay_rate,          100      },
		{ "monster_loot_type",                  &battle_config.monster_loot_type,                  0        },
		{ "mob_skill_use",                      &battle_config.mob_skill_use,                      1        },
		{ "mob_count_rate",                     &battle_config.mob_count_rate,                     100      },
		{ "mob_delay_rate",                     &battle_config.mob_delay_rate,                     100      },
		{ "mob_middle_boss_delay_rate",         &battle_config.mob_middle_boss_delay_rate,         100      },
		{ "mob_mvp_boss_delay_rate",            &battle_config.mob_mvp_boss_delay_rate,            100      },
		{ "quest_skill_learn",                  &battle_config.quest_skill_learn,                  0        },
		{ "basic_skill_check",                  &battle_config.basic_skill_check,                  1        },
		{ "guild_emperium_check",               &battle_config.guild_emperium_check,               1        },
		{ "guild_emblem_colors",                &battle_config.guild_emblem_colors,                3        },
		{ "guild_exp_limit",                    &battle_config.guild_exp_limit,                    50       },
		{ "player_invincible_time" ,            &battle_config.pc_invincible_time,                 5000     },
		{ "pet_catch_rate",                     &battle_config.pet_catch_rate,                     100      },
		{ "pet_rename",                         &battle_config.pet_rename,                         0        },
		{ "pet_friendly_rate",                  &battle_config.pet_friendly_rate,                  100      },
		{ "pet_hungry_delay_rate",              &battle_config.pet_hungry_delay_rate,              100      },
		{ "pet_hungry_friendly_decrease",       &battle_config.pet_hungry_friendly_decrease,       5        },
		{ "pet_str",                            &battle_config.pet_str,                            1        },
		{ "pet_status_support",                 &battle_config.pet_status_support,                 1        },
		{ "pet_attack_support",                 &battle_config.pet_attack_support,                 0        },
		{ "pet_damage_support",                 &battle_config.pet_damage_support,                 0        },
		{ "pet_support_rate",                   &battle_config.pet_support_rate,                   100      },
		{ "pet_attack_exp_to_master",           &battle_config.pet_attack_exp_to_master,           0        },
		{ "pet_attack_exp_rate",                &battle_config.pet_attack_exp_rate,                100      },
		{ "skill_min_damage",                   &battle_config.skill_min_damage,                   0        },
		{ "finger_offensive_type",              &battle_config.finger_offensive_type,              0        },
		{ "heal_exp",                           &battle_config.heal_exp,                           0        },
		{ "resurrection_exp",                   &battle_config.resurrection_exp,                   0        },
		{ "shop_exp",                           &battle_config.shop_exp,                           0        },
		{ "combo_delay_rate",                   &battle_config.combo_delay_rate,                   100      },
		{ "item_check",                         &battle_config.item_check,                         0        },
		{ "wedding_modifydisplay",              &battle_config.wedding_modifydisplay,              1        },
		{ "natural_healhp_interval",            &battle_config.natural_healhp_interval,            6000     },
		{ "natural_healsp_interval",            &battle_config.natural_healsp_interval,            8000     },
		{ "natural_heal_skill_interval",        &battle_config.natural_heal_skill_interval,        10000    },
		{ "natural_heal_weight_rate",           &battle_config.natural_heal_weight_rate,           50       },
		{ "natural_heal_weight_rate_icon",      &battle_config.natural_heal_weight_rate_icon,      0        },
		{ "item_name_override_grffile",         &battle_config.item_name_override_grffile,         0        },
		{ "arrow_decrement",                    &battle_config.arrow_decrement,                    1        },
		{ "allow_any_weapon_autoblitz",         &battle_config.allow_any_weapon_autoblitz,         0        },
		{ "max_aspd",                           &battle_config.max_aspd,                           200      },
		{ "pk_max_aspd",                        &battle_config.pk_max_aspd,                        200      },
		{ "gvg_max_aspd",                       &battle_config.gvg_max_aspd,                       200      },
		{ "pvp_max_aspd",                       &battle_config.pvp_max_aspd,                       200      },
		{ "max_hp",                             &battle_config.max_hp,                             999999   },
		{ "max_sp",                             &battle_config.max_sp,                             999999   },
		{ "max_parameter",                      &battle_config.max_parameter,                      99       },
		{ "max_cart_weight",                    &battle_config.max_cart_weight,                    8000     },
		{ "player_skill_log",                   &battle_config.pc_skill_log,                       0        },
		{ "monster_skill_log",                  &battle_config.mob_skill_log,                      0        },
		{ "battle_log",                         &battle_config.battle_log,                         0        },
		{ "save_log",                           &battle_config.save_log,                           0        },
		{ "error_log",                          &battle_config.error_log,                          1        },
		{ "etc_log",                            &battle_config.etc_log,                            1        },
		{ "save_clothcolor",                    &battle_config.save_clothcolor,                    0        },
		{ "undead_detect_type",                 &battle_config.undead_detect_type,                 0        },
		{ "player_auto_counter_type",           &battle_config.pc_auto_counter_type,               0        },
		{ "monster_auto_counter_type",          &battle_config.monster_auto_counter_type,          1        },
		{ "min_hitrate",                        &battle_config.min_hitrate,                        5        },
		{ "agi_penalty_type",                   &battle_config.agi_penalty_type,                   1        },
		{ "agi_penalty_count",                  &battle_config.agi_penalty_count,                  3        },
		{ "agi_penalty_num",                    &battle_config.agi_penalty_num,                    10       },
		{ "agi_penalty_count_lv",               &battle_config.agi_penalty_count_lv,               ATK_FLEE },
		{ "vit_penalty_type",                   &battle_config.vit_penalty_type,                   1        },
		{ "vit_penalty_count",                  &battle_config.vit_penalty_count,                  3        },
		{ "vit_penalty_num",                    &battle_config.vit_penalty_num,                    5        },
		{ "vit_penalty_count_lv",               &battle_config.vit_penalty_count_lv,               ATK_DEF  },
		{ "player_defense_type",                &battle_config.player_defense_type,                0        },
		{ "monster_defense_type",               &battle_config.monster_defense_type,               0        },
		{ "pet_defense_type",                   &battle_config.pet_defense_type,                   0        },
		{ "magic_defense_type",                 &battle_config.magic_defense_type,                 0        },
		{ "player_skill_reiteration",           &battle_config.pc_skill_reiteration,               0        },
		{ "monster_skill_reiteration",          &battle_config.monster_skill_reiteration,          0        },
		{ "player_skill_nofootset",             &battle_config.pc_skill_nofootset,                 1        },
		{ "monster_skill_nofootset",            &battle_config.monster_skill_nofootset,            0        },
		{ "player_cloak_check_type",            &battle_config.pc_cloak_check_type,                0        },
		{ "monster_cloak_check_type",           &battle_config.monster_cloak_check_type,           1        },
		{ "gvg_short_attack_damage_rate",       &battle_config.gvg_short_damage_rate,              100      },
		{ "gvg_long_attack_damage_rate",        &battle_config.gvg_long_damage_rate,               60       },
		{ "gvg_normal_attack_damage_rate",      &battle_config.gvg_normalskill_damage_rate,        60       },
		{ "gvg_magic_attack_damage_rate",       &battle_config.gvg_magic_damage_rate,              60       },
		{ "gvg_misc_attack_damage_rate",        &battle_config.gvg_misc_damage_rate,               60       },
		{ "gvg_eliminate_time",                 &battle_config.gvg_eliminate_time,                 7000     },
		{ "mob_changetarget_byskill",           &battle_config.mob_changetarget_byskill,           0        },
		{ "gvg_edp_down_rate",                  &battle_config.gvg_edp_down_rate,                  100      },
		{ "pvp_edp_down_rate",                  &battle_config.pvp_edp_down_rate,                  100      },
		{ "pk_edp_down_rate",                   &battle_config.pk_edp_down_rate,                   100      },
		{ "gvg_chargeattack_move",              &battle_config.gvg_chargeattack_move,              0        },
		{ "player_attack_direction_change",     &battle_config.pc_attack_direction_change,         1        },
		{ "monster_attack_direction_change",    &battle_config.monster_attack_direction_change,    0        },
		{ "player_land_skill_limit",            &battle_config.pc_land_skill_limit,                1        },
		{ "monster_land_skill_limit",           &battle_config.monster_land_skill_limit,           1        },
		{ "party_skill_penalty",                &battle_config.party_skill_penalty,                1        },
		{ "monster_class_change_full_recover",  &battle_config.monster_class_change_full_recover,  1        },
		{ "produce_item_name_input",            &battle_config.produce_item_name_input,            0        },
		{ "produce_potion_name_input",          &battle_config.produce_potion_name_input,          1        },
		{ "making_arrow_name_input",            &battle_config.making_arrow_name_input,            0        },
		{ "holywater_name_input",               &battle_config.holywater_name_input,               0        },
		{ "display_delay_skill_fail",           &battle_config.display_delay_skill_fail,           0        },
		{ "display_snatcher_skill_fail",        &battle_config.display_snatcher_skill_fail,        1        },
		{ "chat_warpportal",                    &battle_config.chat_warpportal,                    0        },
		{ "mob_warpportal",                     &battle_config.mob_warpportal,                     0        },
		{ "dead_branch_active",                 &battle_config.dead_branch_active,                 1        },
		{ "vending_max_value",                  &battle_config.vending_max_value,                  99990000 },
		{ "pet_lootitem",                       &battle_config.pet_lootitem,                       0        },
		{ "pet_weight",                         &battle_config.pet_weight,                         1000     },
		{ "show_steal_in_same_party",           &battle_config.show_steal_in_same_party,           0        },
		{ "enable_upper_class",                 &battle_config.enable_upper_class,                 1        },
		{ "pet_attack_attr_none",               &battle_config.pet_attack_attr_none,               0        },
		{ "mob_attack_attr_none",               &battle_config.mob_attack_attr_none,               1        },
		{ "pc_attack_attr_none",                &battle_config.pc_attack_attr_none,                0        },
		{ "gx_allhit",                          &battle_config.gx_allhit,                          0        },
		{ "gx_cardfix",                         &battle_config.gx_cardfix,                         0        },
		{ "gx_dupele",                          &battle_config.gx_dupele,                          1        },
		{ "gx_disptype",                        &battle_config.gx_disptype,                        1        },
		{ "devotion_level_difference",          &battle_config.devotion_level_difference,          10       },
		{ "player_skill_partner_check",         &battle_config.player_skill_partner_check,         1        },
		{ "sole_concert_type",                  &battle_config.sole_concert_type,                  2        },
		{ "hide_GM_session",                    &battle_config.hide_GM_session,                    0        },
		{ "invite_request_check",               &battle_config.invite_request_check,               1        },
		{ "gvg_trade_request_refused",          &battle_config.gvg_trade_request_refused,          1        },
		{ "pvp_trade_request_refused",          &battle_config.pvp_trade_request_refused,          1        },
		{ "skill_removetrap_type",              &battle_config.skill_removetrap_type,              0        },
		{ "disp_experience",                    &battle_config.disp_experience,                    1        },
		{ "castle_defense_rate",                &battle_config.castle_defense_rate,                100      },
		{ "riding_weight",                      &battle_config.riding_weight,                      10000    },
		{ "hp_rate",                            &battle_config.hp_rate,                            100      },
		{ "sp_rate",                            &battle_config.sp_rate,                            100      },
		{ "gm_can_drop_lv",                     &battle_config.gm_can_drop_lv,                     0        },
		{ "disp_hpmeter",                       &battle_config.disp_hpmeter,                       0        },
		{ "bone_drop",                          &battle_config.bone_drop,                          0        },
		{ "bone_drop_itemid",                   &battle_config.bone_drop_itemid,                   7005     },
		{ "item_rate_details",                  &battle_config.item_rate_details,                  0        },
		{ "item_rate_1",                        &battle_config.item_rate_1,                        100      },
		{ "item_rate_10",                       &battle_config.item_rate_10,                       100      },
		{ "item_rate_100",                      &battle_config.item_rate_100,                      100      },
		{ "item_rate_1000",                     &battle_config.item_rate_1000,                     100      },
		{ "item_rate_1_min",                    &battle_config.item_rate_1_min,                    1        },
		{ "item_rate_1_max",                    &battle_config.item_rate_1_max,                    9        },
		{ "item_rate_10_min",                   &battle_config.item_rate_10_min,                   10       },
		{ "item_rate_10_max",                   &battle_config.item_rate_10_max,                   99       },
		{ "item_rate_100_min",                  &battle_config.item_rate_100_min,                  100      },
		{ "item_rate_100_max",                  &battle_config.item_rate_100_max,                  999      },
		{ "item_rate_1000_min",                 &battle_config.item_rate_1000_min,                 1000     },
		{ "item_rate_1000_max",                 &battle_config.item_rate_1000_max,                 10000    },
		{ "monster_damage_delay",               &battle_config.monster_damage_delay,               0        },
		{ "card_drop_rate",                     &battle_config.card_drop_rate,                     100      },
		{ "equip_drop_rate",                    &battle_config.equip_drop_rate,                    100      },
		{ "consume_drop_rate",                  &battle_config.consume_drop_rate,                  100      },
		{ "refine_drop_rate",                   &battle_config.refine_drop_rate,                   100      },
		{ "etc_drop_rate",                      &battle_config.etc_drop_rate,                      100      },
		{ "potion_drop_rate",                   &battle_config.potion_drop_rate,                   100      },
		{ "arrow_drop_rate",                    &battle_config.arrow_drop_rate,                    100      },
		{ "petequip_drop_rate",                 &battle_config.petequip_drop_rate,                 100      },
		{ "weapon_drop_rate",                   &battle_config.weapon_drop_rate,                   100      },
		{ "other_drop_rate",                    &battle_config.other_drop_rate,                    100      },
		{ "item_res",                           &battle_config.item_res,                           1        },
		{ "next_exp_limit",                     &battle_config.next_exp_limit,                     0        },
		{ "monster_skill_over",                 &battle_config.monster_skill_over,                 10       },
		{ "finding_ore_drop_rate",              &battle_config.finding_ore_drop_rate,              100      },
		{ "joint_struggle_exp_bonus",           &battle_config.joint_struggle_exp_bonus,           25       },
		{ "joint_struggle_limit",               &battle_config.joint_struggle_limit,               600      },
		{ "pt_bonus_b",                         &battle_config.pt_bonus_b,                         0        },
		{ "pt_bonus_j",                         &battle_config.pt_bonus_j,                         0        },
		{ "mvp_announce",                       &battle_config.mvp_announce,                       0        },
		{ "petowneditem",                       &battle_config.petowneditem,                       0        },
		{ "pet_loot_type",                      &battle_config.pet_loot_type,                      1        },
		{ "buyer_name",                         &battle_config.buyer_name,                         0        },
		{ "noportal_flag",                      &battle_config.noportal_flag,                      1        },
		{ "once_autospell",                     &battle_config.once_autospell,                     1        },
		{ "allow_same_autospell",               &battle_config.allow_same_autospell,               1        },
		{ "combo_delay_lower_limits",           &battle_config.combo_delay_lower_limits,           0        },
		{ "tkcombo_delay_lower_limits",         &battle_config.tkcombo_delay_lower_limits,         0        },
		{ "new_marrige_skill",                  &battle_config.new_marrige_skill,                  1        },
		{ "reveff_plus_addeff",                 &battle_config.reveff_plus_addeff,                 0        },
		{ "summonslave_no_drop",                &battle_config.summonslave_no_drop,                0        },
		{ "summonslave_no_exp",                 &battle_config.summonslave_no_exp,                 0        },
		{ "summonslave_no_mvp",                 &battle_config.summonslave_no_mvp,                 0        },
		{ "summonmonster_no_drop",              &battle_config.summonmonster_no_drop,              0        },
		{ "summonmonster_no_exp",               &battle_config.summonmonster_no_exp,               0        },
		{ "summonmonster_no_mvp",               &battle_config.summonmonster_no_mvp,               0        },
		{ "cannibalize_no_drop",                &battle_config.cannibalize_no_drop,                1        },
		{ "cannibalize_no_exp",                 &battle_config.cannibalize_no_exp,                 1        },
		{ "cannibalize_no_mvp",                 &battle_config.cannibalize_no_mvp,                 1        },
		{ "spheremine_no_drop",                 &battle_config.spheremine_no_drop,                 1        },
		{ "spheremine_no_exp",                  &battle_config.spheremine_no_exp,                  1        },
		{ "spheremine_no_mvp",                  &battle_config.spheremine_no_mvp,                  1        },
		{ "branch_mob_no_drop",                 &battle_config.branch_mob_no_drop,                 0        },
		{ "branch_mob_no_exp",                  &battle_config.branch_mob_no_exp,                  0        },
		{ "branch_mob_no_mvp",                  &battle_config.branch_mob_no_mvp,                  0        },
		{ "branch_boss_no_drop",                &battle_config.branch_boss_no_drop,                0        },
		{ "branch_boss_no_exp",                 &battle_config.branch_boss_no_exp,                 0        },
		{ "branch_boss_no_mvp",                 &battle_config.branch_boss_no_mvp,                 0        },
		{ "pc_hit_stop_type",                   &battle_config.pc_hit_stop_type,                   0        },
		{ "nomanner_mode",                      &battle_config.nomanner_mode,                      1        },
		{ "death_by_unrig_penalty",             &battle_config.death_by_unrig_penalty,             0        },
		{ "dance_and_play_duration",            &battle_config.dance_and_play_duration,            20000    },
		{ "soulcollect_max_fail",               &battle_config.soulcollect_max_fail,               0        },
		{ "gvg_flee_rate",                      &battle_config.gvg_flee_rate,                      100      },
		{ "gvg_flee_penalty",                   &battle_config.gvg_flee_penalty,                   0        },
		{ "equip_sex",                          &battle_config.equip_sex,                          0        },
		{ "noexp_hiding",                       &battle_config.noexp_hiding,                       0        },
		{ "noexp_trickdead",                    &battle_config.noexp_trickdead,                    1        },
		{ "hide_attack",                        &battle_config.hide_attack,                        1        },
		{ "gm_hide_attack_lv",                  &battle_config.gm_hide_attack_lv,                  1        },
		{ "weapon_attack_autospell",            &battle_config.weapon_attack_autospell,            0        },
		{ "magic_attack_autospell",             &battle_config.magic_attack_autospell,             0        },
		{ "misc_attack_autospell",              &battle_config.misc_attack_autospell,              0        },
		{ "magic_attack_drain",                 &battle_config.magic_attack_drain,                 1        },
		{ "misc_attack_drain",                  &battle_config.misc_attack_drain,                  1        },
		{ "magic_attack_drain_enable_type",     &battle_config.magic_attack_drain_enable_type,     2        },
		{ "misc_attack_drain_enable_type",      &battle_config.misc_attack_drain_enable_type,      2        },
		{ "hallucianation_off",                 &battle_config.hallucianation_off,                 0        },
		{ "weapon_reflect_autospell",           &battle_config.weapon_reflect_autospell,           1        },
		{ "magic_reflect_autospell",            &battle_config.magic_reflect_autospell,            0        },
		{ "weapon_reflect_drain",               &battle_config.weapon_reflect_drain,               1        },
		{ "weapon_reflect_drain_enable_type",   &battle_config.weapon_reflect_drain_enable_type,   2        },
		{ "magic_reflect_drain",                &battle_config.magic_reflect_drain,                0        },
		{ "magic_reflect_drain_enable_type",    &battle_config.magic_reflect_drain_enable_type,    2        },
		{ "max_parameter_str",                  &battle_config.max_parameter_str,                  999      },
		{ "max_parameter_agi",                  &battle_config.max_parameter_agi,                  999      },
		{ "max_parameter_vit",                  &battle_config.max_parameter_vit,                  999      },
		{ "max_parameter_int",                  &battle_config.max_parameter_int,                  999      },
		{ "max_parameter_dex",                  &battle_config.max_parameter_dex,                  999      },
		{ "max_parameter_luk",                  &battle_config.max_parameter_luk,                  999      },
		{ "cannibalize_nocost",                 &battle_config.cannibalize_nocost,                 0        },
		{ "spheremine_nocost",                  &battle_config.spheremine_nocost,                  0        },
		{ "demonstration_nocost",               &battle_config.demonstration_nocost,               0        },
		{ "acidterror_nocost",                  &battle_config.acidterror_nocost,                  0        },
		{ "aciddemonstration_nocost",           &battle_config.aciddemonstration_nocost,           0        },
		{ "chemical_nocost",                    &battle_config.chemical_nocost,                    0        },
		{ "slimpitcher_nocost",                 &battle_config.slimpitcher_nocost,                 0        },
		{ "mes_send_type",                      &battle_config.mes_send_type,                      0        },
		{ "allow_assumptop_in_gvg",             &battle_config.allow_assumptop_in_gvg,             1        },
		{ "allow_falconassault_elemet",         &battle_config.allow_falconassault_elemet,         0        },
		{ "allow_guild_invite_in_gvg",          &battle_config.allow_guild_invite_in_gvg,          0        },
		{ "allow_guild_leave_in_gvg",           &battle_config.allow_guild_leave_in_gvg,           0        },
		{ "allow_guild_alliance_in_agit",       &battle_config.allow_guild_alliance_in_agit,       0        },
		{ "guild_skill_available",              &battle_config.guild_skill_available,              1        },
		{ "guild_hunting_skill_available",      &battle_config.guild_hunting_skill_available,      1        },
		{ "guild_skill_check_range",            &battle_config.guild_skill_check_range,            0        },
		{ "allow_guild_skill_in_gvg_only",      &battle_config.allow_guild_skill_in_gvg_only,      1        },
		{ "allow_me_guild_skill",               &battle_config.allow_me_guild_skill,               0        },
		{ "emergencycall_point_type",           &battle_config.emergencycall_point_type,           1        },
		{ "emergencycall_call_limit",           &battle_config.emergencycall_call_limit,           0        },
		{ "allow_guild_skill_in_gvgtime_only",  &battle_config.allow_guild_skill_in_gvgtime_only,  0        },
		{ "guild_skill_in_pvp_limit",           &battle_config.guild_skill_in_pvp_limit,           1        },
		{ "guild_exp_rate",                     &battle_config.guild_exp_rate,                     100      },
		{ "guild_skill_effective_range",        &battle_config.guild_skill_effective_range,        2        },
		{ "tarotcard_display_position",         &battle_config.tarotcard_display_position,         2        },
		{ "serverside_friendlist",              &battle_config.serverside_friendlist,              1        },
		{ "pet0078_hair_id",                    &battle_config.pet0078_hair_id,                    100      },
		{ "job_soul_check",                     &battle_config.job_soul_check,                     1        },
		{ "repeal_die_counter_rate",            &battle_config.repeal_die_counter_rate,            100      },
		{ "disp_job_soul_state_change",         &battle_config.disp_job_soul_state_change,         0        },
		{ "check_knowlege_map",                 &battle_config.check_knowlege_map,                 1        },
		{ "tripleattack_rate_up_keeptime",      &battle_config.tripleattack_rate_up_keeptime,      2000     },
		{ "tk_counter_rate_up_keeptime",        &battle_config.tk_counter_rate_up_keeptime,        2000     },
		{ "allow_skill_without_day",            &battle_config.allow_skill_without_day,            0        },
		{ "save_feel_map",                      &battle_config.save_feel_map,                      1        },
		{ "save_hate_mob",                      &battle_config.save_hate_mob,                      1        },
		{ "allow_se_univ_skill_limit",          &battle_config.allow_se_univ_skill_limit,          1        },
		{ "twilight_party_check",               &battle_config.twilight_party_check,               1        },
		{ "alchemist_point_type",               &battle_config.alchemist_point_type,               0        },
		{ "marionette_type",                    &battle_config.marionette_type,                    0        },
		{ "max_marionette_str",                 &battle_config.max_marionette_str,                 99       },
		{ "max_marionette_agi",                 &battle_config.max_marionette_agi,                 99       },
		{ "max_marionette_vit",                 &battle_config.max_marionette_vit,                 99       },
		{ "max_marionette_int",                 &battle_config.max_marionette_int,                 99       },
		{ "max_marionette_dex",                 &battle_config.max_marionette_dex,                 99       },
		{ "max_marionette_luk",                 &battle_config.max_marionette_luk,                 99       },
		{ "max_marionette_pk_str",              &battle_config.max_marionette_pk_str,              99       },
		{ "max_marionette_pk_agi",              &battle_config.max_marionette_pk_agi,              99       },
		{ "max_marionette_pk_vit",              &battle_config.max_marionette_pk_vit,              99       },
		{ "max_marionette_pk_int",              &battle_config.max_marionette_pk_int,              99       },
		{ "max_marionette_pk_dex",              &battle_config.max_marionette_pk_dex,              99       },
		{ "max_marionette_pk_luk",              &battle_config.max_marionette_pk_luk,              99       },
		{ "max_marionette_pvp_str",             &battle_config.max_marionette_pvp_str,             99       },
		{ "max_marionette_pvp_agi",             &battle_config.max_marionette_pvp_agi,             99       },
		{ "max_marionette_pvp_vit",             &battle_config.max_marionette_pvp_vit,             99       },
		{ "max_marionette_pvp_int",             &battle_config.max_marionette_pvp_int,             99       },
		{ "max_marionette_pvp_dex",             &battle_config.max_marionette_pvp_dex,             99       },
		{ "max_marionette_pvp_luk",             &battle_config.max_marionette_pvp_luk,             99       },
		{ "max_marionette_gvg_str",             &battle_config.max_marionette_gvg_str,             99       },
		{ "max_marionette_gvg_agi",             &battle_config.max_marionette_gvg_agi,             99       },
		{ "max_marionette_gvg_vit",             &battle_config.max_marionette_gvg_vit,             99       },
		{ "max_marionette_gvg_int",             &battle_config.max_marionette_gvg_int,             99       },
		{ "max_marionette_gvg_dex",             &battle_config.max_marionette_gvg_dex,             99       },
		{ "max_marionette_gvg_luk",             &battle_config.max_marionette_gvg_luk,             99       },
		{ "baby_status_max",                    &battle_config.baby_status_max,                    80       },
		{ "baby_hp_rate",                       &battle_config.baby_hp_rate,                       70       },
		{ "baby_sp_rate",                       &battle_config.baby_sp_rate,                       70       },
		{ "upper_hp_rate",                      &battle_config.upper_hp_rate,                      125      },
		{ "upper_sp_rate",                      &battle_config.upper_sp_rate,                      125      },
		{ "normal_hp_rate",                     &battle_config.normal_hp_rate,                     100      },
		{ "normal_sp_rate",                     &battle_config.normal_sp_rate,                     100      },
		{ "baby_weight_rate",                   &battle_config.baby_weight_rate,                   100      },
		{ "upper_weight_rate",                  &battle_config.upper_weight_rate,                  100      },
		{ "normal_weight_rate",                 &battle_config.normal_weight_rate,                 100      },
		{ "no_emergency_call",                  &battle_config.no_emergency_call,                  0        },
		{ "save_am_pharmacy_success",           &battle_config.save_am_pharmacy_success,           1        },
		{ "save_all_ranking_point_when_logout", &battle_config.save_all_ranking_point_when_logout, 0        },
		{ "soul_linker_battle_mode",            &battle_config.soul_linker_battle_mode,            0        },
		{ "soul_linker_battle_mode_ka",         &battle_config.soul_linker_battle_mode_ka,         0        },
		{ "skillup_type",                       &battle_config.skillup_type,                       1        },
		{ "allow_me_dance_effect",              &battle_config.allow_me_dance_effect,              0        },
		{ "allow_me_concert_effect",            &battle_config.allow_me_concert_effect,            0        },
		{ "allow_me_rokisweil",                 &battle_config.allow_me_rokisweil,                 0        },
		{ "pharmacy_get_point_type",            &battle_config.pharmacy_get_point_type,            0        },
		{ "soulskill_can_be_used_for_myself",   &battle_config.soulskill_can_be_used_for_myself,   0        },
		{ "hermode_wp_check_range",             &battle_config.hermode_wp_check_range,             3        },
		{ "hermode_wp_check",                   &battle_config.hermode_wp_check,                   1        },
		{ "hermode_no_walking",                 &battle_config.hermode_no_walking,                 0        },
		{ "hermode_gvg_only",                   &battle_config.hermode_gvg_only,                   1        },
		{ "redemptio_penalty_type",             &battle_config.redemptio_penalty_type,             1        },
		{ "allow_weaponrearch_to_weaponrefine", &battle_config.allow_weaponrearch_to_weaponrefine, 0        },
		{ "boss_no_knockbacking",               &battle_config.boss_no_knockbacking,               1        },
		{ "boss_no_element_change",             &battle_config.boss_no_element_change,             1        },
		{ "boss_teleport_on_landprotector",     &battle_config.boss_teleport_on_landprotector,     1        },
		{ "scroll_produce_rate",                &battle_config.scroll_produce_rate,                100      },
		{ "scroll_item_name_input",             &battle_config.scroll_item_name_input,             0        },
		{ "pet_leave",                          &battle_config.pet_leave,                          1        },
		{ "pk_short_attack_damage_rate",        &battle_config.pk_short_damage_rate,               100      },
		{ "pk_long_attack_damage_rate",         &battle_config.pk_long_damage_rate,                60       },
		{ "pk_normal_attack_damage_rate",       &battle_config.pk_normalskill_damage_rate,         60       },
		{ "pk_magic_attack_damage_rate",        &battle_config.pk_magic_damage_rate,               60       },
		{ "pk_misc_attack_damage_rate",         &battle_config.pk_misc_damage_rate,                60       },
		{ "cooking_rate",                       &battle_config.cooking_rate,                       100      },
		{ "making_rate",                        &battle_config.making_rate,                        100      },
		{ "extended_abracadabra",               &battle_config.extended_abracadabra,               0        },
		{ "no_pk_level",                        &battle_config.no_pk_level,                        60       },
		{ "allow_cloneskill_at_autospell",      &battle_config.allow_cloneskill_at_autospell,      0        },
		{ "pk_noshift",                         &battle_config.pk_noshift,                         0        },
		{ "pk_penalty_time",                    &battle_config.pk_penalty_time,                    60000    },
		{ "dropitem_itemrate_fix",              &battle_config.dropitem_itemrate_fix,              1        },
		{ "gm_nomanner_lv",                     &battle_config.gm_nomanner_lv,                     50       },
		{ "clif_fixpos_type",                   &battle_config.clif_fixpos_type,                   1        },
		{ "romailuse",                          &battle_config.romail,                             0        },
		{ "pc_die_script",                      &battle_config.pc_die_script,                      0        },
		{ "pc_kill_script",                     &battle_config.pc_kill_script,                     0        },
		{ "pc_movemap_script",                  &battle_config.pc_movemap_script,                  0        },
		{ "pc_login_script",                    &battle_config.pc_login_script,                    0        },
		{ "pc_logout_script",                   &battle_config.pc_logout_script,                   0        },
		{ "pc_mapin_script",                    &battle_config.pc_mapin_script,                    0        },
		{ "pc_mapout_script",                   &battle_config.pc_mapout_script,                   0        },
		{ "md_delete_script",                   &battle_config.md_delete_script,                   0        },
		{ "save_pckiller_type",                 &battle_config.save_pckiller_type,                 0        },
		{ "def_ratio_atk_to_shieldchain",       &battle_config.def_ratio_atk_to_shieldchain,       0        },
		{ "def_ratio_atk_to_carttermination",   &battle_config.def_ratio_atk_to_carttermination,   1        },
		{ "player_gravitation_type",            &battle_config.player_gravitation_type,            0        },
		{ "enemy_gravitation_type",             &battle_config.enemy_gravitation_type,             0        },
		{ "mob_attack_fixwalkpos",              &battle_config.mob_attack_fixwalkpos,              0        },
		{ "mob_ai_limiter",                     &battle_config.mob_ai_limiter,                     0        },
		{ "mob_ai_cpu_usage",                   &battle_config.mob_ai_cpu_usage,                   80       },
		{ "itemidentify",                       &battle_config.itemidentify,                       0        },
		{ "casting_penalty_type",               &battle_config.casting_penalty_type,               0        },
		{ "casting_penalty_weapon",             &battle_config.casting_penalty_weapon,             0        },
		{ "casting_penalty_shield",             &battle_config.casting_penalty_shield,             0        },
		{ "casting_penalty_armor",              &battle_config.casting_penalty_armor,              0        },
		{ "casting_penalty_helm",               &battle_config.casting_penalty_helm,               0        },
		{ "casting_penalty_robe",               &battle_config.casting_penalty_robe,               0        },
		{ "casting_penalty_shoes",              &battle_config.casting_penalty_shoes,              0        },
		{ "casting_penalty_acce",               &battle_config.casting_penalty_acce,               0        },
		{ "casting_penalty_arrow",              &battle_config.casting_penalty_arrow,              0        },
		{ "show_always_party_name",             &battle_config.show_always_party_name,             0        },
		{ "check_player_name_global_msg",       &battle_config.check_player_name_global_msg,       0        },
		{ "check_player_name_party_msg",        &battle_config.check_player_name_party_msg,        0        },
		{ "check_player_name_guild_msg",        &battle_config.check_player_name_guild_msg,        0        },
		{ "check_player_name_battle_msg",       &battle_config.check_player_name_battle_msg,       0        },
		{ "save_player_when_drop_item",         &battle_config.save_player_when_drop_item,         0        },
		{ "save_player_when_storage_closed",    &battle_config.save_player_when_storage_closed,    0        },
		{ "allow_homun_status_change",          &battle_config.allow_homun_status_change,          0        },
		{ "save_homun_temporal_intimate",       &battle_config.save_homun_temporal_intimate,       1        },
		{ "homun_intimate_rate",                &battle_config.homun_intimate_rate,                100      },
		{ "homun_temporal_intimate_resilience", &battle_config.homun_temporal_intimate_resilience, 50       },
		{ "hvan_explosion_intimate",            &battle_config.hvan_explosion_intimate,            45000    },
		{ "homun_speed_is_same_as_pc",          &battle_config.homun_speed_is_same_as_pc,          1        },
		{ "homun_skill_intimate_type",          &battle_config.homun_skill_intimate_type,          0        },
		{ "master_get_homun_base_exp",          &battle_config.master_get_homun_base_exp,          100      },
		{ "master_get_homun_job_exp",           &battle_config.master_get_homun_job_exp,           0        },
		{ "extra_system_flag",                  &battle_config.extra_system_flag,                  0        },
		{ "mob_take_over_sp",                   &battle_config.mob_take_over_sp,                   0        },
		{ "party_join_limit",                   &battle_config.party_join_limit,                   1        },
		{ "check_skillpos_range",               &battle_config.check_skillpos_range,               0        },
		{ "pet_speed_is_same_as_pc",            &battle_config.pet_speed_is_same_as_pc,            1        },
		{ "tax_rate",                           &battle_config.tax_rate,                           0        },
		{ "steal_rate",                         &battle_config.steal_rate,                         100      },
		{ "sw_def_type",                        &battle_config.sw_def_type,                        2        },
		{ "calc_dist_flag",                     &battle_config.calc_dist_flag,                     1        },
		{ "allow_sw_dist",                      &battle_config.allow_sw_dist,                      4        },
		{ "personal_storage_sort",              &battle_config.personal_storage_sort,              1        },
		{ "guild_storage_sort",                 &battle_config.guild_storage_sort,                 1        },
		{ "allow_es_magic_all",                 &battle_config.allow_es_magic_all,                 0        },
		{ "trap_is_invisible",                  &battle_config.trap_is_invisible,                  0        },
		{ "anklesnare_no_knockbacking",         &battle_config.anklesnare_no_knockbacking,         1        },
		{ "gm_perfect_hide",                    &battle_config.gm_perfect_hide,                    0        },
		{ "pcview_mob_clear_type",              &battle_config.pcview_mob_clear_type,              1        },
		{ "party_item_share_type",              &battle_config.party_item_share_type,              1        },
		{ "party_item_share_show",              &battle_config.party_item_share_show,              1        },
		{ "pk_murderer_point",                  &battle_config.pk_murderer_point,                  100      },
		{ "sg_miracle_rate",                    &battle_config.sg_miracle_rate,                    1        },
		{ "sg_angel_rate",                      &battle_config.sg_angel_rate,                      2        },
		{ "skill_autospell_delay_enable",       &battle_config.skill_autospell_delay_enable,       1        },
		{ "bonus_autospell_delay_enable",       &battle_config.bonus_autospell_delay_enable,       1        },
		{ "merc_speed_is_same_as_pc",           &battle_config.merc_speed_is_same_as_pc,           1        },
		{ "master_get_merc_base_exp",           &battle_config.master_get_merc_base_exp,           100      },
		{ "master_get_merc_job_exp",            &battle_config.master_get_merc_job_exp,            0        },
		{ "party_invite_range_check",           &battle_config.party_invite_range_check,           0        },
		{ "homun_skilldelay_reset",             &battle_config.homun_skilldelay_reset,             1        },
		{ "homun_statuschange_reset",           &battle_config.homun_statuschange_reset,           1        },
		{ "free_sc_data_dynamically",           &battle_config.free_sc_data_dynamically,           0        },
		{ "snovice_maxexp_border",              &battle_config.snovice_maxexp_border,              99999999 },
		{ "homun_rename",                       &battle_config.homun_rename,                       0        },
		{ "equip_window_type",                  &battle_config.equip_window_type,                  1        },
		{ "str_weight",                         &battle_config.str_weight,                         300      },
		{ "summonslave_generation",             &battle_config.summonslave_generation,             2        },
		{ "pvp_send_guild_xy",                  &battle_config.pvp_send_guild_xy,                  1        },
		{ "mvpitem_weight_limit",               &battle_config.mvpitem_weight_limit,               50       },
		{ "roki_item_autospell",                &battle_config.roki_item_autospell,                0        },
		{ "trap_splash_on",                     &battle_config.trap_splash_on,                     0        },
		{ "firepillar_splash_on",               &battle_config.firepillar_splash_on,               0        },
		{ "mob_nohitstop_rate",                 &battle_config.mob_nohitstop_rate,                 0        },
		{ "refinery_research_lv",               &battle_config.refinery_research_lv,               0        },
		{ "maprespawnguildid_all_players",      &battle_config.maprespawnguildid_all_players,      0        },
		{ "party_booking_lv",                   &battle_config.party_booking_lv,                   15       },
		{ "new_statusup_calc",                  &battle_config.new_statusup_calc,                  1        },
		{ "third_status_max",                   &battle_config.third_status_max,                   120      },
		{ "third_baby_status_max",              &battle_config.third_baby_status_max,              108      },
		{ "third_max_aspd",                     &battle_config.third_max_aspd,                     140      },
		{ "third_song_overlap",                 &battle_config.third_song_overlap,                 0        },
		{ "clif_fix_level",                     &battle_config.clif_fix_level,                     0        },
		{ "max_buyingstore_zeny",               &battle_config.max_buyingstore_zeny,               99990000 },
		{ "max_buyingstore_amount",             &battle_config.max_buyingstore_amount,             9999     },
		{ "allow_same_activeitem",              &battle_config.allow_same_activeitem,              0        },
		{ "disable_transform_when_gvg",         &battle_config.disable_transform_when_gvg,         1        },
		{ "esnv_status_max",                    &battle_config.esnv_status_max,                    120      },
		{ "esnv_baby_status_max",               &battle_config.esnv_baby_status_max,               108      },
		{ "esnv_max_aspd",                      &battle_config.esnv_max_aspd,                      140      },
		{ "ko_status_max",                      &battle_config.ko_status_max,                      120      },
		{ "ko_max_aspd",                        &battle_config.ko_max_aspd,                        140      },
		{ "rl_status_max",                      &battle_config.rl_status_max,                      120      },
		{ "rl_max_aspd",                        &battle_config.rl_max_aspd,                        140      },
		{ "sum_status_max",                     &battle_config.sum_status_max,                     125      },
		{ "sum_max_aspd",                       &battle_config.sum_max_aspd,                       140      },
		{ "disable_costume_when_gvg",           &battle_config.disable_costume_when_gvg,           1        },
		{ "elem_attackskill_rate",              &battle_config.elem_attackskill_rate,              500      },
		{ "slave_inherit_mode",                 &battle_config.slave_inherit_mode,                 2        },
		{ "no_casting_int",                     &battle_config.no_cast_int,                        115      },
		{ "monster_atk2_to_matk",               &battle_config.monster_atk2_to_matk,               1        },
		{ "npc_timeout_time",                   &battle_config.npc_timeout_time,                   60       },
		{ "use_packet_obfuscation",             &battle_config.use_packet_obfuscation,             1        },
		{ "greed_use_town",                     &battle_config.greed_use_town,                     0        },
		{ "alliance_message",                   &battle_config.alliance_message,                   0        },
		{ "ranker_potion_bonus",                &battle_config.ranker_potion_bonus,                150      },
		{ "ranker_potion_bonus_rogue",          &battle_config.ranker_potion_bonus_rogue,          200      },
		{ "ranker_weapon_bonus",                &battle_config.ranker_weapon_bonus,                10       },
		{ "min_lv100_aspdfix",                  &battle_config.min_lv100_aspdfix,                  1        },
		{ "min_npc_vendchat_distance",          &battle_config.min_npc_vendchat_distance,          3        },
		{ "ping_timer_interval",                &battle_config.ping_timer_interval,                30       },
		{ "enable_pet_autofeed",                &battle_config.enable_pet_autofeed,                1        },
		{ "enable_hom_autofeed",                &battle_config.enable_hom_autofeed,                1        },
		{ "homun_status_max",                   &battle_config.homun_status_max,                   150      },
		{ "skill_unit_interval_limit",          &battle_config.skill_unit_interval_limit,          1        },
		{ "max_skillpoint_nv",                  &battle_config.max_skillpoint_nv,                  9        },
		{ "max_skillpoint_1st",                 &battle_config.max_skillpoint_1st,                 58       },
		{ "max_skillpoint_n2nd",                &battle_config.max_skillpoint_n2nd,                107      },
		{ "max_skillpoint_n3rd",                &battle_config.max_skillpoint_n3rd,                176      },
		{ "max_skillpoint_2nd",                 &battle_config.max_skillpoint_2nd,                 127      },
		{ "max_skillpoint_3rd",                 &battle_config.max_skillpoint_3rd,                 196      },
		{ "max_skillpoint_tk2nd",               &battle_config.max_skillpoint_tk2nd,               107      },
		{ "max_skillpoint_tk3rd",               &battle_config.max_skillpoint_tk3rd,               176      },
		{ "max_skillpoint_ex1st",               &battle_config.max_skillpoint_ex1st,               78       },
		{ "max_skillpoint_ex2nd",               &battle_config.max_skillpoint_ex2nd,               147      },
		{ "max_skillpoint_snv",                 &battle_config.max_skillpoint_snv,                 107      },
		{ "max_skillpoint_esnv",                &battle_config.max_skillpoint_esnv,                176      },
		{ "max_skillpoint_doram",               &battle_config.max_skillpoint_doram,               59       },
		{ "pc_tstatus_max",                     &battle_config.pc_tstatus_max,                     100      },
		{ "mob_ai_area_size",                   &battle_config.mob_ai_area_size,                   36       },
		{ "mob_ai_sleeptime",                   &battle_config.mob_ai_sleeptime,                   60000    },
		{ "boss_ai_sleeptime",                  &battle_config.boss_ai_sleeptime,                  300000   },
		{ "fourth_jobchange_tstpoint",          &battle_config.fourth_jobchange_tstpoint,          7        },
		{ NULL,                                 NULL,                                              0        },
	};

	if(count++ == 0) {
		for(i=0; data[i].val; i++) {
			*data[i].val = data[i].defval;
		}
	}

	fp = fopen(cfgName,"r");
	if(fp == NULL) {
		printf("battle_config_read: open [%s] failed !\n", cfgName);
		return 1;
	}
	while(fgets(line,1020,fp)) {
		if(line[0] == '\0' || line[0] == '\r' || line[0] == '\n')
			continue;
		if(line[0] == '/' && line[1] == '/')
			continue;
		if(sscanf(line,"%1023[^:]:%1023s",w1,w2) != 2)
			continue;

		if(strcmpi(w1,"import") == 0) {
			battle_config_read(w2);
		} else {
			for(i=0; data[i].val; i++) {
				if(strcmpi(w1,data[i].str) == 0) {
					*data[i].val = battle_config_switch(w2);
					break;
				}
			}
			if(data[i].val == NULL)
				printf("unknown battle config : %s\a\n",w1);
		}
	}
	fclose(fp);

	// フラグ調整
	if(battle_config.allow_guild_skill_in_gvgtime_only)
		battle_config.guild_skill_available = 0;

	if(--count == 0) {
		if(battle_config.flooritem_lifetime < 1000)
			battle_config.flooritem_lifetime = 60*1000;
		if(battle_config.restart_hp_rate < 0)
			battle_config.restart_hp_rate = 0;
		else if(battle_config.restart_hp_rate > 100)
			battle_config.restart_hp_rate = 100;
		if(battle_config.restart_sp_rate < 0)
			battle_config.restart_sp_rate = 0;
		else if(battle_config.restart_sp_rate > 100)
			battle_config.restart_sp_rate = 100;
		if(battle_config.natural_healhp_interval < NATURAL_HEAL_INTERVAL)
			battle_config.natural_healhp_interval = NATURAL_HEAL_INTERVAL;
		if(battle_config.natural_healsp_interval < NATURAL_HEAL_INTERVAL)
			battle_config.natural_healsp_interval = NATURAL_HEAL_INTERVAL;
		if(battle_config.natural_heal_skill_interval < NATURAL_HEAL_INTERVAL)
			battle_config.natural_heal_skill_interval = NATURAL_HEAL_INTERVAL;
		if(battle_config.natural_heal_weight_rate < 50)
			battle_config.natural_heal_weight_rate = 50;
		else if(battle_config.natural_heal_weight_rate > 101)
			battle_config.natural_heal_weight_rate = 101;
		if(battle_config.hp_rate < 0)
			battle_config.hp_rate = 1;
		if(battle_config.sp_rate < 0)
			battle_config.sp_rate = 1;
		if(battle_config.max_hp > 1000000)
			battle_config.max_hp = 1000000;
		else if(battle_config.max_hp < 100)
			battle_config.max_hp = 100;
		if(battle_config.max_sp > 1000000)
			battle_config.max_sp = 1000000;
		else if(battle_config.max_sp < 100)
			battle_config.max_sp = 100;

		if(battle_config.max_parameter < 10)
			battle_config.max_parameter = 10;
		else if(battle_config.max_parameter > 10000)
			battle_config.max_parameter = 10000;
		if(battle_config.max_parameter_str < 1)
			battle_config.max_parameter_str = 1;
		else if(battle_config.max_parameter_str > battle_config.max_parameter)
			battle_config.max_parameter_str = battle_config.max_parameter;
		if(battle_config.max_parameter_agi < 1)
			battle_config.max_parameter_agi = 1;
		else if(battle_config.max_parameter_agi > battle_config.max_parameter)
			battle_config.max_parameter_agi = battle_config.max_parameter;
		if(battle_config.max_parameter_vit < 1)
			battle_config.max_parameter_vit = 1;
		else if(battle_config.max_parameter_vit > battle_config.max_parameter)
			battle_config.max_parameter_vit = battle_config.max_parameter;
		if(battle_config.max_parameter_int < 1)
			battle_config.max_parameter_int = 1;
		else if(battle_config.max_parameter_int > battle_config.max_parameter)
			battle_config.max_parameter_int = battle_config.max_parameter;
		if(battle_config.max_parameter_dex < 1)
			battle_config.max_parameter_dex = 1;
		else if(battle_config.max_parameter_dex > battle_config.max_parameter)
			battle_config.max_parameter_dex = battle_config.max_parameter;
		if(battle_config.max_parameter_luk < 1)
			battle_config.max_parameter_luk = 1;
		else if(battle_config.max_parameter_luk > battle_config.max_parameter)
			battle_config.max_parameter_luk = battle_config.max_parameter;

		if(battle_config.max_cart_weight > 1000000)
			battle_config.max_cart_weight = 1000000;
		else if(battle_config.max_cart_weight < 100)
			battle_config.max_cart_weight = 100;
		battle_config.max_cart_weight *= 10;

		if(battle_config.agi_penalty_count < 2)
			battle_config.agi_penalty_count = 2;
		if(battle_config.vit_penalty_count < 2)
			battle_config.vit_penalty_count = 2;

		if(battle_config.guild_exp_limit > 99)
			battle_config.guild_exp_limit = 99;
		if(battle_config.guild_exp_limit < 0)
			battle_config.guild_exp_limit = 0;
		if(battle_config.pet_weight < 0)
			battle_config.pet_weight = 0;

		if(battle_config.castle_defense_rate < 0)
			battle_config.castle_defense_rate = 0;
		if(battle_config.castle_defense_rate > 100)
			battle_config.castle_defense_rate = 100;

		if(battle_config.next_exp_limit < 0)
			battle_config.next_exp_limit = 0;

		if(battle_config.card_drop_rate < 0)
			battle_config.card_drop_rate = 0;
		if(battle_config.equip_drop_rate < 0)
			battle_config.equip_drop_rate = 0;
		if(battle_config.consume_drop_rate < 0)
			battle_config.consume_drop_rate = 0;
		if(battle_config.refine_drop_rate < 0)
			battle_config.refine_drop_rate = 0;
		if(battle_config.etc_drop_rate < 0)
			battle_config.etc_drop_rate = 0;

		if(battle_config.potion_drop_rate < 0)
			battle_config.potion_drop_rate = 0;
		if(battle_config.arrow_drop_rate < 0)
			battle_config.arrow_drop_rate = 0;
		if(battle_config.petequip_drop_rate < 0)
			battle_config.petequip_drop_rate = 0;
		if(battle_config.weapon_drop_rate < 0)
			battle_config.weapon_drop_rate = 0;
		if(battle_config.other_drop_rate < 0)
			battle_config.other_drop_rate = 0;

		if(battle_config.monster_skill_over < 0)
			battle_config.monster_skill_over = 0;
		if (battle_config.finding_ore_drop_rate < 0)
			battle_config.finding_ore_drop_rate = 0;
		else if (battle_config.finding_ore_drop_rate > 10000)
			battle_config.finding_ore_drop_rate = 10000;

		if(battle_config.max_marionette_str < 1)
			battle_config.max_marionette_str = battle_config.max_parameter;
		if(battle_config.max_marionette_agi < 1)
			battle_config.max_marionette_agi = battle_config.max_parameter;
		if(battle_config.max_marionette_vit < 1)
			battle_config.max_marionette_vit = battle_config.max_parameter;
		if(battle_config.max_marionette_int < 1)
			battle_config.max_marionette_int = battle_config.max_parameter;
		if(battle_config.max_marionette_dex < 1)
			battle_config.max_marionette_dex = battle_config.max_parameter;
		if(battle_config.max_marionette_luk < 1)
			battle_config.max_marionette_luk = battle_config.max_parameter;

		if(battle_config.max_marionette_pk_str < 1)
			battle_config.max_marionette_pk_str = battle_config.max_parameter;
		if(battle_config.max_marionette_pk_agi < 1)
			battle_config.max_marionette_pk_agi = battle_config.max_parameter;
		if(battle_config.max_marionette_pk_vit < 1)
			battle_config.max_marionette_pk_vit = battle_config.max_parameter;
		if(battle_config.max_marionette_pk_int < 1)
			battle_config.max_marionette_pk_int = battle_config.max_parameter;
		if(battle_config.max_marionette_pk_dex < 1)
			battle_config.max_marionette_pk_dex = battle_config.max_parameter;
		if(battle_config.max_marionette_pk_luk < 1)
			battle_config.max_marionette_pk_luk = battle_config.max_parameter;

		if(battle_config.max_marionette_pvp_str < 1)
			battle_config.max_marionette_pvp_str = battle_config.max_parameter;
		if(battle_config.max_marionette_pvp_agi < 1)
			battle_config.max_marionette_pvp_agi = battle_config.max_parameter;
		if(battle_config.max_marionette_pvp_vit < 1)
			battle_config.max_marionette_pvp_vit = battle_config.max_parameter;
		if(battle_config.max_marionette_pvp_int < 1)
			battle_config.max_marionette_pvp_int = battle_config.max_parameter;
		if(battle_config.max_marionette_pvp_dex < 1)
			battle_config.max_marionette_pvp_dex = battle_config.max_parameter;
		if(battle_config.max_marionette_pvp_luk < 1)
			battle_config.max_marionette_pvp_luk = battle_config.max_parameter;

		if(battle_config.max_marionette_gvg_str < 1)
			battle_config.max_marionette_gvg_str = battle_config.max_parameter;
		if(battle_config.max_marionette_gvg_agi < 1)
			battle_config.max_marionette_gvg_agi = battle_config.max_parameter;
		if(battle_config.max_marionette_gvg_vit < 1)
			battle_config.max_marionette_gvg_vit = battle_config.max_parameter;
		if(battle_config.max_marionette_gvg_int < 1)
			battle_config.max_marionette_gvg_int = battle_config.max_parameter;
		if(battle_config.max_marionette_gvg_dex < 1)
			battle_config.max_marionette_gvg_dex = battle_config.max_parameter;
		if(battle_config.max_marionette_gvg_luk < 1)
			battle_config.max_marionette_gvg_luk = battle_config.max_parameter;
		if(battle_config.tax_rate < 0)
			battle_config.tax_rate = 0;
		if(battle_config.tax_rate > 100)
			battle_config.tax_rate = 100;
		if(battle_config.steal_rate < 0)
			battle_config.steal_rate = 0;
		if(battle_config.pk_murderer_point < 0)
			battle_config.pk_murderer_point = 0;
	}

	return 0;
}

/*==========================================
 * 初期化
 *------------------------------------------
 */
int do_init_battle(void)
{
	add_timer_func_list(battle_delay_damage_sub);

	return 0;
}

/*==========================================
 * 終了
 *------------------------------------------
 */
int do_final_battle(void)
{
	// nothing to do
	return 0;
}
