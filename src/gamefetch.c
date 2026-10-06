#include "gamefetch.h"
#include "tar.h"
#include "fs.h"
#include "vga.h"
#include "mm.h"
#include "serial.h"
#include "string.h"
#include "net/media_fetch.h"
#include "iso9660.h"
#include "ata.h"
#include "elf.h"
#include "task.h"
#include "gui.h"
#include <stdint.h>
#include <stddef.h>

void cmd_game_fetch(const char *args)
{
    if (!args || !args[0]) {
        vga_print_color("Usage: game-fetch <game-id> <url>\n", 0x0E);
        vga_print("  Supported games: tentacle, monkey, atlantis, doom\n");
        vga_print("  Example:\n");
        vga_print("    game-fetch tentacle http://10.0.2.2:8000/tentacle.tar\n");
        return;
    }

    while (*args == ' ') args++;

    char game[64] = "";
    char url[256] = "";

    /* Parse game argument */
    int gi = 0;
    while (*args && *args != ' ' && gi < 63) {
        game[gi++] = *args++;
    }
    game[gi] = '\0';

    while (*args == ' ') args++;

    /* Parse url argument */
    int ui = 0;
    while (*args && *args != ' ' && ui < 255) {
        url[ui++] = *args++;
    }
    url[ui] = '\0';

    if (!url[0]) {
        vga_print_color("game-fetch: missing URL\n", 0x0C);
        return;
    }

    char dest_dir[FS_MAX_PATH];
    if (strcmp(game, "doom") == 0) {
        strncpy(dest_dir, "/DOOM", sizeof(dest_dir) - 1);
    } else {
        strncpy(dest_dir, "/games/", sizeof(dest_dir) - 1);
        size_t dlen = strlen(dest_dir);
        strncpy(dest_dir + dlen, game, sizeof(dest_dir) - 1 - dlen);
    }
    dest_dir[sizeof(dest_dir) - 1] = '\0';

    vga_print_color("[GameFetch] Connecting to: ", 0x0B);
    vga_print(url);
    vga_print("\n");
    vga_print_color("[GameFetch] Target VFS folder: ", 0x07);
    vga_print(dest_dir);
    vga_print("\n");

    uint8_t *data = NULL;
    uint32_t len = 0;

    int ret = media_fetch(url, &data, &len);
    if (!ret || !data || len == 0) {
        vga_print_color("[GameFetch] Error: Download failed (network error or HTTP 404)\n", 0x0C);
        if (data) kfree(data);
        return;
    }

    vga_print_color("[GameFetch] Download complete (", 0x0A);
    char szbuf[16];
    itoa((int)len, szbuf, 10);
    vga_print(szbuf);
    vga_print(" bytes). Installing...\n");

    /* Check if archive is TAR */
    int is_tar = (strstr(url, ".tar") != NULL);
    if (!is_tar && len >= 512) {
        /* Check for ustar signature at offset 257 */
        if (memcmp(data + 257, "ustar", 5) == 0) {
            is_tar = 1;
        }
    }

    if (is_tar) {
        int count = tar_extract(data, len, dest_dir);
        if (count >= 0) {
            vga_print_color("[GameFetch] Successfully extracted ", 0x0A);
            char cbuf[16];
            itoa(count, cbuf, 10);
            vga_print(cbuf);
            vga_print(" files into ");
            vga_print(dest_dir);
            vga_print("!\n");
            vga_print_color("[GameFetch] You can now launch the full game from Desktop or CLI!\n", 0x0B);
        } else {
            vga_print_color("[GameFetch] Error extracting TAR archive.\n", 0x0C);
        }
    } else {
        /* Single file download */
        const char *fname = "game.dat";
        const char *last_slash = strrchr(url, '/');
        if (last_slash && last_slash[1]) fname = last_slash + 1;

        char out_path[FS_MAX_PATH];
        strncpy(out_path, dest_dir, sizeof(out_path) - 1);
        size_t cur = strlen(out_path);
        out_path[cur] = '/';
        strncpy(out_path + cur + 1, fname, sizeof(out_path) - 2 - cur);
        out_path[sizeof(out_path) - 1] = '\0';

        fs_mkdir_p(dest_dir);
        if (fs_write(out_path, (const char *)data, len) == 0) {
            vga_print_color("[GameFetch] Successfully saved to ", 0x0A);
            vga_print(out_path);
            vga_print("\n");
        } else {
            vga_print_color("[GameFetch] Failed writing to VFS target.\n", 0x0C);
        }
    }

    kfree(data);
}

void cmd_tar(const char *args)
{
    if (!args || !args[0]) {
        vga_print("Usage: tar x <archive.tar> [destination_dir]\n");
        return;
    }

    while (*args == ' ') args++;

    if (*args == 'x' || *args == '-') {
        while (*args && *args != ' ') args++;
        while (*args == ' ') args++;
    }

    char tar_path[128] = "";
    char dest_dir[128] = "/";

    int pi = 0;
    while (*args && *args != ' ' && pi < 127) {
        tar_path[pi++] = *args++;
    }
    tar_path[pi] = '\0';

    while (*args == ' ') args++;
    if (*args) {
        int di = 0;
        while (*args && *args != ' ' && di < 127) {
            dest_dir[di++] = *args++;
        }
        dest_dir[di] = '\0';
    }

    if (!tar_path[0]) {
        vga_print("tar: missing archive file\n");
        return;
    }

    fs_node_t *node = fs_resolve(tar_path);
    if (!node || node->type != FS_FILE || !node->data || node->size == 0) {
        vga_print_color("tar: file not found or empty: ", 0x0C);
        vga_print(tar_path);
        vga_print("\n");
        return;
    }

    vga_print("Extracting ");
    vga_print(tar_path);
    vga_print(" to ");
    vga_print(dest_dir);
    vga_print("...\n");

    int count = tar_extract(node->data, node->size, dest_dir);
    if (count >= 0) {
        vga_print_color("[TAR] Extracted ", 0x0A);
        char cbuf[16];
        itoa(count, cbuf, 10);
        vga_print(cbuf);
        vga_print(" files successfully.\n");
    } else {
        vga_print_color("tar: failed to extract archive\n", 0x0C);
    }
}

/* =========================================================================
 * Interactive Game Launching Wizard
 * ========================================================================= */

typedef enum {
    GW_STATE_IDLE = 0,
    GW_STATE_METHOD,
    GW_STATE_URL
} gw_state_t;

static gw_state_t gw_state = GW_STATE_IDLE;
static char gw_game[32] = "";

static int is_game_installed(const char *game)
{
    if (strcasecmp(game, "doom") == 0) {
        return (fs_resolve("/games/doom/doom1.wad") != NULL ||
                fs_resolve("/games/doom/DOOM1.WAD") != NULL ||
                fs_resolve("/games/doom/doom.wad") != NULL ||
                fs_resolve("/DOOM/DOOM1.WAD") != NULL ||
                fs_resolve("/DOOM/doom1.wad") != NULL ||
                fs_resolve("/doom1.wad") != NULL ||
                fs_resolve("doom1.wad") != NULL);
    } else if (strcasecmp(game, "tentacle") == 0) {
        return (fs_resolve("/games/tentacle/TENTACLE.000") != NULL ||
                fs_resolve("/games/tentacle/tentacle.000") != NULL ||
                fs_resolve("/games/tentacle/TENTACLE.001") != NULL ||
                fs_resolve("/games/tentacle/DOTTDEMO.000") != NULL ||
                fs_resolve("/games/tentacle/dottdemo.000") != NULL);
    } else if (strcasecmp(game, "monkey") == 0) {
        return (fs_resolve("/games/monkey/MONKEY.000") != NULL ||
                fs_resolve("/games/monkey/monkey.000") != NULL ||
                fs_resolve("/games/monkey/000.lfl") != NULL ||
                fs_resolve("/games/monkey/000.LFL") != NULL ||
                fs_resolve("/games/monkey/disk01.lec") != NULL ||
                fs_resolve("/games/monkey/DISK01.LEC") != NULL);
    } else if (strcasecmp(game, "atlantis") == 0) {
        return (fs_resolve("/games/atlantis/ATLANTIS.000") != NULL ||
                fs_resolve("/games/atlantis/atlantis.000") != NULL ||
                fs_resolve("/games/atlantis/ATLANTIS.001") != NULL ||
                fs_resolve("/games/atlantis/PLAYFATE.000") != NULL ||
                fs_resolve("/games/atlantis/playfate.000") != NULL);
    }
    return 0;
}

static void launch_game_binary(const char *game)
{
    const char *elf_path = NULL;
    const char *cmd_line = NULL;

    if (strcasecmp(game, "doom") == 0) {
        elf_path = fs_resolve("/bin/doom.elf") ? "/bin/doom.elf" : "/bin/doomgeneric.elf";
        cmd_line = "doom";
    } else if (strcasecmp(game, "tentacle") == 0) {
        elf_path = "/bin/tentacle.elf";
        cmd_line = "tentacle";
    } else if (strcasecmp(game, "monkey") == 0) {
        elf_path = "/bin/monkey.elf";
        cmd_line = "monkey";
    } else if (strcasecmp(game, "atlantis") == 0) {
        elf_path = "/bin/atlantis.elf";
        cmd_line = "atlantis";
    }

    if (!elf_path || !fs_resolve(elf_path)) {
        vga_print_color("[Launcher] Error: Game binary not found: ", 0x0C);
        vga_print(elf_path ? elf_path : "unknown");
        vga_print("\n");
        return;
    }

    vga_print_color("[Launcher] Spawning ", 0x0A);
    vga_print(elf_path);
    vga_print("...\n");

    int pid = elf_load_file_args(elf_path, cmd_line);
    if (pid <= 0) {
        vga_print_color("[Launcher] Failed to spawn process.\n", 0x0C);
        return;
    }

    /* If in text mode, boot GUI to show game window */
    if (!gui_active) {
        vga_print("[Launcher] Starting ArchaOS GUI Desktop...\n");
        gui_enter();
    }
}

static int download_and_extract_game(const char *game, const char *url_input)
{
    char url[256] = "";
    char fallback_url[256] = "";

    if (!url_input || !url_input[0] || strcasecmp(url_input, "default") == 0) {
        if (strcasecmp(game, "doom") == 0) {
            strncpy(url, "https://raw.githubusercontent.com/AkshajCreator/ArchaOS/main/games_storage/doom.tar", sizeof(url) - 1);
            strncpy(fallback_url, "http://10.0.2.2:8000/doom.tar", sizeof(fallback_url) - 1);
        } else if (strcasecmp(game, "tentacle") == 0) {
            strncpy(url, "https://raw.githubusercontent.com/AkshajCreator/ArchaOS/main/games_storage/tentacle.tar", sizeof(url) - 1);
            strncpy(fallback_url, "http://10.0.2.2:8000/tentacle.tar", sizeof(fallback_url) - 1);
        } else if (strcasecmp(game, "monkey") == 0) {
            strncpy(url, "https://raw.githubusercontent.com/AkshajCreator/ArchaOS/main/games_storage/monkey.tar", sizeof(url) - 1);
            strncpy(fallback_url, "http://10.0.2.2:8000/monkey.tar", sizeof(fallback_url) - 1);
        } else if (strcasecmp(game, "atlantis") == 0) {
            strncpy(url, "https://raw.githubusercontent.com/AkshajCreator/ArchaOS/main/games_storage/atlantis.tar", sizeof(url) - 1);
            strncpy(fallback_url, "http://10.0.2.2:8000/atlantis.tar", sizeof(fallback_url) - 1);
        }
    } else {
        strncpy(url, url_input, sizeof(url) - 1);
    }

    char args[320];
    strcpy(args, game);
    strcat(args, " ");
    strcat(args, url);
    cmd_game_fetch(args);

    /* If download failed and we have a local fallback URL, try the fallback */
    if (!is_game_installed(game) && fallback_url[0]) {
        vga_print_color("[Launcher] Retrying with local test server: ", 0x0E);
        vga_print(fallback_url);
        vga_print("\n");
        strcpy(args, game);
        strcat(args, " ");
        strcat(args, fallback_url);
        cmd_game_fetch(args);
    }

    /* Doom sync helper: duplicate wad to /DOOM if needed */
    if (strcasecmp(game, "doom") == 0) {
        fs_mkdir_p("/DOOM");
        fs_node_t *gwad = fs_resolve("/games/doom/doom1.wad");
        if (gwad && gwad->data && gwad->size > 0 && !fs_resolve("/DOOM/DOOM1.WAD")) {
            fs_write("/DOOM/DOOM1.WAD", (const char *)gwad->data, gwad->size);
            fs_write("/DOOM/doom1.wad", (const char *)gwad->data, gwad->size);
        }
    }

    return is_game_installed(game);
}

static int mount_cdrom_for_game(const char *game)
{
    char target_dir[64];
    if (strcasecmp(game, "doom") == 0) {
        strncpy(target_dir, "/DOOM", sizeof(target_dir));
    } else {
        strcpy(target_dir, "/games/");
        strcat(target_dir, game);
    }

    vga_print_color("[CD-ROM] Scanning optical & secondary media drives...\n", 0x0B);

    int count = ata_get_device_count();
    int mounted_files = -1;
    int mounted_dev = -1;

    for (int dev = 0; dev < count && dev < 4; dev++) {
        ata_device_t *d = ata_get_device(dev);
        if (!d || !d->present) continue;

        int res = iso9660_mount(dev, target_dir);
        if (res > 0) {
            mounted_files = res;
            mounted_dev = dev;
            break;
        }
    }

    if (mounted_files >= 0) {
        vga_print_color("[CD-ROM] Success: Mounted ", 0x0A);
        char fbuf[16]; itoa(mounted_files, fbuf, 10); vga_print(fbuf);
        vga_print(" files from Drive ");
        char dbuf[16]; itoa(mounted_dev, dbuf, 10); vga_print(dbuf);
        vga_print(" into "); vga_print(target_dir); vga_print("!\n");
        return 1;
    }

    vga_print_color("[CD-ROM] No ISO9660 CD-ROM or virtual ISO image detected on ATA drives.\n", 0x0C);
    vga_print("  (To attach an ISO in QEMU: add '-cdrom <path_to_game.iso>' to command line)\n");
    return 0;
}

int game_wizard_handle(const char *cmd)
{
    if (!cmd) return 0;
    while (*cmd == ' ') cmd++;
    if (!*cmd) return 0;

    if (gw_state == GW_STATE_IDLE) {
        /* Check if command matches any supported game */
        const char *matched = NULL;
        if (strcasecmp(cmd, "doom") == 0) matched = "doom";
        else if (strcasecmp(cmd, "tentacle") == 0 || strcasecmp(cmd, "dott") == 0) matched = "tentacle";
        else if (strcasecmp(cmd, "monkey") == 0) matched = "monkey";
        else if (strcasecmp(cmd, "atlantis") == 0 || strcasecmp(cmd, "fate") == 0) matched = "atlantis";

        if (!matched) return 0; /* Not a game command */

        strncpy(gw_game, matched, sizeof(gw_game) - 1);
        gw_game[sizeof(gw_game) - 1] = '\0';
        gw_state = GW_STATE_METHOD;

        /* Print the exact wizard text requested by user */
        vga_print("Select game launching method:\n");
        vga_print("a) Link (Hint: \"If you choose this, in the text box after selecting this, write \"default\")\n");
        vga_print("b) CD-ROM (Supports Virtual ISO images or physical CD disks formatted to FAT32)\n");
        if (is_game_installed(gw_game)) {
            vga_print("c) Play installed game\n");
        }
        return 1;
    }

    if (gw_state == GW_STATE_METHOD) {
        /* User chooses method */
        if (strcasecmp(cmd, "a") == 0 || strcasecmp(cmd, "a)") == 0 ||
            strcasecmp(cmd, "link") == 0 || strcasecmp(cmd, "1") == 0) {
            gw_state = GW_STATE_URL;
            vga_print("Enter download URL (or write \"default\"): \n");
            return 1;
        }

        /* If user typed "default" directly right after seeing the prompt */
        if (strcasecmp(cmd, "default") == 0) {
            vga_print_color("[Launcher] Fetching default game archive...\n", 0x0B);
            download_and_extract_game(gw_game, "default");
            gw_state = GW_STATE_IDLE;
            launch_game_binary(gw_game);
            return 1;
        }

        if (strcasecmp(cmd, "b") == 0 || strcasecmp(cmd, "b)") == 0 ||
            strcasecmp(cmd, "cd") == 0 || strcasecmp(cmd, "cd-rom") == 0 ||
            strcasecmp(cmd, "cdrom") == 0 || strcasecmp(cmd, "2") == 0) {
            gw_state = GW_STATE_IDLE;
            if (mount_cdrom_for_game(gw_game)) {
                launch_game_binary(gw_game);
            }
            return 1;
        }

        if (strcasecmp(cmd, "c") == 0 || strcasecmp(cmd, "c)") == 0 ||
            strcasecmp(cmd, "play") == 0 || strcasecmp(cmd, "3") == 0) {
            gw_state = GW_STATE_IDLE;
            launch_game_binary(gw_game);
            return 1;
        }

        if (strcasecmp(cmd, "cancel") == 0 || strcasecmp(cmd, "exit") == 0 || strcasecmp(cmd, "q") == 0) {
            vga_print("[Launcher] Canceled.\n");
            gw_state = GW_STATE_IDLE;
            return 1;
        }

        vga_print("Invalid choice. Please enter 'a', 'b', or 'default' (or 'cancel'):\n");
        return 1;
    }

    if (gw_state == GW_STATE_URL) {
        /* User provides URL or "default" */
        if (strcasecmp(cmd, "cancel") == 0 || strcasecmp(cmd, "exit") == 0 || strcasecmp(cmd, "q") == 0) {
            vga_print("[Launcher] Canceled.\n");
            gw_state = GW_STATE_IDLE;
            return 1;
        }

        vga_print_color("[Launcher] Fetching game package...\n", 0x0B);
        download_and_extract_game(gw_game, cmd);
        gw_state = GW_STATE_IDLE;
        launch_game_binary(gw_game);
        return 1;
    }

    return 0;
}

