#ifndef JJ1_SCENES_H
#define JJ1_SCENES_H

/* Each call takes ownership of the VDP display, stops the XGM song on return,
 * and restores neither video assets nor music. Reinitialize them afterwards. */
void jj1_play_startup(void);
void jj1_play_info_pages(unsigned char order);
/* Returns the approached planet ID, or previous when unchanged/unsupported. */
unsigned short jj1_play_planet(unsigned char stage, unsigned short previous);
void jj1_show_original_title(void);
void jj1_show_original_menu(unsigned char option);
void jj1_set_menu_option(unsigned char option);
void jj1_play_episode_ending(const unsigned char *music);
void jj1_play_level_clear(const unsigned char *music);
void jj1_play_bonus_clear(const unsigned char *music);
#endif
