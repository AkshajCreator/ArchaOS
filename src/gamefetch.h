#ifndef GAMEFETCH_H
#define GAMEFETCH_H

void cmd_game_fetch(const char *args);
void cmd_tar(const char *args);

/* Interactive Game Launching Wizard
 * Returns 1 if handled by the wizard, 0 otherwise.
 */
int game_wizard_handle(const char *cmd);

#endif /* GAMEFETCH_H */
