/* MegaZeux
 *
 * Copyright (C) 1996 Greg Janson
 * Copyright (C) 1999 Charles Goetzman
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

#include "caption.h"
#include "core.h"
#include "counter.h"
#include "event.h"
#include "game.h"
#include "game_ops.h"
#include "game_player.h"
#include "game_update.h"
#include "graphics.h"
#include "idarray.h"
#include "idput.h"
#include "scrdisp.h" // strlencolor
#include "sprite.h"
#include "robot.h"
#include "world.h"
#include "world_struct.h"

#include "audio/audio.h"
#include "audio/sfx.h"

// Number of cycles to make player idle before repeating a
// directional move
#define REPEAT_WAIT 2

// Length of cooldown between player shots, including current frame.
#define MAX_PLAYER_SHOT_COOLDOWN 2

// Updates game variables
// Slowed = 1 to not update lazwall or time
// due to slowtime or freezetime

static void update_variables(struct world *mzx_world)
{
  struct board *src_board = mzx_world->current_board;
  int blind_dur = mzx_world->blind_dur;
  int firewalker_dur = mzx_world->firewalker_dur;
  int freeze_time_dur = mzx_world->freeze_time_dur;
  int slow_time_dur = mzx_world->slow_time_dur;
  int wind_dur = mzx_world->wind_dur;
  int b_mesg_timer = src_board->b_mesg_timer;
  int invinco;
  int lazwall_start = src_board->lazwall_start;
  static boolean scroll_color_flip_flop;

  // No game state change has happened (yet)
  mzx_world->change_game_state = 0;

  // Determine whether the current cycle is frozen
  if(mzx_world->freeze_time_dur)
  {
    mzx_world->current_cycle_frozen = true;
  }
  else
  if(mzx_world->slow_time_dur)
  {
    mzx_world->current_cycle_frozen = !mzx_world->current_cycle_frozen;
  }
  else
  {
    mzx_world->current_cycle_frozen = false;
  }

  // Toggle board cycle flip flop
  if(!mzx_world->current_cycle_frozen)
    mzx_world->current_cycle_odd = !mzx_world->current_cycle_odd;

  // Also toggle the scroll color flip flop
  scroll_color_flip_flop = !scroll_color_flip_flop;

  // Change scroll color
  if(!scroll_color_flip_flop)
  {
    scroll_color++;

    if(scroll_color > 15)
      scroll_color = 7;
  }

  // Decrease time limit (unless this is a frozen cycle)
  if(!mzx_world->current_cycle_odd && !mzx_world->current_cycle_frozen)
  {
    int time_left = get_counter(mzx_world, "TIME", 0);
    if(time_left > 0)
    {
      if(time_left == 1)
      {
        // Out of time
        dec_counter(mzx_world, "HEALTH", 10, 0);
        set_mesg(mzx_world, "Out of time!");
        play_sfx(mzx_world, SFX_OUT_OF_TIME);
        // Reset time
        set_counter(mzx_world, "TIME", src_board->time_limit, 0);
      }
      else
      {
        dec_counter(mzx_world, "TIME", 1, 0);
      }
    }
  }

  // Decrease effect durations
  if(blind_dur > 0)
    mzx_world->blind_dur = blind_dur - 1;

  if(firewalker_dur > 0)
    mzx_world->firewalker_dur = firewalker_dur - 1;

  if(freeze_time_dur > 0)
    mzx_world->freeze_time_dur = freeze_time_dur - 1;

  if(slow_time_dur > 0)
    mzx_world->slow_time_dur = slow_time_dur - 1;

  if(wind_dur > 0)
    mzx_world->wind_dur = wind_dur - 1;

  // Decrease message timer
  if(b_mesg_timer > 0)
    src_board->b_mesg_timer = b_mesg_timer - 1;

  // Invinco
  invinco = get_counter(mzx_world, "INVINCO", 0);
  if(invinco > 0)
  {
    if(invinco == 1)
    {
      // Just finished-
      set_counter(mzx_world, "INVINCO", 0, 0);
    }
    else
    {
      // Decrease
      dec_counter(mzx_world, "INVINCO", 1, 0);
      play_sfx(mzx_world, SFX_INVINCO_BEAT);
      id_chars[player_color] = Random(256);
    }
  }

  // Lazerwall start- cycle 0 to 7 then -7 to -1
  if(!mzx_world->current_cycle_frozen)
  {
    if(((signed char)lazwall_start) < 7)
      src_board->lazwall_start = lazwall_start + 1;
    else
      src_board->lazwall_start = -7;
  }
}

/**
 * Update the current mod's volume if a mod fade is active.
 */

static void update_mod_volume(struct world *mzx_world)
{
  struct board *cur_board = mzx_world->current_board;
  int volume_target = cur_board->volume_target;
  int volume_inc = cur_board->volume_inc;
  int volume = cur_board->volume;

  if(volume_inc)
  {
    volume += volume_inc;

    if(volume_inc < 0)
    {
      if(volume <= volume_target)
      {
        volume = volume_target;
        cur_board->volume_inc = 0;
      }
    }
    else
    {
      if(volume >= volume_target)
      {
        volume = volume_target;
        cur_board->volume_inc = 0;
      }
    }
    cur_board->volume = volume;
    audio_set_module_volume(volume);
  }
}

/**
 * Handle all player effects caused by things beneath the player.
 * May result in a context change.
 */

static void update_player_under(struct world *mzx_world)
{
  struct board *cur_board = mzx_world->current_board;
  int player_x = mzx_world->player_x;
  int player_y = mzx_world->player_y;
  int offset = xy_to_offset(cur_board, player_x, player_y);
  enum thing under_id = (enum thing)cur_board->level_under_id[offset];

  // NOTE: flags[under_id] & A_AFFECT_IF_STOOD
  // All types handled here use this flag, however, checking it here isn't
  // really necessary anymore. However, this flag might be useful in the
  // future if user-defined types are implemented.

  switch(under_id)
  {
    case N_WATER:
    {
      move_player(mzx_world, 0);
      return;
    }

    case S_WATER:
    {
      move_player(mzx_world, 1);
      return;
    }

    case E_WATER:
    {
      move_player(mzx_world, 2);
      return;
    }

    case W_WATER:
    {
      move_player(mzx_world, 3);
      return;
    }

    case ICE:
    {
      int player_last_dir = cur_board->player_last_dir;
      if(player_last_dir & 0x0F)
      {
        move_player(mzx_world, (player_last_dir & 0x0F) - 1);

        // FIXME has_context_changed
        if(!mzx_world->player_moved)
          cur_board->player_last_dir = player_last_dir & 0xF0;
      }
      return;
    }

    case LAVA:
    {
      if(mzx_world->firewalker_dur > 0)
        break;

      play_sfx(mzx_world, SFX_LAVA);
      set_mesg(mzx_world, "Augh!");
      dec_counter(mzx_world, "HEALTH", id_dmg[26], 0);
      return;
    }

    case FIRE:
    {
      if(mzx_world->firewalker_dur > 0)
        break;

      play_sfx(mzx_world, SFX_FIRE_HURT);
      set_mesg(mzx_world, "Ouch!");
      dec_counter(mzx_world, "HEALTH", id_dmg[63], 0);
      return;
    }

    default:
    {
      return;
    }
  }
}

/**
 * Apply wind to the player if wind is currently active.
 * May result in a context change.
 */

static void update_player_wind(struct world *mzx_world)
{
  struct board *cur_board = mzx_world->current_board;

  if(mzx_world->wind_dur > 0)
  {
    int wind_dir = Random(9);
    if(wind_dir < 4)
    {
      // No wind this turn if above 3
      cur_board->player_last_dir =
       (cur_board->player_last_dir & 0xF0) + wind_dir;
      move_player(mzx_world, wind_dir);
    }
  }
}

/**
 * Handle all user player input.
 * May result in a context change.
 */

static void update_player_input(struct world *mzx_world)
{
  struct board *cur_board = mzx_world->current_board;
  int space_pressed = get_key_status(keycode_internal_wrt_numlock, IKEY_SPACE);
  int up_pressed = get_key_status(keycode_internal_wrt_numlock, IKEY_UP);
  int down_pressed = get_key_status(keycode_internal_wrt_numlock, IKEY_DOWN);
  int right_pressed = get_key_status(keycode_internal_wrt_numlock, IKEY_RIGHT);
  int left_pressed = get_key_status(keycode_internal_wrt_numlock, IKEY_LEFT);
  int del_pressed = get_key_status(keycode_internal_wrt_numlock, IKEY_DELETE);

  // Shoot
  if(space_pressed && mzx_world->bi_shoot_status)
  {
    if(!mzx_world->player_shoot_cooldown && !cur_board->player_attack_locked)
    {
      int move_dir = -1;

      if(up_pressed)
        move_dir = 0;
      else

      if(down_pressed)
        move_dir = 1;
      else

      if(right_pressed)
        move_dir = 2;
      else

      if(left_pressed)
        move_dir = 3;

      if(move_dir != -1)
      {
        if(!cur_board->can_shoot)
        {
          set_mesg(mzx_world, "Can't shoot here!");
        }
        else

        if(!get_counter(mzx_world, "AMMO", 0))
        {
          set_mesg(mzx_world, "You are out of ammo!");
          play_sfx(mzx_world, SFX_OUT_OF_AMMO);
        }

        else
        {
          dec_counter(mzx_world, "AMMO", 1, 0);
          play_sfx(mzx_world, SFX_SHOOT);
          shoot(mzx_world, mzx_world->player_x, mzx_world->player_y,
           move_dir, PLAYER_BULLET);
          mzx_world->player_shoot_cooldown = MAX_PLAYER_SHOT_COOLDOWN;
          cur_board->player_last_dir =
           (cur_board->player_last_dir & 0x0F) | (move_dir << 4);
        }
      }
    }
  }
  else

  // Player movement
  if(up_pressed && !cur_board->player_ns_locked)
  {
    int key_up_delay = mzx_world->key_up_delay;
    if((key_up_delay == 0) || (key_up_delay > REPEAT_WAIT))
    {
      move_player(mzx_world, 0);
      cur_board->player_last_dir = (cur_board->player_last_dir & 0x0F);
    }
    if(key_up_delay <= REPEAT_WAIT)
      mzx_world->key_up_delay = key_up_delay + 1;
  }
  else

  if(down_pressed && !cur_board->player_ns_locked)
  {
    int key_down_delay = mzx_world->key_down_delay;
    if((key_down_delay == 0) || (key_down_delay > REPEAT_WAIT))
    {
      move_player(mzx_world, 1);
      cur_board->player_last_dir =
       (cur_board->player_last_dir & 0x0F) + 0x10;
    }
    if(key_down_delay <= REPEAT_WAIT)
      mzx_world->key_down_delay = key_down_delay + 1;
  }
  else

  if(right_pressed && !cur_board->player_ew_locked)
  {
    int key_right_delay = mzx_world->key_right_delay;
    if((key_right_delay == 0) || (key_right_delay > REPEAT_WAIT))
    {
      move_player(mzx_world, 2);
      cur_board->player_last_dir =
       (cur_board->player_last_dir & 0x0F) + 0x20;
    }
    if(key_right_delay <= REPEAT_WAIT)
      mzx_world->key_right_delay = key_right_delay + 1;
  }
  else

  if(left_pressed && !cur_board->player_ew_locked)
  {
    int key_left_delay = mzx_world->key_left_delay;
    if((key_left_delay == 0) || (key_left_delay > REPEAT_WAIT))
    {
      move_player(mzx_world, 3);
      cur_board->player_last_dir =
       (cur_board->player_last_dir & 0x0F) + 0x30;
    }
    if(key_left_delay <= REPEAT_WAIT)
      mzx_world->key_left_delay = key_left_delay + 1;
  }

  else
  {
    mzx_world->key_up_delay = 0;
    mzx_world->key_down_delay = 0;
    mzx_world->key_right_delay = 0;
    mzx_world->key_left_delay = 0;
  }

  // Bomb
  if(del_pressed && !cur_board->player_attack_locked)
  {
    int offset =
     xy_to_offset(cur_board, mzx_world->player_x, mzx_world->player_y);
    enum thing under_id = (enum thing)cur_board->level_under_id[offset];
    char under_param = cur_board->level_under_param[offset];
    char under_color = cur_board->level_under_color[offset];

    if(under_id != LIT_BOMB)
    {
      if(!cur_board->can_bomb)
      {
        set_mesg(mzx_world, "Can't bomb here!");
      }
      else

      if((mzx_world->bomb_type == 0) && !get_counter(mzx_world, "LOBOMBS", 0))
      {
        set_mesg(mzx_world, "You are out of low strength bombs!");
        play_sfx(mzx_world, SFX_OUT_OF_BOMBS);
      }
      else

      if((mzx_world->bomb_type == 1) && !get_counter(mzx_world, "HIBOMBS", 0))
      {
        set_mesg(mzx_world, "You are out of high strength bombs!");
        play_sfx(mzx_world, SFX_OUT_OF_BOMBS);
      }
      else

      if(!(flags[under_id] & A_ENTRANCE))
      {
        // Bomb!
        mzx_world->under_player_id = under_id;
        mzx_world->under_player_color = under_color;
        mzx_world->under_player_param = under_param;
        cur_board->level_under_id[offset] = LIT_BOMB;
        cur_board->level_under_color[offset] = 8;
        cur_board->level_under_param[offset] = mzx_world->bomb_type << 7;

        if(mzx_world->bomb_type)
        {
          play_sfx(mzx_world, SFX_PLACE_HI_BOMB);
          dec_counter(mzx_world, "HIBOMBS", 1, 0);
        }
        else
        {
          play_sfx(mzx_world, SFX_PLACE_LO_BOMB);
          dec_counter(mzx_world, "LOBOMBS", 1, 0);
        }
      }
    }
  }

  if(mzx_world->player_shoot_cooldown)
    mzx_world->player_shoot_cooldown--;
}

/**
 * Is the player on an entrance?
 */

static boolean player_on_entrance(struct world *mzx_world)
{
  struct board *cur_board = mzx_world->current_board;
  int player_x = mzx_world->player_x;
  int player_y = mzx_world->player_y;
  int offset = xy_to_offset(cur_board, player_x, player_y);

  int under_id = cur_board->level_under_id[offset];

  if(flags[under_id] & A_ENTRANCE)
    return true;

  return false;
}

/**
 * Focus the screen on the player.
 * Mainly for platforms with screens too small to display the whole world.
 */

static void focus_on_player(struct world *mzx_world)
{
  int player_x   = mzx_world->player_x;
  int player_y   = mzx_world->player_y;
  int viewport_x = mzx_world->current_board->viewport_x;
  int viewport_y = mzx_world->current_board->viewport_y;
  int top_x, top_y;

  calculate_xytop(mzx_world, &top_x, &top_y);

  // Focus on the player
  focus_screen(player_x - top_x + viewport_x, player_y - top_y + viewport_y);
}

/**
 * End the game (player lives reached zero).
 */

static void end_game(struct world *mzx_world)
{
  struct board *cur_board = mzx_world->current_board;
  int endgame_board = mzx_world->endgame_board;
  int endgame_x = mzx_world->endgame_x;
  int endgame_y = mzx_world->endgame_y;

  // Game over
  if(endgame_board != NO_ENDGAME_BOARD)
  {
    // Jump to given board
    if(mzx_world->current_board_id == endgame_board)
    {
      id_remove_top(mzx_world, mzx_world->player_x,
        mzx_world->player_y);
      id_place(mzx_world, endgame_x, endgame_y, PLAYER, 0, 0);
      mzx_world->player_x = endgame_x;
      mzx_world->player_y = endgame_y;
    }
    else
    {
      mzx_world->target_board = endgame_board;
      mzx_world->target_where = TARGET_POSITION;
      mzx_world->target_x = endgame_x;
      mzx_world->target_y = endgame_y;
    }

    // Clear "endgame" part
    // FIXME this used to be a global in DOS versions, and the behavior was
    // that the endgame board would be permanently disabled after this teleport.
    // We probably don't want that behavior for port worlds (this regression was
    // added in 2.80X) but it might be worth restoring for old games.
    endgame_board = NO_ENDGAME_BOARD;

    // Give one more life with minimal health
    set_counter(mzx_world, "LIVES", 1, 0);
    set_counter(mzx_world, "HEALTH", 1, 0);
  }
  else
  {
    set_mesg(mzx_world, "Game over");
    /* I can't imagine anything actually relied on this obtuse misbehavior
     * but it's good to version lock anyhow.
     */
    if((mzx_world->version <= V283) || mzx_world->bi_mesg_status)
    {
      cur_board->b_mesg_row = 24;
      cur_board->b_mesg_col = -1;
    }

    if(mzx_world->game_over_sfx)
      play_sfx(mzx_world, SFX_GAME_OVER);

    mzx_world->dead = true;
  }
}

/**
 * End the player's current life.
 */

static void end_life(struct world *mzx_world)
{
  struct board *cur_board = mzx_world->current_board;
  int death_board = mzx_world->death_board;

  // Death
  set_counter(mzx_world, "HEALTH", mzx_world->starting_health, 0);
  dec_counter(mzx_world, "Lives", 1, 0);
  set_mesg(mzx_world, "You have died...");
  sfx_clear_queue();
  play_sfx(mzx_world, SFX_DEATH);

  // Go somewhere else?
  if(death_board != DEATH_SAME_POS)
  {
    if(death_board == NO_DEATH_BOARD)
    {
      int player_restart_x = mzx_world->player_restart_x;
      int player_restart_y = mzx_world->player_restart_y;

      if(player_restart_x >= cur_board->board_width)
        player_restart_x = cur_board->board_width - 1;

      if(player_restart_y >= cur_board->board_height)
        player_restart_y = cur_board->board_height - 1;

      // Return to entry x/y
      id_remove_top(mzx_world, mzx_world->player_x, mzx_world->player_y);
      id_place(mzx_world, player_restart_x, player_restart_y, PLAYER, 0, 0);
      mzx_world->player_x = player_restart_x;
      mzx_world->player_y = player_restart_y;
    }
    else
    {
      // Jump to given board
      if(mzx_world->current_board_id == death_board)
      {
        int death_x = mzx_world->death_x;
        int death_y = mzx_world->death_y;

        id_remove_top(mzx_world, mzx_world->player_x, mzx_world->player_y);
        id_place(mzx_world, death_x, death_y, PLAYER, 0, 0);
        mzx_world->player_x = death_x;
        mzx_world->player_y = death_y;
      }
      else
      {
        mzx_world->target_board = death_board;
        mzx_world->target_where = TARGET_POSITION;
        mzx_world->target_x = mzx_world->death_x;
        mzx_world->target_y = mzx_world->death_y;
      }
    }
  }
}

static Uint32 get_viewport_layer(struct world *mzx_world)
{
  // Creates a new layer if needed to prevent graphical discrepancies between
  // the fallback and layer rendering. This can happen in SMZX mode if:
  // 1) there is a visible viewport border or
  // 2) the message is active.

  struct board *src_board = mzx_world->current_board;

  if(get_screen_mode())
  {
    if((src_board->viewport_width < 80) ||
     (src_board->viewport_height < 25) ||
     (src_board->b_mesg_timer > 0))
      return create_layer(0, 0, 80, 25, LAYER_DRAWORDER_UI - 1, -1, 0, 1);
  }

  return UI_LAYER;
}

/**
 * Update the world, including the player, board, and robots.
 * Draw the world/sprites/etc too.
 */
// FIXME try to break this function down further
void update_world(struct world *mzx_world, boolean is_title)
{
  int time_remaining;
  char tmp_str[10];

  if(!is_title && mzx_world->version >= V251s1 &&
   get_counter(mzx_world, "CURSORSTATE", 0))
  {
    // Turn on mouse
    m_show();
  }
  else
  {
    // Turn off mouse
    m_hide();
  }

  // Update
  focus_on_player(mzx_world); // FIXME do after board scan instead
  update_variables(mzx_world);
  update_mod_volume(mzx_world);
  update_player_under(mzx_world); // Ice, fire, water, lava
  update_player_wind(mzx_world);

  if(!is_title && (!mzx_world->dead))
    update_player_input(mzx_world);

  // Global robot
  if(mzx_world->current_board->robot_list[0])
    if(mzx_world->current_board->robot_list[0]->used)
      run_robot(mzx_world, 0, -1, -1);

  if(!mzx_world->current_cycle_frozen)
  {
    mzx_world->player_was_on_entrance = player_on_entrance(mzx_world);
    mzx_world->was_zapped = false;

    update_board(mzx_world);

    if(player_on_entrance(mzx_world) && !mzx_world->player_was_on_entrance &&
     !mzx_world->was_zapped)
    {
      // Pushed onto an entrance
      sfx_clear_queue();
      entrance(mzx_world, mzx_world->player_x, mzx_world->player_y);
    }
  }
  else
  {
    // Find the player in case the player was moved by the global robot
    find_player(mzx_world);
  }

  // Death and game over
  if(get_counter(mzx_world, "LIVES", 0) == 0)
  {
    end_game(mzx_world);
  }
  else

  if(get_counter(mzx_world, "HEALTH", 0) == 0)
  {
    end_life(mzx_world);
  }

  if(mzx_world->target_where != TARGET_TELEPORT)
  {
    struct board *src_board = mzx_world->current_board;
    Uint32 viewport_layer;
    int top_x;
    int top_y;

    blank_layers();

    // Draw border
    viewport_layer = get_viewport_layer(mzx_world);
    select_layer(viewport_layer);
    draw_viewport(src_board, mzx_world->edge_color);

    // Draw screen
    if(is_title)
    {
      id_chars[player_color] = 0;
      id_chars[player_char + 0] = 32;
      id_chars[player_char + 1] = 32;
      id_chars[player_char + 2] = 32;
      id_chars[player_char + 3] = 32;
    }

    // Figure out x/y of top
    calculate_xytop(mzx_world, &top_x, &top_y);

    if(mzx_world->blind_dur > 0)
    {
      int i;
      int viewport_x = src_board->viewport_x;
      int viewport_y = src_board->viewport_y;
      int viewport_width = src_board->viewport_width;
      int viewport_height = src_board->viewport_height;
      int player_x = mzx_world->player_x;
      int player_y = mzx_world->player_y;

      select_layer(BOARD_LAYER);

      for(i = viewport_y; i < viewport_y + viewport_height; i++)
      {
        fill_line(viewport_width, viewport_x, i, 176, 8);
      }

      // Find where player would be and draw.
      if(!is_title)
      {
        id_put(src_board, player_x - top_x + viewport_x,
         player_y - top_y + viewport_y, player_x,
         player_y, player_x, player_y);
      }
    }
    else
    {
      draw_game_window(src_board, top_x, top_y);
    }
    select_layer(OVERLAY_LAYER);

    // Add sprites
    draw_sprites(mzx_world);

    // Add time limit
    time_remaining = get_counter(mzx_world, "TIME", 0);
    if(time_remaining)
    {
      int edge_color = mzx_world->edge_color;
      int timer_color;
      if(edge_color == 15)
        timer_color = 0xF0; // Prevent white on white for timer
      else
        timer_color = (edge_color << 4) + 15;

      select_layer(UI_LAYER);

      sprintf(tmp_str, "%d:%02d",
       (unsigned short)(time_remaining / 60), (time_remaining % 60) );
      write_string(tmp_str, 1, 24, timer_color, 0);

      // Border with spaces
      draw_char(' ', edge_color, (Uint32)strlen(tmp_str) + 1, 24);
      draw_char(' ', edge_color, 0, 24);
    }

    // Add message
    if(src_board->b_mesg_timer > 0)
    {
      int mesg_y = src_board->b_mesg_row;
      Uint8 tmp_color = scroll_color;
      char *lines[25];
      int i = 1, j;

      if(mzx_world->smzx_message)
        select_layer(viewport_layer);
      else
        select_layer(UI_LAYER);

      /* Always at least one line.. */
      lines[0] = src_board->bottom_mesg;

      /* Find pointers to each "line" terminated by \n */
      while(1)
      {
        char *pos = strchr(lines[i - 1], '\n');
        if(!pos)
          break;
        *pos = 0;
        if(i >= 25 - mesg_y)
          break;
        lines[i] = pos + 1;
        i++;
      }

      for(j = 0; j < i; j++)
      {
        int mesg_length = strlencolor(lines[j]);
        int mesg_edges = mzx_world->mesg_edges;
        int mesg_x = src_board->b_mesg_col;
        char backup = 0;

        if(mesg_length > 80)
        {
          backup = lines[j][80];
          lines[j][80] = 0;
          mesg_length = 80;
        }

        if(mesg_x == -1)
          mesg_x = 40 - (mesg_length / 2);

        color_string_ext_special(lines[j], mesg_x, mesg_y, &tmp_color,
         0, 0, false);

        if((mesg_x > 0) && (mesg_edges))
          draw_char_ext(' ', scroll_color, mesg_x - 1, mesg_y, 0, 0);

        mesg_x += mesg_length;
        if((mesg_x < 80) && (mesg_edges))
          draw_char_ext(' ', scroll_color, mesg_x, mesg_y, 0, 0);

        if(backup)
          lines[j][80] = backup;

        mesg_y++;
      }

      /* Restore original bottom mesg for next iteration */
      for(j = 1; j < i; j++)
        *(lines[j] - 1) = '\n';
    }

    select_layer(UI_LAYER);
    draw_intro_mesg(mzx_world);

    // Add debug box
    if(draw_debug_box && mzx_world->debug_mode)
    {
      draw_debug_box(mzx_world, 60, 19, mzx_world->player_x,
       mzx_world->player_y, 1);
    }
  }
}

/**
 * Handle major game state changes that need to addressed at the start
 * of the cycle: the board fade in, board changes, etc.
 */
// FIXME could probably break down a little, combine parts with game.c, rename
boolean update2(struct world *mzx_world, boolean is_title, boolean *fadein)
{
  struct board *src_board = mzx_world->current_board;
  int board_width = src_board->board_width;
  int board_height = src_board->board_height;
  char *level_id = src_board->level_id;
  char *level_color = src_board->level_color;
  char *level_under_id = src_board->level_under_id;

  if(*fadein)
  {
    vquick_fadein();
    *fadein = false;
  }

  switch(mzx_world->change_game_state)
  {
    case CHANGE_STATE_SWAP_WORLD:
    {
      // Load the new board's mod
      load_board_module(mzx_world);

      // Send both JUSTLOADED and JUSTENTERED; the JUSTENTERED label will take
      // priority if a robot defines it (instead of JUSTLOADED like on the title
      // screen).
      send_robot_def(mzx_world, 0, LABEL_JUSTLOADED);
      send_robot_def(mzx_world, 0, LABEL_JUSTENTERED);

      return true;
    }

    case CHANGE_STATE_LOAD_GAME_ROBOTIC:
    {
      load_game_module(mzx_world, mzx_world->real_mod_playing, false);

      // Only send JUSTLOADED for savegames.
      send_robot_def(mzx_world, 0, LABEL_JUSTLOADED);

      return true;
    }

    case CHANGE_STATE_EXIT_GAME_ROBOTIC:
    case CHANGE_STATE_REQUEST_EXIT:
      // Exit game--skip input processing. The game state will exit.
      return true;

    case CHANGE_STATE_PLAY_GAME_ROBOTIC:
      // We need to continue input processing.
      return false;

    default:
      break;
  }

  if(mzx_world->target_where != TARGET_NONE)
  {
    int saved_player_last_dir = src_board->player_last_dir;
    int target_board = mzx_world->target_board;
    boolean load_assets = false;

    // TELEPORT or ENTRANCE.
    // Destroy message, bullets, spitfire?

    if(mzx_world->clear_on_exit)
    {
      int offset;
      enum thing d_id;

      src_board->b_mesg_timer = 0;
      for(offset = 0; offset < (board_width * board_height); offset++)
      {
        d_id = (enum thing)level_id[offset];
        if((d_id == SHOOTING_FIRE) || (d_id == BULLET))
          offs_remove_id(mzx_world, offset);
      }
    }

    // Load board
    mzx_world->under_player_id = (char)SPACE;
    mzx_world->under_player_param = 0;
    mzx_world->under_player_color = 7;
    mzx_world->current_cycle_odd = false;

    if(mzx_world->current_board_id != target_board)
    {
      change_board(mzx_world, target_board);
      load_assets = true;
    }

    src_board = mzx_world->current_board;

    level_id = src_board->level_id;
    level_color = src_board->level_color;
    level_under_id = src_board->level_under_id;
    board_width = src_board->board_width;
    board_height = src_board->board_height;

    // Find target x/y
    if(mzx_world->target_where == TARGET_ENTRANCE)
    {
      int i;
      int tmp_x[5];
      int tmp_y[5];
      int x, y, offset;
      enum thing d_id;
      enum thing target_id = mzx_world->target_id;
      int target_color = mzx_world->target_color;

      // Entrance
      // First, set searched for id to the first in the whirlpool
      // series if it is part of the whirlpool series
      if(is_whirlpool(target_id))
        target_id = WHIRLPOOL_1;

      // Now scan. Order-
      // 1) Same type & color
      // 2) Same color
      // 3) Same type & foreground
      // 4) Same foreground
      // 5) Same type
      // Search for first of all 5 at once

      for(i = 0; i < 5; i++)
      {
        // None found
        tmp_x[i] = -1;
        tmp_y[i] = -1;
      }

      for(y = 0, offset = 0; y < board_height; y++)
      {
        for(x = 0; x < board_width; x++, offset++)
        {
          d_id = (enum thing)level_id[offset];

          if(d_id == PLAYER)
          {
            // Remove the player, maybe readd
            mzx_world->player_x = x;
            mzx_world->player_y = y;
            id_remove_top(mzx_world, x, y);
            // Grab again - might have revealed an entrance
            d_id = (enum thing)level_id[offset];
          }

          if(is_whirlpool(d_id))
            d_id = WHIRLPOOL_1;

          if(d_id == target_id)
          {
            // Same type
            // Color match?
            if(level_color[offset] == target_color)
            {
              // Yep
              tmp_x[0] = x;
              tmp_y[0] = y;
            }
            else

            // Fg?
            if((level_color[offset] & 0x0F) ==
             (target_color & 0x0F))
            {
              // Yep
              tmp_x[2] = x;
              tmp_y[2] = y;
            }
            else
            {
              // Just same type
              tmp_x[4] = x;
              tmp_y[4] = y;
            }
          }
          else

          if(flags[(int)d_id] & A_ENTRANCE)
          {
            // Not same type, but an entrance
            // Color match?
            if(level_color[offset] == target_color)
            {
              // Yep
              tmp_x[1] = x;
              tmp_y[1] = y;
            }
            // Fg?
            else

            if((level_color[offset] & 0x0F) == (target_color & 0x0F))
            {
              // Yep
              tmp_x[3] = x;
              tmp_y[3] = y;
            }
          }
          // Done with this x/y
        }
      }

      // We've got it... maybe.
      for(i = 0; i < 5; i++)
      {
        if(tmp_x[i] >= 0)
          break;
      }

      if(i < 5)
      {
        mzx_world->player_x = tmp_x[i];
        mzx_world->player_y = tmp_y[i];
      }

      id_place(mzx_world, mzx_world->player_x,
       mzx_world->player_y, PLAYER, 0, 0);
    }
    else
    {
      int target_x = mzx_world->target_x;
      int target_y = mzx_world->target_y;

      // Specified x/y
      if(target_x < 0)
        target_x = 0;

      if(target_y < 0)
        target_y = 0;

      if(target_x >= board_width)
        target_x = board_width - 1;

      if(target_y >= board_height)
        target_y = board_height - 1;

      find_player(mzx_world);
      place_player_xy(mzx_world, target_x, target_y);
    }

    send_robot_def(mzx_world, 0, LABEL_JUSTENTERED);

    // Set the time/restart position/etc even if the board hasn't changed.
    change_board_set_values(mzx_world);

    // Now... Set player_last_dir for direction FACED
    src_board->player_last_dir = (src_board->player_last_dir & 0x0F) |
     (saved_player_last_dir & 0xF0);

    // ...and if player ended up on ICE, set last dir pressed as well
    if((enum thing)level_under_id[mzx_world->player_x +
     (mzx_world->player_y * board_width)] == ICE)
    {
      src_board->player_last_dir = saved_player_last_dir;
    }

    if(mzx_world->target_where != TARGET_TELEPORT)
    {
      // Prepare for fadein
      if(!get_fade_status())
        *fadein = true;
      vquick_fadeout();
    }

    // Load the new board's charset and palette, if necessary
    if(load_assets)
    {
      // Load board's mod unless it's the same mod playing.
      load_game_module(mzx_world, src_board->mod_playing, true);
      change_board_load_assets(mzx_world);

#ifdef CONFIG_EDITOR
      // Also, update the caption to indicate the current board.
      if(mzx_world->editing)
        caption_set_board(mzx_world, mzx_world->current_board);
#endif
    }

    mzx_world->target_where = TARGET_NONE;

    // Disallow any keypresses this cycle

    return true;
  }

  return false;
}
