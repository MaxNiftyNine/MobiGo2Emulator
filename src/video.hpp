#pragma once

#include "cpu.hpp"

namespace mobigo {

struct Video {
    SDL_Window *win = nullptr;
    SDL_Renderer *ren = nullptr;
    SDL_Texture *tex = nullptr;
    static constexpr int W = 320;
    static constexpr int H = 240;
    std::vector<uint32_t> pixels = std::vector<uint32_t>(W * H);
    uint32_t ppu_page_diag_count = 0;
    uint32_t ppu_page_late_diag_count = 0;
    uint32_t mba_scanout_stall_log_count = 0;
    std::array<uint32_t, 0x8000> rgb555_effect_lut{};
    uint16_t effect_fade = 0xffff;
    uint16_t effect_saturation = 0xffff;

    void init(bool vsync) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) die(SDL_GetError());
        win = SDL_CreateWindow("MobiGo 2 GPL16250 Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W * 2, H * 2, SDL_WINDOW_SHOWN);
        if (!win) die(SDL_GetError());
        const Uint32 renderer_flags = SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
        ren = SDL_CreateRenderer(win, -1, renderer_flags);
        if (!ren) {
            std::cerr << "warning: accelerated renderer unavailable; using software: "
                      << SDL_GetError() << "\n";
            ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        }
        if (!ren) die(SDL_GetError());
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
        if (!tex) die(SDL_GetError());
    }

    void shutdown() {
        if (tex) SDL_DestroyTexture(tex);
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        SDL_Quit();
    }

    static uint32_t rgb555_to_argb(uint16_t v) {
        const uint8_t r5 = uint8_t((v >> 10) & 0x1f);
        const uint8_t g5 = uint8_t((v >> 5) & 0x1f);
        const uint8_t b5 = uint8_t(v & 0x1f);
        const uint8_t r = uint8_t((r5 << 3) | (r5 >> 2));
        const uint8_t g = uint8_t((g5 << 3) | (g5 >> 2));
        const uint8_t b = uint8_t((b5 << 3) | (b5 >> 2));
        return 0xff000000u | (r << 16) | (g << 8) | b;
    }

    static uint32_t rgb565_to_argb(uint16_t v) {
        const uint8_t r5 = uint8_t((v >> 11) & 0x1f);
        const uint8_t g6 = uint8_t((v >> 5) & 0x3f);
        const uint8_t b5 = uint8_t(v & 0x1f);
        const uint8_t r = uint8_t((r5 << 3) | (r5 >> 2));
        const uint8_t g = uint8_t((g6 << 2) | (g6 >> 4));
        const uint8_t b = uint8_t((b5 << 3) | (b5 >> 2));
        return 0xff000000u | (r << 16) | (g << 8) | b;
    }

    static uint16_t argb_to_rgb555(uint32_t v) {
        const uint16_t r = uint16_t((v >> 19) & 0x1f);
        const uint16_t g = uint16_t((v >> 11) & 0x1f);
        const uint16_t b = uint16_t((v >> 3) & 0x1f);
        return uint16_t((r << 10) | (g << 5) | b);
    }

    static uint16_t argb_to_rgb565(uint32_t v) {
        const uint16_t r = uint16_t((v >> 19) & 0x1f);
        const uint16_t g = uint16_t((v >> 10) & 0x3f);
        const uint16_t b = uint16_t((v >> 3) & 0x1f);
        return uint16_t((r << 11) | (g << 5) | b);
    }

    static uint8_t global_blend_alpha(const Bus &bus) {
        // P_Blending (0x702a) selects 25%, 50%, 75%, or 100% top-layer
        // opacity. The level is only consulted when a tile/sprite requests
        // blending.
        static constexpr std::array<uint8_t, 4> levels{8, 16, 24, 32};
        return levels[bus.mmio[0x702a - kMmioBase] & 3];
    }

    static uint16_t blend_rgb555(uint16_t bottom, uint16_t top, uint8_t alpha) {
        if (bottom & 0x8000) return top;
        const auto mix = [alpha](uint16_t a, uint16_t b) -> uint16_t {
            return uint16_t(((32u - alpha) * a + alpha * b) >> 5);
        };
        return uint16_t(
            (mix((bottom >> 10) & 31, (top >> 10) & 31) << 10) |
            (mix((bottom >> 5) & 31, (top >> 5) & 31) << 5) |
            mix(bottom & 31, top & 31));
    }

    void update_effect_lut(const Bus &bus) {
        const uint16_t fade = bus.mmio[0x7030 - kMmioBase];
        const uint16_t saturation = bus.mmio[0x703c - kMmioBase] & 0x00ff;
        if (fade == effect_fade && saturation == effect_saturation) return;
        effect_fade = fade;
        effect_saturation = saturation;
        const double saturation_scale =
            double(0xff - saturation) / double(0xff - 0x20);
        for (uint32_t value = 0; value < rgb555_effect_lut.size(); ++value) {
            const uint32_t raw = rgb555_to_argb(uint16_t(value));
            const double red = double((raw >> 16) & 0xff);
            const double green = double((raw >> 8) & 0xff);
            const double blue = double(raw & 0xff);
            const double luma = red * 0.299 + green * 0.587 + blue * 0.114;
            auto adjust = [&](double channel) -> uint32_t {
                const double saturated = saturation == 0x20
                    ? channel : luma + (channel - luma) * saturation_scale;
                const int faded = int(std::floor(saturated)) - int(fade);
                return uint32_t(std::clamp(faded, 0, 255));
            };
            rgb555_effect_lut[value] = 0xff000000u |
                (adjust(red) << 16) | (adjust(green) << 8) | adjust(blue);
        }
    }

    static uint32_t tile_palette_base(uint16_t attr, uint32_t nc_bpp) {
        uint32_t palette_offset = (attr & 0x0f00) >> 4;
        palette_offset >>= nc_bpp;
        palette_offset <<= nc_bpp;
        return palette_offset;
    }

    static int signed_sprite_coord(uint16_t raw) {
        const int value = int(raw & 0x03ff);
        return (value & 0x0200) ? value - 0x0400 : value;
    }

    template <typename ArrayT>
    static void log_word_slice(const char *label, const ArrayT &words, uint32_t base, uint32_t count) {
        if (!g_log) return;
        g_log << label << "[0x" << std::hex << base << "]";
        for (uint32_t i = 0; i < count; ++i) {
            g_log << " " << std::setw(4) << std::setfill('0')
                  << words[(base + i) & (words.size() - 1)]
                  << std::setfill(' ');
        }
        g_log << "\n";
    }

    static void log_dma_words(Bus &bus, const char *label, uint32_t base, uint32_t count) {
        if (!g_log) return;
        g_log << label << "[0x" << std::hex << base << "]";
        for (uint32_t i = 0; i < count; ++i) {
            g_log << " " << std::setw(4) << std::setfill('0')
                  << bus.dma_read((base + i) & kAddrMask)
                  << std::setfill(' ');
        }
        g_log << "\n";
    }

    static bool vcomp_logical_scanline(Bus &bus, uint32_t scanline, uint32_t &logical_scanline) {
        int currentline = 0;
        int step = int(bus.mmio[0x701e - kMmioBase] & 0x00ff);
        if (step & 0x80) step -= 0x100;
        int current_inc_value = int(bus.mmio[0x701c - kMmioBase]) << 4;
        int counter = 0;
        const uint32_t offset = bus.mmio[0x701d - kMmioBase];

        if (current_inc_value == 0 && step == 0) {
            if (scanline < offset) return false;
            logical_scanline = scanline - offset;
            return logical_scanline < H;
        }

        for (uint32_t i = 0; i <= scanline; ++i) {
            bool visible = false;
            if (i >= offset && currentline >= 0 && currentline < H) {
                logical_scanline = uint32_t(currentline);
                visible = true;
            }
            counter += current_inc_value;
            while (counter >= (0x20 << 4)) {
                ++currentline;
                current_inc_value += step;
                counter -= (0x20 << 4);
            }
            if (i == scanline) return visible;
        }
        return false;
    }

    void draw_tile_strip(Bus &bus, std::array<uint16_t, W> &line, uint32_t tilegfxbase,
                         uint32_t tile, uint32_t tile_scanline, int drawx,
                         uint32_t tile_w, uint32_t tile_h, uint32_t nc_bpp,
                         uint32_t bits_per_row, uint32_t words_per_tile,
                         bool flip_x, bool flip_y, uint32_t palette_offset,
                         bool blend, uint8_t blend_alpha, bool wrap_x = true) {
        const uint32_t yflipmask = flip_y ? tile_h - 1 : 0;
        uint32_t src = tilegfxbase + words_per_tile * tile + bits_per_row * (tile_scanline ^ yflipmask);
        uint32_t bits = 0;
        uint32_t nbits = 0;

        for (int px = flip_x ? int(tile_w) - 1 : 0; flip_x ? px >= 0 : px < int(tile_w); flip_x ? --px : ++px) {
            bits <<= nc_bpp;
            if (nbits < nc_bpp) {
                uint16_t b = bus.dma_read(src++);
                b = uint16_t((b << 8) | (b >> 8));
                bits |= uint32_t(b) << (nc_bpp - nbits);
                nbits += 16;
            }
            nbits -= nc_bpp;

            const uint32_t color_index = bits >> 16;
            bits &= 0xffff;
            const int unwrapped_x = drawx + px;
            const uint32_t x = wrap_x
                ? (uint32_t(unwrapped_x) & 0x1ff)
                : uint32_t(unwrapped_x);
            if ((!wrap_x && unwrapped_x < 0) || x >= W) continue;

            const uint16_t rgb = bus.palette_ram[(palette_offset + color_index) & 0x0fff];
            if (rgb & 0x8000) continue;
            if (blend) {
                line[x] = blend_rgb555(line[x], rgb, blend_alpha);
            } else {
                line[x] = rgb;
            }
        }
    }

    void draw_direct_bitmap_strip(Bus &bus, std::array<uint16_t, W> &line,
                                  uint32_t source, uint32_t tile_scanline, int drawx,
                                  uint32_t tile_w, uint32_t tile_h,
                                  bool flip_x, bool flip_y) {
        const uint32_t yflipmask = flip_y ? tile_h - 1 : 0;
        source += (tile_scanline ^ yflipmask) * tile_w;
        for (int px = flip_x ? int(tile_w) - 1 : 0;
             flip_x ? px >= 0 : px < int(tile_w);
             flip_x ? --px : ++px) {
            const uint32_t x = uint32_t(drawx + px) & 0x1ff;
            const uint16_t rgb1555 = bus.dma_read(source++);
            if (x < W && !(rgb1555 & 0x8000)) line[x] = rgb1555;
        }
    }

    void draw_linemap(Bus &bus, std::array<uint16_t, W> &line, int y,
                      uint32_t tilegfxbase, uint32_t scroll_reg,
                      uint32_t attr_reg, uint32_t ctrl_reg,
                      uint32_t tilemap_reg, uint32_t attrmap_reg) {
        const uint16_t attr = bus.mmio[attr_reg - kMmioBase];
        const uint16_t ctrl = bus.mmio[ctrl_reg - kMmioBase];
        const uint32_t tilemap = bus.mmio[tilemap_reg - kMmioBase];
        const uint32_t palette_map = bus.mmio[attrmap_reg - kMmioBase];
        const uint32_t yscroll = bus.mmio[scroll_reg + 1 - kMmioBase];
        const uint32_t realline = (uint32_t(y) + yscroll) & 0xff;

        uint32_t tile = bus.read(tilemap + realline);
        uint16_t palette = bus.read(palette_map + realline / 2);
        palette = (y & 1) ? uint16_t(palette >> 8) : uint16_t(palette & 0x00ff);
        uint32_t sourcebase = tilegfxbase + tile + (uint32_t(palette) << 16);

        if (ctrl & 0x0080) {
            for (uint32_t x = 0; x < W; ++x) {
                const uint16_t data = bus.dma_read(sourcebase + x);
                if (!(data & 0x8000)) line[x] = data & 0x7fff;
            }
            return;
        }

        const uint32_t nc_bpp = ((attr & 3) + 1) << 1;
        uint32_t palette_offset = tile_palette_base(attr, nc_bpp);
        uint32_t bits = 0;
        uint32_t nbits = 0;

        for (uint32_t x = 0; x < W; ++x) {
            bits <<= nc_bpp;
            if (nbits < nc_bpp) {
                uint16_t b = bus.dma_read(sourcebase++);
                b = uint16_t((b << 8) | (b >> 8));
                bits |= uint32_t(b) << (nc_bpp - nbits);
                nbits += 16;
            }
            nbits -= nc_bpp;

            const uint32_t color_index = bits >> 16;
            bits &= 0xffff;
            const uint16_t rgb = bus.palette_ram[(palette_offset + color_index) & 0x0fff];
            if (!(rgb & 0x8000)) line[x] = rgb;
        }
    }

    void draw_page(Bus &bus, std::array<uint16_t, W> &line, int priority, int y,
                   uint32_t base_lsb_reg, uint32_t base_msb_reg,
                   uint32_t scroll_reg, uint32_t attr_reg, uint32_t ctrl_reg,
                   uint32_t tilemap_reg, uint32_t attrmap_reg) {
        const uint16_t attr = bus.mmio[attr_reg - kMmioBase];
        const uint16_t ctrl = bus.mmio[ctrl_reg - kMmioBase];
        if (!(ctrl & 0x0008)) return;
        if (((attr >> 12) & 3) != uint32_t(priority)) return;

        const uint16_t ppu = bus.mmio[0x707f - kMmioBase];
        const uint32_t tilegfxbase = (ppu & 0x0040)
            ? ((uint32_t(bus.mmio[base_msb_reg - kMmioBase] & 0x07ff) << 16) | bus.mmio[base_lsb_reg - kMmioBase])
            : uint32_t(bus.mmio[base_lsb_reg - kMmioBase]) * 0x40;

        if (ctrl & 0x0001) {
            draw_linemap(bus, line, y, tilegfxbase, scroll_reg, attr_reg,
                         ctrl_reg, tilemap_reg, attrmap_reg);
            return;
        }

        uint32_t logical_scanline = uint32_t(y);
        if (ctrl & 0x0040) {
            if (!vcomp_logical_scanline(bus, uint32_t(y), logical_scanline)) return;
        }

        const uint32_t xscroll = bus.mmio[scroll_reg - kMmioBase];
        const uint32_t yscroll = bus.mmio[scroll_reg + 1 - kMmioBase];
        const uint32_t tilemap = bus.mmio[tilemap_reg - kMmioBase];
        const uint32_t attrmap = bus.mmio[attrmap_reg - kMmioBase];
        const uint32_t tile_w = 8u << ((attr >> 4) & 3);
        const uint32_t tile_h = 8u << ((attr >> 6) & 3);
        const uint32_t nc_bpp = ((attr & 3) + 1) << 1;
        const uint32_t total_width = (attr & 0x8000) ? 1024 : 512;
        const uint32_t screen_width = (attr & 0x8000) ? 640 : 320;
        const uint32_t tile_count_x = total_width / tile_w;
        const uint32_t ymask = (((attr & 0x8000) ? 0x200 : 0x100) << ((attr & 0x4000) ? 1 : 0)) - 1;
        const uint32_t bitmap_y = (logical_scanline + yscroll) & ymask;
        const uint32_t y0 = bitmap_y / tile_h;
        const uint32_t tile_scanline = bitmap_y % tile_h;
        const uint32_t bits_per_row = nc_bpp * tile_w / 16;
        const uint32_t words_per_tile = (ppu & 0x0004) ? 8 : bits_per_row * tile_h;
        const uint32_t endpos = (screen_width + tile_w) / tile_w;
        int realxscroll = int(xscroll);
        if (ctrl & 0x0010) {
            // MAME GPL renderer maps 0x7100..0x71ff as Tx_Hvoffset and adds
            // signed row scroll per logical scanline when ctrl bit 4 is set.
            realxscroll += int16_t(bus.rowscroll_ram[logical_scanline & 0xff]);
        }
        const int real_upper_scroll_tiles = realxscroll >> (((attr >> 4) & 3) + 3);

        for (uint32_t x0 = 0; x0 < endpos; ++x0) {
            const uint32_t realx0 = uint32_t(int(x0) + real_upper_scroll_tiles) & (tile_count_x - 1);
            const uint32_t tile_address = realx0 + tile_count_x * y0;
            uint32_t tile = (ctrl & 0x0004) ? bus.read(tilemap)
                                             : bus.read(tilemap + tile_address);
            if (ctrl & 0x0080) {
                // Direct-colour bitmap character mode.  The number array is
                // the low 16 bits of a pixel address and its attribute-array
                // byte supplies the high bits.  The retail menu proves both
                // parts: 0e0ab6, 0e0bb6, ... advance by the exact 0x100 words
                // occupied by a 16x16 RGB1555 tile, and the attribute byte
                // advances from 0x0e to 0x0f when the low address wraps.
                uint16_t ex = (ctrl & 0x0004) ? bus.read(attrmap)
                                              : bus.read(attrmap + tile_address / 2);
                ex = (realx0 & 1) ? uint16_t(ex >> 8) : uint16_t(ex & 0x00ff);
                const uint32_t source = tile | (uint32_t(ex) << 16);
                if (source != 0 || !(ppu & 0x0002)) {
                    const int drawx = int(x0 * tile_w) - (realxscroll & int(tile_w - 1));
                    draw_direct_bitmap_strip(bus, line, source, tile_scanline, drawx,
                                             tile_w, tile_h, attr & 0x0004, attr & 0x0008);
                }
                continue;
            }
            // P_PPU_Enable bit 1 makes character number zero transparent.
            // Retail firmware relies on this for empty text-layer cells: the
            // menu deliberately enables a layer with a zero segment base and
            // a zero-filled number array, so fetching character zero would
            // expose unrelated low RAM as graphics.
            if (tile == 0 && (ppu & 0x0002)) continue;
            uint16_t tileattr = attr;
            uint16_t tilectrl = ctrl;
            if (ppu & 0x0004) {
                uint16_t ex = (tilectrl & 0x0004) ? bus.read(attrmap) : bus.read(attrmap + tile_address / 2);
                ex = (realx0 & 1) ? uint16_t(ex >> 8) : uint16_t(ex & 0x00ff);
                tile |= uint32_t(ex & 0xff) << 16;
            } else if ((tilectrl & 0x0002) == 0) {
                uint16_t ex = (tilectrl & 0x0004) ? bus.read(attrmap) : bus.read(attrmap + tile_address / 2);
                ex = (realx0 & 1) ? uint16_t(ex >> 8) : uint16_t(ex & 0x00ff);
                tileattr = uint16_t((tileattr & ~0x000c) | ((ex >> 2) & 0x000c));
                tileattr = uint16_t((tileattr & ~0x0f00) | ((ex << 8) & 0x0f00));
                tilectrl = uint16_t((tilectrl & ~0x0100) | ((ex << 2) & 0x0100));
            }
            if (!tile && (ppu & 0x0002)) continue;

            uint32_t palette_offset = tile_palette_base(tileattr, nc_bpp);
            if (bus.mmio[base_msb_reg - kMmioBase] & 0x8000) palette_offset |= 0x200;

            const int drawx = int(x0 * tile_w) - (realxscroll & int(tile_w - 1));
            draw_tile_strip(bus, line, tilegfxbase, tile, tile_scanline, drawx,
                            tile_w, tile_h, nc_bpp, bits_per_row, words_per_tile, tileattr & 0x0004,
                            tileattr & 0x0008, palette_offset, tilectrl & 0x0100,
                            global_blend_alpha(bus));
        }
    }

    void draw_sprites(Bus &bus, std::array<uint16_t, W> &line, int priority, int y) {
        const uint16_t extra = bus.mmio[0x7042 - kMmioBase];
        if (!(extra & 0x0001)) return;
        const uint16_t ppu = bus.mmio[0x707f - kMmioBase];
        const uint32_t gfxbase = (ppu & 0x0040)
            ? ((uint32_t(bus.mmio[0x702d - kMmioBase] & 0x07ff) << 16) | bus.mmio[0x7022 - kMmioBase])
            : uint32_t(bus.mmio[0x7022 - kMmioBase]) * 0x40;
        uint32_t sprlimit = (extra >> 8) & 0xff;
        if (sprlimit == 0) sprlimit = 0x100;

        for (uint32_t n = 0; n < sprlimit; ++n) {
            const uint32_t base = n * 4;
            uint32_t tile = bus.sprite_ram[base + 0];
            if (!tile) continue;
            int sx = signed_sprite_coord(bus.sprite_ram[base + 1]);
            int sy = signed_sprite_coord(bus.sprite_ram[base + 2]);
            uint16_t attr = bus.sprite_ram[base + 3];
            if (((attr >> 12) & 3) != uint32_t(priority)) continue;

            const uint32_t tile_h = 8u << ((attr >> 6) & 3);
            const uint32_t tile_w = 8u << ((attr >> 4) & 3);
            if (!(extra & 0x0002)) {
                sx = (W / 2 + sx) - int(tile_w / 2);
                sy = (256 / 2 - sy) - int(tile_h / 2);
            }
            const int firstline = sy;
            const int lastline = sy + int(tile_h) - 1;
            // Real MobiGo 2 evidence from SY's centered Family-B animation:
            // vertical sprite positions are clipped as signed screen-space
            // coordinates. Masking Y to nine bits makes an object moving off
            // the top re-enter from the LCD's bottom, which hardware does not
            // do. The same signed clipping rule is applied horizontally by
            // the sprite-specific draw call below.
            if (y < firstline || y > lastline) continue;
            const int scanline = y - firstline;

            if (ppu & 0x0200) {
                tile |= uint32_t(bus.sprite_ram[base + 0x400] & 0x00ff) << 16;
            } else {
                tile |= uint32_t(bus.sprite_ram[(base / 4) + 0x400] & 0x00ff) << 16;
            }
            if (!(extra & 0x0010)) tile &= 0xffff;

            const uint32_t nc_bpp = ((attr & 3) + 1) << 1;
            const uint32_t bits_per_row = nc_bpp * tile_w / 16;
            uint32_t words_per_tile = bits_per_row * tile_h;
            uint32_t palette_offset = tile_palette_base(attr, nc_bpp);
            if ((bus.mmio[0x703a - kMmioBase] & 1) != 0) palette_offset |= 0x100;
            if (attr & 0x8000) palette_offset |= 0x200;

            if (extra & 0x0010) words_per_tile = 8;
            uint8_t blend_alpha = global_blend_alpha(bus);
            if (extra & 0x0004) {
                const uint16_t extended = (ppu & 0x0200)
                    ? bus.sprite_ram[base + 0x400]
                    : bus.sprite_ram[(base / 4) + 0x400];
                blend_alpha = uint8_t(((extended >> 8) & 0x3f) >> 1);
            }
            draw_tile_strip(bus, line, gfxbase, tile, uint32_t(scanline), sx,
                            tile_w, tile_h, nc_bpp, bits_per_row, words_per_tile, attr & 0x0004,
                            attr & 0x0008, palette_offset, attr & 0x4000,
                            blend_alpha, false);
        }
    }

    bool render_ppu(Bus &bus, bool preserve_background = false) {
        const bool any_layer =
            (bus.mmio[0x7005 - kMmioBase] | bus.mmio[0x700d - kMmioBase] |
             bus.mmio[0x7013 - kMmioBase] | bus.mmio[0x7019 - kMmioBase]) & 0x0008;
        const bool any_sprite = bus.mmio[0x7042 - kMmioBase] & 0x0001;
        // GPL16250VA defines every documented P_PPU_Enable field at bit 3 or
        // above. Bit 0 is not a global PPU enable on this chip.
        if (!any_layer && !any_sprite) return false;

        update_effect_lut(bus);
        std::array<uint16_t, W> line{};
        for (int y = 0; y < H; ++y) {
            if (preserve_background) {
                for (int x = 0; x < W; ++x) line[x] = argb_to_rgb555(pixels[y * W + x]);
            } else {
                line.fill(0);
            }
            for (int pri = 0; pri < 4; ++pri) {
                const bool bottom_up = (bus.mmio[0x707f - kMmioBase] & 0x0008) != 0;
                auto draw_layer = [&](int layer) {
                    switch (layer) {
                    case 0:
                        draw_page(bus, line, pri, y, 0x7020, 0x702b, 0x7010, 0x7012, 0x7013, 0x7014, 0x7015);
                        break;
                    case 1:
                        draw_page(bus, line, pri, y, 0x7021, 0x702c, 0x7016, 0x7018, 0x7019, 0x701a, 0x701b);
                        break;
                    case 2:
                        draw_page(bus, line, pri, y, 0x7023, 0x702e, 0x7000, 0x7004, 0x7005, 0x7006, 0x7007);
                        break;
                    case 3:
                        draw_page(bus, line, pri, y, 0x7024, 0x702f, 0x7008, 0x700c, 0x700d, 0x700e, 0x700f);
                        break;
                    }
                };
                if (bottom_up) {
                    for (int layer = 0; layer < 4; ++layer) draw_layer(layer);
                } else {
                    for (int layer = 3; layer >= 0; --layer) draw_layer(layer);
                }
                draw_sprites(bus, line, pri, y);
            }
            for (int x = 0; x < W; ++x) {
                pixels[y * W + x] = rgb555_effect_lut[line[x] & 0x7fff];
            }
        }
        return true;
    }

    bool render_ppu_to_framebuffer(Bus &bus) {
        const uint32_t fbi = ppu_frame_addr(bus.mmio[0x7078 - kMmioBase], bus.mmio[0x7079 - kMmioBase]);
        const uint32_t fbo = ppu_frame_addr(bus.mmio[0x707a - kMmioBase], bus.mmio[0x707b - kMmioBase]);
        const bool plausible_fbo = fbo != 0 && !(fbo >= kMmioBase && fbo <= kMmioEnd);
        const bool plausible_fbi = fbi != 0 && !(fbi >= kMmioBase && fbi <= kMmioEnd);
        const uint32_t src = plausible_fbi ? fbi : fbo;
        const uint32_t dst = plausible_fbo ? fbo : fbi;

        bus.ppu_go_pending = false;
        bus.ppu_go_due_cycles = 0;
        bus.mmio[0x707c - kMmioBase] = 0x8000;
        if (!plausible_fbo && !plausible_fbi) {
            if (g_log) {
                g_log << "PPU_GO ignored: no plausible framebuffer fbi=0x"
                      << std::hex << fbi << " fbo=0x" << fbo << std::dec << "\n";
            }
            return false;
        }

        pixels.resize(W * H);
        for (uint32_t i = 0; i < W * H; ++i) {
            pixels[i] = rgb565_to_argb(bus.dma_read(src + i));
        }

        const uint32_t page_diag_index = ppu_page_diag_count++;
        if (g_log && (page_diag_index < 16 ||
                      (bus.cycles >= 0x2f000000ull && ppu_page_late_diag_count++ < 24))) {
            const uint16_t ppu = bus.mmio[0x707f - kMmioBase];
            g_log << "PPU GLOBAL ppu=0x" << std::hex << ppu
                  << " bank=0x" << bus.mmio[0x707e - kMmioBase]
                  << " palctrl=0x" << bus.mmio[0x703a - kMmioBase]
                  << " blend=0x" << bus.mmio[0x702a - kMmioBase]
                  << " sprctrl=0x" << bus.mmio[0x7042 - kMmioBase]
                  << " ycmp=0x" << bus.mmio[0x701c - kMmioBase]
                  << "/0x" << bus.mmio[0x701d - kMmioBase]
                  << "/0x" << bus.mmio[0x701e - kMmioBase] << "\n";
            auto log_layer = [&](const char *name, uint32_t base_lsb_reg, uint32_t base_msb_reg,
                                 uint32_t scroll_reg, uint32_t attr_reg, uint32_t ctrl_reg,
                                 uint32_t tilemap_reg, uint32_t attrmap_reg) {
                const uint16_t attr = bus.mmio[attr_reg - kMmioBase];
                const uint16_t ctrl = bus.mmio[ctrl_reg - kMmioBase];
                const uint32_t tilegfxbase = (ppu & 0x0040)
                    ? ((uint32_t(bus.mmio[base_msb_reg - kMmioBase] & 0x07ff) << 16) | bus.mmio[base_lsb_reg - kMmioBase])
                    : uint32_t(bus.mmio[base_lsb_reg - kMmioBase]) * 0x40;
                const uint32_t tile_w = 8u << ((attr >> 4) & 3);
                const uint32_t tile_h = 8u << ((attr >> 6) & 3);
                const uint32_t nc_bpp = ((attr & 3) + 1) << 1;
                const uint32_t bits_per_row = nc_bpp * tile_w / 16;
                const uint32_t words_per_tile = (ppu & 0x0004) ? 8 : bits_per_row * tile_h;
                const uint32_t tilemap = bus.mmio[tilemap_reg - kMmioBase];
                const uint32_t first_tile = bus.read(tilemap & kAddrMask);
                g_log << "PPU " << name
                      << " attr=0x" << attr
                      << " ctrl=0x" << ctrl
                      << " base=0x" << tilegfxbase
                      << " base_regs=0x" << bus.mmio[base_msb_reg - kMmioBase]
                      << "/0x" << bus.mmio[base_lsb_reg - kMmioBase]
                      << " scroll=0x" << bus.mmio[scroll_reg - kMmioBase]
                      << "/0x" << bus.mmio[scroll_reg + 1 - kMmioBase]
                      << " tilemap=0x" << tilemap
                      << " attrmap=0x" << bus.mmio[attrmap_reg - kMmioBase]
                      << " first_tile=0x" << first_tile
                      << " tile=" << std::dec << tile_w << "x" << tile_h
                      << " bpp=" << nc_bpp
                      << " words_per_tile=" << words_per_tile
                      << std::hex << "\n";
                g_log << "PPU " << name << " TILEMAP0";
                for (uint32_t i = 0; i < 32; ++i) {
                    g_log << " " << std::setw(4) << std::setfill('0')
                          << bus.read((tilemap + i) & kAddrMask)
                          << std::setfill(' ');
                }
                g_log << "\n";
                log_dma_words(bus, (std::string("PPU ") + name + " TILE0GFX").c_str(),
                              tilegfxbase + first_tile * words_per_tile, 32);
            };
            log_layer("L0", 0x7020, 0x702b, 0x7010, 0x7012, 0x7013, 0x7014, 0x7015);
            log_layer("L1", 0x7021, 0x702c, 0x7016, 0x7018, 0x7019, 0x701a, 0x701b);
            log_layer("L2", 0x7023, 0x702e, 0x7000, 0x7004, 0x7005, 0x7006, 0x7007);
            log_layer("L3", 0x7024, 0x702f, 0x7008, 0x700c, 0x700d, 0x700e, 0x700f);
            const uint32_t tilemap = bus.mmio[0x7006 - kMmioBase];
            for (uint32_t row : {4u, 8u, 12u}) {
                g_log << "PPU L2 TILEMAP row=" << std::dec << row << std::hex;
                for (uint32_t i = 0; i < 32; ++i) {
                    g_log << " " << std::setw(4) << std::setfill('0')
                          << bus.read((tilemap + row * 32 + i) & kAddrMask)
                          << std::setfill(' ');
                }
                g_log << "\n";
            }
            log_word_slice("PPU ROWSCROLL", bus.rowscroll_ram, 0, 32);
            log_word_slice("PPU ROWZOOM", bus.rowzoom_ram, 0, 32);
            log_word_slice("PPU TX3LO", bus.tx3_transform_ram, 0, 32);
            log_word_slice("PPU TX3HI", bus.tx3_transform_ram, 0x100, 32);
            static constexpr std::array<uint32_t, 8> palette_offsets{0, 64, 128, 192, 256, 320, 384, 448};
            for (uint32_t base : palette_offsets) {
                g_log << "PPU PAGE3 PAL[" << std::hex << base << "]";
                for (uint32_t i = 0; i < 8; ++i) {
                    g_log << " " << std::setw(4) << std::setfill('0')
                          << bus.palette_ram[(base + i) & 0x0fff]
                          << std::setfill(' ');
                }
                g_log << "\n";
            }
        }

        const bool rendered = render_ppu(bus, true);
        const bool rgb565 = (bus.mmio[0x707f - kMmioBase] & 0x0100) == 0;
        for (uint32_t i = 0; i < W * H; ++i) {
            const uint16_t out = rgb565 ? argb_to_rgb565(pixels[i]) : argb_to_rgb555(pixels[i]);
            bus.dma_write(dst + i, out);
        }
        bus.last_ppu_framebuffer_base = dst;
        bus.ppu_framebuffer_valid = true;
        if (g_log) {
            g_log << "PPU_GO render fbi=0x" << std::hex << fbi
                  << " fbo=0x" << fbo
                  << " src=0x" << src
                  << " dst=0x" << dst
                  << " ppu=0x" << bus.mmio[0x707f - kMmioBase]
                  << " status=0x" << bus.mmio[0x7063 - kMmioBase]
                  << " enable=0x" << bus.mmio[0x7062 - kMmioBase]
                  << " rendered=" << (rendered ? 1 : 0)
                  << std::dec << "\n";
        }
        return rendered;
    }

    void compose(Bus &bus, const Cpu &cpu, bool allow_latched_frame = true) {
        const uint16_t ppu = bus.mmio[0x707f - kMmioBase];
        const bool frame_base_mode = (ppu & 0x0080) != 0;
        const uint32_t fbi_raw = ppu_frame_addr(bus.mmio[0x7078 - kMmioBase], bus.mmio[0x7079 - kMmioBase]);
        const uint32_t fbo_raw = ppu_frame_addr(bus.mmio[0x707a - kMmioBase], bus.mmio[0x707b - kMmioBase]);
        const uint32_t fbi = fbi_raw;
        const uint32_t fbo = fbo_raw;
        const bool plausible_fbi = fbi_raw != 0 && !(fbi_raw >= kMmioBase && fbi_raw <= kMmioEnd);
        const bool plausible_fbo = fbo_raw != 0 && !(fbo_raw >= kMmioBase && fbo_raw <= kMmioEnd);
        const bool ppu_rendered = !frame_base_mode && !plausible_fbo && render_ppu(bus);
        const uint32_t latched_fbi = (allow_latched_frame && bus.last_framebuffer_valid)
            ? bus.last_framebuffer_base : 0;
        // The retail loader hands an MBA a live system display. On hardware,
        // disabling both inherited interrupt classes at entry prevents that
        // display service from accepting the application's new scanout state;
        // the LCD remains on its previously latched frame. Keep standalone
        // SDK-style programs unaffected because they own the display outright.
        if (bus.mba_application_active && !cpu.enable_irq && !cpu.enable_fiq) {
            if (g_log && mba_scanout_stall_log_count++ < 8) {
                g_log << "MBA SCANOUT STALLED: IRQ and FIQ are both disabled"
                      << " fbi=0x" << std::hex << fbi
                      << " ppu=0x" << ppu << std::dec << "\n";
            }
            return;
        }
        if (frame_base_mode && plausible_fbi) {
            pixels.resize(W * H);
            for (uint32_t i = 0; i < W * H; ++i) {
                const uint16_t v = bus.dma_read(fbi + i);
                pixels[i] = rgb565_to_argb(v);
            }
            // The verified SDK identifies FBI as the LCD scanout buffer.
            // Frame-base PPU jobs independently composite FBI plus the page
            // and sprite engines into FBO; displaying the most recently
            // written FBO here would bypass the firmware's buffer rotation.
            if (g_log) {
                g_log << "FRAME SELECT base=0x" << std::hex << fbi
                      << " fbi=0x" << fbi << " fbo=0x" << fbo
                      << " latch=0x" << latched_fbi
                      << " ppu_fb=0x" << bus.last_ppu_framebuffer_base
                      << " ppu_fb_valid=" << (bus.ppu_framebuffer_valid ? 1 : 0)
                      << " (fbi)"
                      << std::dec << "\n";
            }
            bus.last_framebuffer_base = fbi;
            bus.last_framebuffer_valid = true;
        } else if (latched_fbi != 0) {
            // The firmware clears the live FBI registers from its video IRQ
            // path. For headless frame dumps, render the last completed
            // full-screen DMA target instead of falling through to debug noise.
            const uint32_t frame_words = W * H;
            for (uint32_t i = 0; i < frame_words; ++i) {
                const uint16_t v = bus.dma_read(latched_fbi + i);
                pixels[i] = rgb565_to_argb(v);
            }
        } else if (!ppu_rendered) {
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    uint32_t argb;
                    if (plausible_fbo) {
                        const uint16_t v = bus.dma_read(fbo + y * W + x);
                        argb = rgb565_to_argb(v);
                    } else {
                        argb = 0xff000000u;
                    }
                    pixels[y * W + x] = argb;
                }
            }
        }
    }

    void render(Bus &bus, const Cpu &cpu) {
        compose(bus, cpu);
        SDL_UpdateTexture(tex, nullptr, pixels.data(), W * sizeof(uint32_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
    }

    void save_bmp(const std::filesystem::path &path) {
        std::ofstream out(path, std::ios::binary);
        if (!out) die("failed to create frame dump " + path.string());
        auto w8 = [&](uint8_t v) { out.put(char(v)); };
        auto w16 = [&](uint16_t v) { w8(uint8_t(v)); w8(uint8_t(v >> 8)); };
        auto w32 = [&](uint32_t v) {
            w8(uint8_t(v)); w8(uint8_t(v >> 8)); w8(uint8_t(v >> 16)); w8(uint8_t(v >> 24));
        };
        const uint32_t pixel_bytes = W * H * 4;
        const uint32_t file_bytes = 14 + 40 + pixel_bytes;
        w8('B'); w8('M');
        w32(file_bytes);
        w16(0); w16(0);
        w32(14 + 40);
        w32(40);
        w32(W);
        w32(uint32_t(-H)); // top-down DIB
        w16(1);
        w16(32);
        w32(0);
        w32(pixel_bytes);
        w32(2835); w32(2835);
        w32(0); w32(0);
        for (uint32_t argb : pixels) {
            w8(uint8_t(argb));
            w8(uint8_t(argb >> 8));
            w8(uint8_t(argb >> 16));
            w8(uint8_t(argb >> 24));
        }
        if (!out) die("failed while writing frame dump " + path.string());
    }
};

} // namespace mobigo
