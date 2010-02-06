/* MegaZeux
 *
 * Copyright (C) 2004 Gilead Kutnick <exophase@adelphia.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef __BOARD_H
#define __BOARD_H

#include "compat.h"

__M_BEGIN_DECLS

#include "const.h"
#include "board_struct.h"
#include "world_struct.h"

#define MAX_BOARDS 250

void replace_current_board(World *mzx_world, char *name);
Board *load_board_allocate(FILE *fp, int savegame);
Board *load_board_allocate_direct(FILE *fp, int savegame);
void load_board(Board *cur_board, FILE *fp, int savegame);
void load_board_direct(Board *cur_board, FILE *fp, int savegame);
Board *create_blank_board();
void save_board_file(Board *cur_board, char *name);
int save_board(Board *cur_board, FILE *fp, int savegame);
void load_RLE2_plane(char *plane, FILE *fp, int size);
void save_RLE2_plane(char *plane, FILE *fp, int size);
void clear_board(Board *cur_board);
int find_board(World *mzx_world, char *name);
void change_board_size(Board *src_board, int new_width, int new_height);

__M_END_DECLS

#endif // __BOARD_H
