/* ************************************************************************
*   File: olc.global.c                                    EmpireMUD 2.0b3 *
*  Usage: OLC for globals                                                 *
*                                                                         *
*  EmpireMUD code base by Paul Clarke, (C) 2000-2015                      *
*  All rights reserved.  See license.doc for complete information.        *
*                                                                         *
*  EmpireMUD based upon CircleMUD 3.0, bpl 17, by Jeremy Elson.           *
*  CircleMUD (C) 1993, 94 by the Trustees of the Johns Hopkins University *
*  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
************************************************************************ */
#include "conf.h"
#include "sysdep.h"

#include "structs.h"
#include "utils.h"
#include "interpreter.h"
#include "db.h"
#include "comm.h"
#include "olc.h"
#include "skills.h"
#include "handler.h"

/**
* Contents:
*   Helpers
*   Displays
*   Edit Modules
*/

// external consts
extern const char *action_bits[];
extern const char *global_flags[];
extern const char *global_types[];
extern const char *interact_types[];
extern const byte interact_vnum_types[NUM_INTERACTS];

// external funcs
void sort_interactions(struct interaction_item **list);


 //////////////////////////////////////////////////////////////////////////////
//// HELPERS /////////////////////////////////////////////////////////////////

/**
* Checks for common global table problems and reports them to ch.
*
* @param struct global_data *global The item to audit.
* @param char_data *ch The person to report to.
* @return bool TRUE if any problems were reported; FALSE if all good.
*/
bool audit_global(struct global_data *glb, char_data *ch) {
	struct interaction_item *interact;
	bool problem = FALSE;

	if (IS_SET(GET_GLOBAL_FLAGS(glb), GLB_FLAG_IN_DEVELOPMENT)) {
		olc_audit_msg(ch, GET_GLOBAL_VNUM(glb), "IN-DEVELOPMENT");
		problem = TRUE;
	}
	if (GET_GLOBAL_MIN_LEVEL(glb) > GET_GLOBAL_MAX_LEVEL(glb)) {
		olc_audit_msg(ch, GET_GLOBAL_VNUM(glb), "Min level is greater than max level");
		problem = TRUE;
	}
	if ((GET_GLOBAL_TYPE_FLAGS(glb) & GET_GLOBAL_TYPE_EXCLUDE(glb)) != 0) {
		olc_audit_msg(ch, GET_GLOBAL_VNUM(glb), "Same flags in required and excluded set");
		problem = TRUE;
	}
	
	for (interact = GET_GLOBAL_INTERACTIONS(glb); interact; interact = interact->next) {
		if (GET_GLOBAL_TYPE(glb) == GLOBAL_MOB_INTERACTIONS) {
			if (interact->type != INTERACT_SHEAR && interact->type != INTERACT_LOOT && interact->type != INTERACT_PICKPOCKET) {
				olc_audit_msg(ch, GET_GLOBAL_VNUM(glb), "Unsupported interaction type");
				problem = TRUE;
			}
		}
	}
		
	return problem;
}


/**
* Creates a new global entry.
* 
* @param any_vnum vnum The number to create.
* @return struct global_data* The new global's prototype.
*/
struct global_data *create_global_table_entry(any_vnum vnum) {
	void add_global_to_table(struct global_data *glb);
	
	struct global_data *glb;
	
	// sanity
	if (global_proto(vnum)) {
		log("SYSERR: Attempting to insert global at existing vnum %d", vnum);
		return global_proto(vnum);
	}
	
	CREATE(glb, struct global_data, 1);
	GET_GLOBAL_VNUM(glb) = vnum;
	add_global_to_table(glb);

	// save index and global file now
	save_index(DB_BOOT_GLB);
	save_library_file_for_vnum(DB_BOOT_GLB, vnum);

	return glb;
}


/**
* For the .list command.
*
* @param struct global_data *glb The thing to list.
* @param bool detail If TRUE, provide additional details
* @return char* The line to show (without a CRLF).
*/
char *list_one_global(struct global_data *glb, bool detail) {
	extern const char *action_bits[];
	
	static char output[MAX_STRING_LENGTH];
	char flags[MAX_STRING_LENGTH];

	switch (GET_GLOBAL_TYPE(glb)) {
		case GLOBAL_MOB_INTERACTIONS: {
			if (detail) {
				sprintbit(GET_GLOBAL_TYPE_FLAGS(glb), action_bits, flags, TRUE);
				snprintf(output, sizeof(output), "[%5d] %s (%s) %s", GET_GLOBAL_VNUM(glb), GET_GLOBAL_NAME(glb), level_range_string(GET_GLOBAL_MIN_LEVEL(glb), GET_GLOBAL_MAX_LEVEL(glb), 0), flags);
			}
			else {
				snprintf(output, sizeof(output), "[%5d] %s", GET_GLOBAL_VNUM(glb), GET_GLOBAL_NAME(glb));
			}
			break;
		}
		default: {
			if (detail) {
				snprintf(output, sizeof(output), "[%5d] %s", GET_GLOBAL_VNUM(glb), GET_GLOBAL_NAME(glb));
			}
			else {
				snprintf(output, sizeof(output), "[%5d] %s", GET_GLOBAL_VNUM(glb), GET_GLOBAL_NAME(glb));
			}
			break;
		}
	}
		
	return output;
}


/**
* WARNING: This function actually deletes a global.
*
* @param char_data *ch The person doing the deleting.
* @param any_vnum vnum The vnum to delete.
*/
void olc_delete_global(char_data *ch, any_vnum vnum) {
	void remove_global_from_table(struct global_data *glb);
	
	struct global_data *glb;
	
	if (!(glb = global_proto(vnum))) {
		msg_to_char(ch, "There is no such global %d.\r\n", vnum);
		return;
	}
	
	if (HASH_COUNT(globals_table) <= 1) {
		msg_to_char(ch, "You can't delete the last global.\r\n");
		return;
	}

	// remove it from the hash table first
	remove_global_from_table(glb);

	// save index and global file now
	save_index(DB_BOOT_GLB);
	save_library_file_for_vnum(DB_BOOT_GLB, vnum);
	
	syslog(SYS_OLC, GET_INVIS_LEV(ch), TRUE, "OLC: %s has deleted global %d", GET_NAME(ch), vnum);
	msg_to_char(ch, "Global %d deleted.\r\n", vnum);
	
	free_global(glb);
}


/**
* Searches for all uses of a global and displays them.
*
* @param char_data *ch The player.
* @param any_vnum vnum The global vnum.
*/
void olc_search_global(char_data *ch, any_vnum vnum) {
	char buf[MAX_STRING_LENGTH];
	struct global_data *glb = global_proto(vnum);
	int size, found;
	
	if (!glb) {
		msg_to_char(ch, "There is no global %d.\r\n", vnum);
		return;
	}
	
	found = 0;
	size = snprintf(buf, sizeof(buf), "Occurrences of global %d (%s):\r\n", vnum, GET_GLOBAL_NAME(glb));
	
	// globals are not actually used anywhere else
	
	if (found > 0) {
		size += snprintf(buf + size, sizeof(buf) - size, "%d location%s shown\r\n", found, PLURAL(found));
	}
	else {
		size += snprintf(buf + size, sizeof(buf) - size, " none\r\n");
	}
	
	page_string(ch->desc, buf, TRUE);
}


/**
* Function to save a player's changes to a global (or a new one).
*
* @param descriptor_data *desc The descriptor who is saving.
*/
void save_olc_global(descriptor_data *desc) {	
	struct global_data *proto, *glb = GET_OLC_GLOBAL(desc);
	any_vnum vnum = GET_OLC_VNUM(desc);
	struct interaction_item *interact;
	UT_hash_handle hh;

	// have a place to save it?
	if (!(proto = global_proto(vnum))) {
		proto = create_global_table_entry(vnum);
	}
	
	// free prototype strings and pointers
	if (GET_GLOBAL_NAME(proto)) {
		free(GET_GLOBAL_NAME(proto));
	}
	while ((interact = GET_GLOBAL_INTERACTIONS(proto))) {
		GET_GLOBAL_INTERACTIONS(proto) = interact->next;
		free(interact);
	}
	
	// sanity
	if (!GET_GLOBAL_NAME(glb) || !*GET_GLOBAL_NAME(glb)) {
		if (GET_GLOBAL_NAME(glb)) {
			free(GET_GLOBAL_NAME(glb));
		}
		GET_GLOBAL_NAME(glb) = str_dup("Unnamed Global");
	}

	// save data back over the proto-type
	hh = proto->hh;	// save old hash handle
	*proto = *glb;	// copy over all data
	proto->vnum = vnum;	// ensure correct vnum
	proto->hh = hh;	// restore old hash handle
		
	// and save to file
	save_library_file_for_vnum(DB_BOOT_GLB, vnum);
}


/**
* Creates a copy of a global, or clears a new one, for editing.
* 
* @param struct global_data *input The global to copy, or NULL to make a new one.
* @return struct global_data* The copied global.
*/
struct global_data *setup_olc_global(struct global_data *input) {
	struct global_data *new;
	
	CREATE(new, struct global_data, 1);
	
	if (input) {
		// copy normal data
		*new = *input;

		// copy things that are pointers
		GET_GLOBAL_NAME(new) = GET_GLOBAL_NAME(input) ? str_dup(GET_GLOBAL_NAME(input)) : NULL;
		
		// copy interactions
		GET_GLOBAL_INTERACTIONS(new) = copy_interaction_list(GET_GLOBAL_INTERACTIONS(input));
	}
	else {
		// brand new: some defaults
		GET_GLOBAL_NAME(new) = str_dup("Unnamed Global");
		GET_GLOBAL_FLAGS(new) = GLB_FLAG_IN_DEVELOPMENT;
	}
	
	// done
	return new;	
}


 //////////////////////////////////////////////////////////////////////////////
//// DISPLAYS ////////////////////////////////////////////////////////////////

/**
* This is the main recipe display for global OLC. It displays the user's
* currently-edited global.
*
* @param char_data *ch The person who is editing a global and will see its display.
*/
void olc_show_global(char_data *ch) {
	void get_icons_display(struct icon_data *list, char *save_buffer);
	void get_interaction_display(struct interaction_item *list, char *save_buffer);
	
	struct global_data *glb = GET_OLC_GLOBAL(ch->desc);
	char buf[MAX_STRING_LENGTH], lbuf[MAX_STRING_LENGTH];
	
	if (!glb) {
		return;
	}
	
	*buf = '\0';
	
	sprintf(buf + strlen(buf), "[&c%d&0] &c%s&0\r\n", GET_OLC_VNUM(ch->desc), !global_proto(GET_GLOBAL_VNUM(glb)) ? "new global" : GET_GLOBAL_NAME(global_proto(GET_GLOBAL_VNUM(glb))));
	sprintf(buf + strlen(buf), "<&yname&0> %s\r\n", NULLSAFE(GET_GLOBAL_NAME(glb)));
	
	sprintf(buf + strlen(buf), "<&ytype&0> %s\r\n", global_types[GET_GLOBAL_TYPE(glb)]);

	sprintbit(GET_GLOBAL_FLAGS(glb), global_flags, lbuf, TRUE);
	sprintf(buf + strlen(buf), "<&yflags&0> %s\r\n", lbuf);
	
	if (GET_GLOBAL_MIN_LEVEL(glb) == 0) {
		sprintf(buf + strlen(buf), "<&yminlevel&0> none\r\n");
	}
	else {
		sprintf(buf + strlen(buf), "<&yminlevel&0> %d\r\n", GET_GLOBAL_MIN_LEVEL(glb));
	}
	
	if (GET_GLOBAL_MAX_LEVEL(glb) == 0) {
		sprintf(buf + strlen(buf), "<&ymaxlevel&0> none\r\n");
	}
	else {
		sprintf(buf + strlen(buf), "<&ymaxlevel&0> %d\r\n", GET_GLOBAL_MAX_LEVEL(glb));
	}

	// type-based data
	switch (GET_GLOBAL_TYPE(glb)) {
		case GLOBAL_MOB_INTERACTIONS: {
			sprintbit(GET_GLOBAL_TYPE_FLAGS(glb), action_bits, lbuf, TRUE);
			sprintf(buf + strlen(buf), "<&ymobflags&0> %s\r\n", lbuf);
			sprintbit(GET_GLOBAL_TYPE_EXCLUDE(glb), action_bits, lbuf, TRUE);
			sprintf(buf + strlen(buf), "<&ymobexclude&0> %s\r\n", lbuf);
			break;
		}
	}
	
	sprintf(buf + strlen(buf), "Interactions: <&yinteraction&0>\r\n");
	get_interaction_display(GET_GLOBAL_INTERACTIONS(glb), buf1);
	strcat(buf, buf1);
		
	page_string(ch->desc, buf, TRUE);
}


 //////////////////////////////////////////////////////////////////////////////
//// EDIT MODULES ////////////////////////////////////////////////////////////


OLC_MODULE(gedit_flags) {
	struct global_data *glb = GET_OLC_GLOBAL(ch->desc);
	bool had_indev = IS_SET(GET_GLOBAL_FLAGS(glb), GLB_FLAG_IN_DEVELOPMENT) ? TRUE : FALSE;
	
	GET_GLOBAL_FLAGS(glb) = olc_process_flag(ch, argument, "global", "flags", global_flags, GET_GLOBAL_FLAGS(glb));
	
	// validate removal of IN-DEVELOPMENT
	if (had_indev && !IS_SET(GET_GLOBAL_FLAGS(glb), GLB_FLAG_IN_DEVELOPMENT) && GET_ACCESS_LEVEL(ch) < LVL_UNRESTRICTED_BUILDER && !OLC_FLAGGED(ch, OLC_FLAG_CLEAR_IN_DEV)) {
		msg_to_char(ch, "You don't have permission to remove the IN-DEVELOPMENT flag.\r\n");
		SET_BIT(GET_GLOBAL_FLAGS(glb), GLB_FLAG_IN_DEVELOPMENT);
	}
}


OLC_MODULE(gedit_interaction) {
	struct global_data *glb = GET_OLC_GLOBAL(ch->desc);
	olc_process_interactions(ch, argument, &GET_GLOBAL_INTERACTIONS(glb), TYPE_MOB);
}


OLC_MODULE(gedit_maxlevel) {
	struct global_data *glb = GET_OLC_GLOBAL(ch->desc);
	GET_GLOBAL_MAX_LEVEL(glb) = olc_process_number(ch, argument, "maximum level", "maxlevel", 0, MAX_INT, GET_GLOBAL_MAX_LEVEL(glb));
}


OLC_MODULE(gedit_minlevel) {
	struct global_data *glb = GET_OLC_GLOBAL(ch->desc);
	GET_GLOBAL_MIN_LEVEL(glb) = olc_process_number(ch, argument, "minimum level", "minlevel", 0, MAX_INT, GET_GLOBAL_MIN_LEVEL(glb));
}


OLC_MODULE(gedit_mobexclude) {
	struct global_data *glb = GET_OLC_GLOBAL(ch->desc);
	
	if (GET_GLOBAL_TYPE(glb) != GLOBAL_MOB_INTERACTIONS) {
		msg_to_char(ch, "You can't set mobexclude on this type.\r\n");
	}
	else {
		GET_GLOBAL_TYPE_EXCLUDE(glb) = olc_process_flag(ch, argument, "mob", "mobexclude", action_bits, GET_GLOBAL_TYPE_EXCLUDE(glb));
	}
}


OLC_MODULE(gedit_mobflags) {
	struct global_data *glb = GET_OLC_GLOBAL(ch->desc);
	
	if (GET_GLOBAL_TYPE(glb) != GLOBAL_MOB_INTERACTIONS) {
		msg_to_char(ch, "You can't set mobflags on this type.\r\n");
	}
	else {
		GET_GLOBAL_TYPE_FLAGS(glb) = olc_process_flag(ch, argument, "mob", "mobflags", action_bits, GET_GLOBAL_TYPE_FLAGS(glb));
	}
}


OLC_MODULE(gedit_name) {
	struct global_data *glb = GET_OLC_GLOBAL(ch->desc);
	olc_process_string(ch, argument, "name", &GET_GLOBAL_NAME(glb));
}


OLC_MODULE(gedit_type) {
	struct global_data *glb = GET_OLC_GLOBAL(ch->desc);
	GET_GLOBAL_TYPE(glb) = olc_process_type(ch, argument, "type", "type", global_types, GET_GLOBAL_TYPE(glb));
}
