/*
 * NSF Player - Plays NSF files using 6502 CPU + APU emulator
 *
 * Pure C, no platform dependencies. Suitable for ESP32.
 *
 * NSF format:
 * - 128-byte header with init/play addresses and bank info
 * - ROM data loaded at load_addr (or bank-switched)
 * - INIT routine called once with song# in A, PAL/NTSC in X
 * - PLAY routine called at 60Hz (NTSC) to generate audio
 * - APU writes at $4000-$4017 produce sound
 */

#include "nsf_player.h"
#include "apu_if.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Memory write handler: intercept APU register writes */
static void cpu_write_handler(uint16_t addr, uint8_t data, void *userdata)
{
    nsf_player_t *player = (nsf_player_t *)userdata;

    /* APU registers */
    if (addr >= 0x4000 && addr <= 0x4017 && addr != 0x4014 && addr != 0x4016) {
        apuif_write_reg(addr, data);
    }

    /* Bankswitch registers ($5FF8-$5FFF) */
    if (player->use_bankswitch && addr >= 0x5FF8 && addr <= 0x5FFF) {
        int bank = addr - 0x5FF8;
        uint32_t offset = (uint32_t)data * 0x1000;
        uint16_t dest = 0x8000 + bank * 0x1000;

        if (offset < player->rom_size) {
            uint32_t copy_size = 0x1000;
            if (offset + copy_size > player->rom_size) {
                copy_size = player->rom_size - offset;
            }
            memcpy(&player->cpu.mem[dest], &player->rom_data[offset], copy_size);
        }
    }

    /* Some NSFs also use $5FF6-$5FF7 for $6000-$7FFF banking */
    if (player->use_bankswitch && addr >= 0x5FF6 && addr <= 0x5FF7) {
        int bank = addr - 0x5FF6;
        uint32_t offset = (uint32_t)data * 0x1000;
        uint16_t dest = 0x6000 + bank * 0x1000;

        if (offset < player->rom_size) {
            uint32_t copy_size = 0x1000;
            if (offset + copy_size > player->rom_size) {
                copy_size = player->rom_size - offset;
            }
            memcpy(&player->cpu.mem[dest], &player->rom_data[offset], copy_size);
        }
    }
}

static int load_nsf_data(nsf_player_t *player, const uint8_t *data, uint32_t size)
{
    if (size < 128) {
        fprintf(stderr, "NSF: File too small\n");
        return -1;
    }

    /* Parse header */
    memcpy(&player->header, data, sizeof(nsf_header_t));

    if (memcmp(player->header.tag, "NESM\x1A", 5) != 0) {
        fprintf(stderr, "NSF: Invalid header magic\n");
        return -1;
    }

    printf("NSF: '%s' by '%s'\n", player->header.song_name, player->header.artist);
    printf("NSF: %d songs, load=$%04X init=$%04X play=$%04X\n",
           player->header.total_songs,
           player->header.load_addr,
           player->header.init_addr,
           player->header.play_addr);

    /* Check for bankswitching */
    player->use_bankswitch = false;
    for (int i = 0; i < 8; i++) {
        if (player->header.bankswitch[i] != 0) {
            player->use_bankswitch = true;
            break;
        }
    }

    if (player->use_bankswitch) {
        printf("NSF: Using bankswitching\n");
    }

    if (player->header.chip_flags) {
        printf("NSF: Warning - extra chip flags=0x%02X (not supported)\n",
               player->header.chip_flags);
    }

    /* Store ROM data.
     * For bankswitched NSFs, the ROM data may start at an offset
     * within the first 4KB page (load_addr & 0x0FFF).
     * We pad the beginning so bank 0 aligns to a 4KB boundary. */
    uint32_t rom_offset = 128; /* After header */
    uint32_t raw_rom_size = size - rom_offset;
    uint32_t pad = player->header.load_addr & 0x0FFF;

    player->rom_size = raw_rom_size + pad;
    player->rom_data = (uint8_t *)apuemu_malloc(player->rom_size);
    if (!player->rom_data) {
        fprintf(stderr, "NSF: Failed to allocate ROM data\n");
        return -1;
    }
    memset(player->rom_data, 0, pad);
    memcpy(player->rom_data + pad, data + rom_offset, raw_rom_size);

    printf("NSF: ROM size=%u, pad=%u, total=%u\n", raw_rom_size, pad, player->rom_size);

    /* Initialize CPU */
    nes_cpu_init(&player->cpu);
    nes_cpu_set_write_cb(&player->cpu, cpu_write_handler, player);

    /* Load ROM into CPU memory */
    if (player->use_bankswitch) {
        /* Set up initial banks.
         * NSF bankswitch maps 8 x 4KB pages to $8000-$FFFF.
         * Each bankswitch[i] value is a 4KB bank number in the ROM. */
        for (int i = 0; i < 8; i++) {
            uint32_t offset = (uint32_t)player->header.bankswitch[i] * 0x1000;
            uint16_t dest = 0x8000 + i * 0x1000;
            if (offset < player->rom_size) {
                uint32_t copy_size = 0x1000;
                if (offset + copy_size > player->rom_size) {
                    copy_size = player->rom_size - offset;
                }
                memcpy(&player->cpu.mem[dest], &player->rom_data[offset], copy_size);
            }
        }
        printf("NSF: Banks loaded: ");
        for (int i = 0; i < 8; i++) printf("%d ", player->header.bankswitch[i]);
        printf("\n");
    } else {
        /* Non-bankswitched: load directly at load_addr */
        uint16_t load_addr = player->header.load_addr;
        uint32_t copy_size = raw_rom_size;
        if (load_addr + copy_size > 0x10000) {
            copy_size = 0x10000 - load_addr;
        }
        memcpy(&player->cpu.mem[load_addr], data + rom_offset, copy_size);
    }

    player->loaded = true;
    return 0;
}

int nsf_player_load(nsf_player_t *player, const char *filename)
{
    memset(player, 0, sizeof(*player));

    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "NSF: Cannot open '%s'\n", filename);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = (uint8_t *)apuemu_malloc(fsize);
    if (!data) {
        fclose(f);
        return -1;
    }

    if ((long)fread(data, 1, fsize, f) != fsize) {
        apuemu_free(data);
        fclose(f);
        return -1;
    }
    fclose(f);

    int ret = load_nsf_data(player, data, (uint32_t)fsize);
    apuemu_free(data);
    return ret;
}

int nsf_player_load_mem(nsf_player_t *player, const uint8_t *data, uint32_t size)
{
    memset(player, 0, sizeof(*player));
    return load_nsf_data(player, data, size);
}

int nsf_player_start(nsf_player_t *player, int song)
{
    if (!player->loaded) return -1;
    if (song < 0 || song >= player->header.total_songs) {
        fprintf(stderr, "NSF: Invalid song %d (max %d)\n", song, player->header.total_songs - 1);
        return -1;
    }

    player->current_song = song;
    player->frames_played = 0;

    /* Clear RAM (not ROM area) */
    memset(player->cpu.mem, 0, 0x800);
    memset(&player->cpu.mem[0x6000], 0, 0x2000); /* SRAM area */

    /* Reload ROM if bankswitched */
    if (player->use_bankswitch) {
        for (int i = 0; i < 8; i++) {
            uint32_t offset = (uint32_t)player->header.bankswitch[i] * 0x1000;
            uint16_t dest = 0x8000 + i * 0x1000;
            if (offset < player->rom_size) {
                uint32_t copy_size = 0x1000;
                if (offset + copy_size > player->rom_size) {
                    copy_size = player->rom_size - offset;
                }
                memcpy(&player->cpu.mem[dest], &player->rom_data[offset], copy_size);
            }
        }
    }

    /* Reset CPU state */
    player->cpu.regs.a = (uint8_t)song;
    player->cpu.regs.x = 0; /* 0 = NTSC */
    player->cpu.regs.y = 0;
    player->cpu.regs.sp = 0xFF;
    player->cpu.regs.p = FLAG_U | FLAG_I;
    player->cpu.cycles = 0;
    player->cpu.halted = false;

    /* Enable all APU channels */
    apuif_write_reg(0x4015, 0x0F);

    /* Debug: dump first bytes at init_addr */
    printf("NSF: Code at $%04X: ", player->header.init_addr);
    for (int i = 0; i < 16; i++) {
        printf("%02X ", player->cpu.mem[player->header.init_addr + i]);
    }
    printf("\n");

    /* Call INIT routine */
    printf("NSF: Calling INIT($%04X) with song=%d\n", player->header.init_addr, song);
    nes_cpu_jsr(&player->cpu, player->header.init_addr);
    printf("NSF: INIT complete (%d cycles)\n", player->cpu.cycles);

    player->playing = true;
    return 0;
}

void nsf_player_tick(nsf_player_t *player)
{
    if (!player->playing) return;

    /* Reset cycle counter and halt flag for this frame */
    player->cpu.cycles = 0;
    player->cpu.halted = false;

    /* Call PLAY routine */
    nes_cpu_jsr(&player->cpu, player->header.play_addr);

    player->frames_played++;
}

void nsf_player_free(nsf_player_t *player)
{
    if (player->rom_data) {
        apuemu_free(player->rom_data);
        player->rom_data = NULL;
    }
    player->loaded = false;
    player->playing = false;
}
