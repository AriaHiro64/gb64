

#include <ultra64.h>
#include "graphics.h"
#include "../boot.h"
#include "debug_out.h"
#include "../memory.h"
#include "gameboy.h"

Gfx* gCurrentScreenDL;

union {
    u8 buffer[GB_SCREEN_W * GB_SCREEN_H];
    long long unusedAlign;
} gScreenBuffer;

Gfx gDrawScreen[0x100] = {gsSPEndDisplayList()};

u16 gScreenPalette[MAX_PALLETE_SIZE];

#define COPY_SCREEN_STRIP(dl, y, maxY, scale, scaleInv)                     \
    gDPLoadTextureTile(                                                     \
        dl,                                                                 \
        (int)gScreenBuffer.buffer + y * GB_SCREEN_W,                        \
        G_IM_FMT_CI, G_IM_SIZ_8b,                                           \
        GB_SCREEN_W, maxY - y,                                              \
        0, 0,                                                               \
        GB_SCREEN_W - 1, (maxY - y) - 1,                                    \
        0,                                                                  \
        G_TX_CLAMP, G_TX_CLAMP,                                             \
        G_TX_NOMASK, G_TX_NOMASK,                                           \
        G_TX_NOLOD, G_TX_NOLOD                                              \
    );                                                                      \
    gSPTextureRectangle(                                                    \
        dl,                                                                 \
        (SCREEN_WD << 1) - (((scale) * GB_SCREEN_W) >> 15),                 \
        (SCREEN_HT << 1) - (((scale) * (GB_SCREEN_H / 2 - y)) >> 14),       \
        (SCREEN_WD << 1) + (((scale) * GB_SCREEN_W) >> 15),                 \
        (SCREEN_HT << 1) - (((scale) * (GB_SCREEN_H / 2 - maxY)) >> 14),    \
        G_TX_RENDERTILE,                                                    \
        0, 0,                                                               \
        (scaleInv >> 6), (scaleInv >> 6)                                    \
    )


#define WRITE_PIXEL(pixelIndex, x, targetMemory, spriteBuffer, maxX, priority, palleteOffset)    \
    if (priority <= 0 && (priority < 0 || (spriteBuffer[x] && (!(spriteBuffer[x] & SPRITE_FLAGS_PRIORITY) || !pixelIndex)))) \
        *targetMemory = (spriteBuffer[x] & ~SPRITE_FLAGS_PRIORITY) + OBJ_PALETTE_INDEX_START + palleteOffset;                \
    else                                                                                        \
        *targetMemory = pixelIndex + palleteOffset;                            \
    ++x;                                                                                        \
    ++targetMemory;                                                                             \
    if (x == maxX)                                                                       \
        break;

#define WRITE_SPRITE_PIXEL(spriteRow, paletteOffset, x, targetMemory, pixel)                                     \
    if (READ_PIXEL_INDEX(spriteRow, pixel) && *targetMemory == 0)                                                                                       \
        *targetMemory = READ_PIXEL_INDEX(spriteRow, pixel) + paletteOffset;                     \
    ++x;                                                                                        \
    ++targetMemory;                                                                             \
    if (x == GB_SCREEN_W)                                                                       \
        break;

int compareSprites(struct Sprite a, struct Sprite b)
{
    return a.x - b.x;
}

void sortSpritesRecursive(struct Sprite* spriteArray, struct Sprite* workingMemory, int count)
{
    if (count == 2)
    {
        // Only swap if not already sorted
        if (compareSprites(spriteArray[0], spriteArray[1]) > 0)
        {
            struct Sprite tmp = spriteArray[0];
            spriteArray[0] = spriteArray[1];
            spriteArray[1] = tmp;
        }
    }
    else if (count > 2)
    {
        int aIndex;
        int bIndex;
        int outputIndex;
        int midpoint = count / 2;
        sortSpritesRecursive(spriteArray, workingMemory, midpoint);
        sortSpritesRecursive(spriteArray + midpoint, workingMemory + midpoint, count - midpoint);

        outputIndex = 0;
        aIndex = 0;
        bIndex = midpoint;
        
        while (aIndex < midpoint && bIndex < count)
        {
            if (compareSprites(spriteArray[aIndex], spriteArray[bIndex]) <= 0)
            {
                workingMemory[outputIndex++] = spriteArray[aIndex++];
            }
            else
            {
                workingMemory[outputIndex++] = spriteArray[bIndex++];
            }
        }

        while (aIndex < midpoint)
        {
            workingMemory[outputIndex++] = spriteArray[aIndex++];
        }

        while (bIndex < count)
        {
            workingMemory[outputIndex++] = spriteArray[bIndex++];
        }

        for (outputIndex = 0; outputIndex < count; ++outputIndex)
        {
            spriteArray[outputIndex] = workingMemory[outputIndex];
        }
    }
}

void sortSprites(struct Sprite* array, int arrayLength)
{
    struct Sprite workingMemory[SPRITE_COUNT];
    sortSpritesRecursive(array, workingMemory, arrayLength);
}

void prepareSprites(struct Sprite* inputSprites, struct Sprite* sortedSprites, int *spriteCount, int sort)
{
    int currentOutput;
    int currentInput;

    currentOutput = 0;

    for (currentInput = 0; currentInput < SPRITE_COUNT; ++currentInput)
    {
        if (inputSprites[currentInput].y > 0 && inputSprites[currentInput].y < 160 &&
            inputSprites[currentInput].x < 168)
        {
            sortedSprites[currentOutput++] = inputSprites[currentInput];
        }
    }
    if (sort)
    {
        sortSprites(sortedSprites, currentOutput);
    }
    *spriteCount = currentOutput;
}

void applyGrayscalePallete(struct GraphicsState* state) {
    memCopy(&gScreenPalette[state->palleteWriteIndex], gGameboy.memory.vram.colorPalettes, sizeof(u16) * PALETTE_COUNT);
}

static long gScreenScales[ScreenScaleSettingCount] = {
    0x10000,
    0x14000,
    0x18000,
};

static long gInvScreenScales[ScreenScaleSettingCount] = {
    0x10000,
    0x0CCCC,
    0x0AAAA,
};

void beginScreenDisplayList(struct GameboyGraphicsSettings* settings, Gfx* dl)
{
    gCurrentScreenDL = dl;
    
    gDPPipeSync(gCurrentScreenDL++);
    gDPSetCycleType(gCurrentScreenDL++, G_CYC_1CYCLE);
    gDPSetRenderMode(gCurrentScreenDL++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetTextureFilter(gCurrentScreenDL++, settings->smooth ? G_TF_BILERP : G_TF_POINT);
    gDPSetTexturePersp(gCurrentScreenDL++, G_TP_NONE);
    gDPSetCombineMode(gCurrentScreenDL++, G_CC_BLENDRGBA, G_CC_BLENDRGBA);
    gDPSetPrimColor(gCurrentScreenDL++, 0, 0, 255, 255, 255, 255);
    gDPSetTextureLUT(gCurrentScreenDL++, G_TT_RGBA16);
    gDPLoadTLUT_pal256(gCurrentScreenDL++, gScreenPalette);
}

void initGraphicsState(
    struct Memory* memory,
    struct GraphicsState* state,
    struct GameboyGraphicsSettings* settings,
    int gbc
)
{
    if (READ_REGISTER_DIRECT(memory, REG_LCDC) & LCDC_OBJ_ENABLE)
    {
        prepareSprites(memory->misc.sprites, state->sortedSprites, &state->spriteCount, !gbc);
    }
    else
    {
        state->spriteCount = 0;
    }

    gPalleteDirty = 1;
    state->settings = *settings;
    state->gbc = gbc;
    state->row = 0;
    state->lastRenderedRow = 0;
    state->winY = 0;
    state->palleteReadIndex = 0;
    state->palleteWriteIndex = 0;

    beginScreenDisplayList(&state->settings, gDrawScreen);
}

void renderScreenBlock(struct GraphicsState* state)
{
    if (state->lastRenderedRow != state->row)
    {
        COPY_SCREEN_STRIP(
            gCurrentScreenDL++, 
            state->lastRenderedRow, 
            state->row, 
            gScreenScales[state->settings.scaleSetting], 
            gInvScreenScales[state->settings.scaleSetting]
        );
        state->lastRenderedRow = state->row;
    }
}

void prepareGraphicsPallete(struct GraphicsState* state)
{
    if (gPalleteDirty)
    {
        gPalleteDirty = 0;

        if (state->palleteWriteIndex >= MAX_PALLETE_SIZE)
        {
            return;
        }

        if (state->palleteWriteIndex - state->palleteReadIndex >= 256)
        {
            state->palleteReadIndex = state->palleteWriteIndex;
            renderScreenBlock(state);
            gDPLoadTLUT_pal256(gCurrentScreenDL++, gScreenPalette + state->palleteReadIndex);
        }

        if (state->gbc)
        {
            int i;

            for (i = 0; i < PALETTE_COUNT; ++i)
            {
                gScreenPalette[i + state->palleteWriteIndex] = GBC_TO_N64_COLOR(gGameboy.memory.vram.colorPalettes[i]);
            }
        }
        else
        {
            applyGrayscalePallete(state);
        }

        state->palleteWriteIndex += PALETTE_COUNT;
    }
}

void finishScreen(struct GraphicsState* state)
{
    renderScreenBlock(state);
    gSPEndDisplayList(gCurrentScreenDL++);
}

void renderSprites(struct Memory* memory, struct GraphicsState* state)
{
    int currentSpriteIndex;
    int renderedSprites = 0;
    int spriteHeight = (READ_REGISTER_DIRECT(memory, REG_LCDC) & LCDC_OBJ_SIZE) ? SPRITE_BASE_HEIGHT * 2 : SPRITE_BASE_HEIGHT;
    u8* targetMemory = state->spriteIndexBuffer;
    zeroMemory(targetMemory, GB_SCREEN_W);

    currentSpriteIndex = 0;
    renderedSprites = 0;

    for (currentSpriteIndex = 0; currentSpriteIndex < state->spriteCount && renderedSprites < 10; ++currentSpriteIndex)
    {
        struct Sprite currentSprite = state->sortedSprites[currentSpriteIndex];
        int sourceX = 0;

        if (
            currentSprite.y - SPRITE_Y_OFFSET > state->row ||
            currentSprite.y - SPRITE_Y_OFFSET + spriteHeight <= state->row

        )
        {
            // sprite not on this row
            continue;
        }
        
        int x = ((int)currentSprite.x - SPRITE_WIDTH);
        ++renderedSprites;


        if (x < 0)
        {
            sourceX = -x;
            targetMemory = state->spriteIndexBuffer;
        }
        else 
        {
            sourceX = 0;
            targetMemory = state->spriteIndexBuffer + x;
        }
        
        if (sourceX >= SPRITE_WIDTH)
        {
            continue;
        }

        u16 paletteIndex = state->gbc ? 
            (currentSprite.flags & SPRITE_FLAGS_GBC_PALETTE) << 2 : 
            ((currentSprite.flags & SPRITE_FLAGS_DMA_PALETTE) >> 2);

        if (currentSprite.flags & SPRITE_FLAGS_PRIORITY)
        {
            paletteIndex |= SPRITE_FLAGS_PRIORITY;
        }

        int yIndex = state->row - (currentSprite.y - SPRITE_Y_OFFSET);

        if (currentSprite.flags & SPRITE_FLAGS_Y_FLIP)
        {
            yIndex = (spriteHeight - 1) - yIndex;
        }

        u16 spriteRow;
        struct Tile* tiles;

        if (state->gbc && (currentSprite.flags & SPRITE_FLAGS_VRAM_BANK))
        {
            tiles = memory->vram.gbcTiles;
        }
        else
        {
            tiles = memory->vram.tiles;
        }
        
        spriteRow = tiles[currentSprite.tile].rows[yIndex];

        switch (sourceX + ((currentSprite.flags & SPRITE_FLAGS_X_FLIP) ? 8 : 0))
        {
            case 0:
                WRITE_SPRITE_PIXEL(spriteRow, paletteIndex, x, targetMemory, 0);
            case 1:
                WRITE_SPRITE_PIXEL(spriteRow, paletteIndex, x, targetMemory, 1);
            case 2:
                WRITE_SPRITE_PIXEL(spriteRow, paletteIndex, x, targetMemory, 2);
            case 3:
                WRITE_SPRITE_PIXEL(spriteRow, paletteIndex, x, targetMemory, 3);
            case 4:
                WRITE_SPRITE_PIXEL(spriteRow, paletteIndex, x, targetMemory, 4);
            case 5:
                WRITE_SPRITE_PIXEL(spriteRow, paletteIndex, x, targetMemory, 5);
            case 6:
                WRITE_SPRITE_PIXEL(spriteRow, paletteIndex, x, targetMemory, 6);
            case 7:
                WRITE_SPRITE_PIXEL(spriteRow, paletteIndex, x, targetMemory, 7);
                break;
            case 8:
                WRITE_SPRITE_PIXEL(spriteRow, paletteIndex, x, targetMemory, 7);
            case 9:
                WRITE_SPRITE_PIXEL(spriteRow, paletteIndex, x, targetMemory, 6);
            case 10:
                WRITE_SPRITE_PIXEL(spriteRow, paletteIndex, x, targetMemory, 5);
            case 11:
                WRITE_SPRITE_PIXEL(spriteRow, paletteIndex, x, targetMemory, 4);
            case 12:
                WRITE_SPRITE_PIXEL(spriteRow, paletteIndex, x, targetMemory, 3);
            case 13:
                WRITE_SPRITE_PIXEL(spriteRow, paletteIndex, x, targetMemory, 2);
            case 14:
                WRITE_SPRITE_PIXEL(spriteRow, paletteIndex, x, targetMemory, 1);
            case 15:
                WRITE_SPRITE_PIXEL(spriteRow, paletteIndex, x, targetMemory, 0);
                break;
        }
    }
}

void renderPixelRow(
    struct Memory* memory,
    struct GraphicsState* state,
    u16* memoryBuffer
)
{
    int x;
    int offsetX;
    int bgX;
    int bgY;
    int wX;
    int wY;
    int usingWindow;
    int tilemapRow;
    int tileInfo;
    u32 tileIndex;
    int tilemapIndex;
    u8* targetMemory;
    u16 spriteRow;
    struct Tile* tileSource;
    int dataSelect;
    int lcdcReg;
    int maxX = GB_SCREEN_W;
    int priority;

    if (state->row - state->lastRenderedRow >= GB_RENDER_STRIP_HEIGHT)
    {
        renderScreenBlock(state);
    }

    lcdcReg = READ_REGISTER_DIRECT(memory, REG_LCDC);

    prepareGraphicsPallete(state);
    // is sprites are disabled then this just clears the
    // sprite index memory
    renderSprites(memory, state);

    targetMemory = gScreenBuffer.buffer + state->row * GB_SCREEN_W;
    offsetX = READ_REGISTER_DIRECT(memory, REG_SCX);
    bgY = (state->row + READ_REGISTER_DIRECT(memory, REG_SCY)) & 0xFF;

    tilemapRow = (bgY >> 3) * TILEMAP_W;

    tilemapRow += (lcdcReg & LCDC_BG_TILE_MAP) ? 1024 : 0;
    dataSelect = lcdcReg & LCDC_BG_TILE_DATA;
    tileSource = memory->vram.tiles;
    tileInfo = 0;
    
    wX = READ_REGISTER_DIRECT(memory, REG_WX) - WINDOW_X_OFFSET;
    wY = READ_REGISTER_DIRECT(memory, REG_WY);

    if (!(lcdcReg & LCDC_WIN_E) || 
        wX >= GB_SCREEN_W || 
        wY > state->row)
    {
        wX = GB_SCREEN_W;
    }
    else
    {
        maxX = wX;
    }

    usingWindow = 0;

    for (x = 0; x < GB_SCREEN_W;)
    {
        if (x >= wX)
        {
            offsetX = x - wX;
            bgY = state->winY;
            tilemapRow = (bgY >> 3) * TILEMAP_W;
            tilemapRow += (lcdcReg & LCDC_WIN_TILE_MAP) ? 1024 : 0;
            wX = GB_SCREEN_W;
            usingWindow = 1;
            ++state->winY;
        }

        bgX = (x + offsetX) & 0xFF;
        tilemapIndex = tilemapRow + (bgX >> 3);

        if (state->gbc)
        {
            tileInfo = memory->vram.tilemap0Atts[tilemapIndex];
            tileSource = (tileInfo & TILE_ATTR_VRAM_BANK) ? memory->vram.gbcTiles : memory->vram.tiles;

            if (lcdcReg & LCDC_BG_ENABLE)
            {
                priority = (tileInfo & TILE_ATTR_PRIORITY) ? 1 : 0;
            }
            else
            {
                priority = -1;
            }
        }
        else
        {
            priority = 0;
        }

        tileIndex = memory->vram.tilemap0[tilemapIndex];
        
        if (!dataSelect)
        {
            tileIndex = ((tileIndex + 0x80) & 0xFF) + 0x80;
        }
        
        if (state->gbc || usingWindow || (lcdcReg & LCDC_BG_ENABLE))
        {
            spriteRow = tileSource[tileIndex].rows[
                (tileInfo & TILE_ATTR_V_FLIP) ?
                    (7 - (bgY & 0x7)) :
                    (bgY & 0x7)
            ];
        }
        else
        {
            spriteRow = 0;
        }

        u8 pixelIndex;
        u8 pixelIndexOffset = (tileInfo & TILE_ATTR_PALETTE) << 2;
        u16 palleteOffset = state->palleteWriteIndex - state->palleteReadIndex - PALETTE_COUNT;
        
        // A bit of a hack here
        // set the h flip flag to bit 3 and put the 
        // case range 8-15 to render the tile flipped
        switch ((bgX & 0x7) | ((tileInfo & TILE_ATTR_H_FLIP) ? 8 : 0))
        {
            case 0:
                pixelIndex = READ_PIXEL_INDEX(spriteRow, 0) + pixelIndexOffset;
                WRITE_PIXEL(pixelIndex, x, targetMemory, state->spriteIndexBuffer, maxX, priority, palleteOffset);
            case 1:
                pixelIndex = READ_PIXEL_INDEX(spriteRow, 1) + pixelIndexOffset;
                WRITE_PIXEL(pixelIndex, x, targetMemory, state->spriteIndexBuffer, maxX, priority, palleteOffset);
            case 2:
                pixelIndex = READ_PIXEL_INDEX(spriteRow, 2) + pixelIndexOffset;
                WRITE_PIXEL(pixelIndex, x, targetMemory, state->spriteIndexBuffer, maxX, priority, palleteOffset);
            case 3:
                pixelIndex = READ_PIXEL_INDEX(spriteRow, 3) + pixelIndexOffset;
                WRITE_PIXEL(pixelIndex, x, targetMemory, state->spriteIndexBuffer, maxX, priority, palleteOffset);
            case 4:
                pixelIndex = READ_PIXEL_INDEX(spriteRow, 4) + pixelIndexOffset;
                WRITE_PIXEL(pixelIndex, x, targetMemory, state->spriteIndexBuffer, maxX, priority, palleteOffset);
            case 5:
                pixelIndex = READ_PIXEL_INDEX(spriteRow, 5) + pixelIndexOffset;
                WRITE_PIXEL(pixelIndex, x, targetMemory, state->spriteIndexBuffer, maxX, priority, palleteOffset);
            case 6:
                pixelIndex = READ_PIXEL_INDEX(spriteRow, 6) + pixelIndexOffset;
                WRITE_PIXEL(pixelIndex, x, targetMemory, state->spriteIndexBuffer, maxX, priority, palleteOffset);
            case 7:
                pixelIndex = READ_PIXEL_INDEX(spriteRow, 7) + pixelIndexOffset;
                WRITE_PIXEL(pixelIndex, x, targetMemory, state->spriteIndexBuffer, maxX, priority, palleteOffset);
                break;
            case 8:
                pixelIndex = READ_PIXEL_INDEX(spriteRow, 7) + pixelIndexOffset;
                WRITE_PIXEL(pixelIndex, x, targetMemory, state->spriteIndexBuffer, maxX, priority, palleteOffset);
            case 9:
                pixelIndex = READ_PIXEL_INDEX(spriteRow, 6) + pixelIndexOffset;
                WRITE_PIXEL(pixelIndex, x, targetMemory, state->spriteIndexBuffer, maxX, priority, palleteOffset);
            case 10:
                pixelIndex = READ_PIXEL_INDEX(spriteRow, 5) + pixelIndexOffset;
                WRITE_PIXEL(pixelIndex, x, targetMemory, state->spriteIndexBuffer, maxX, priority, palleteOffset);
            case 11:
                pixelIndex = READ_PIXEL_INDEX(spriteRow, 4) + pixelIndexOffset;
                WRITE_PIXEL(pixelIndex, x, targetMemory, state->spriteIndexBuffer, maxX, priority, palleteOffset);
            case 12:
                pixelIndex = READ_PIXEL_INDEX(spriteRow, 3) + pixelIndexOffset;
                WRITE_PIXEL(pixelIndex, x, targetMemory, state->spriteIndexBuffer, maxX, priority, palleteOffset);
            case 13:
                pixelIndex = READ_PIXEL_INDEX(spriteRow, 2) + pixelIndexOffset;
                WRITE_PIXEL(pixelIndex, x, targetMemory, state->spriteIndexBuffer, maxX, priority, palleteOffset);
            case 14:
                pixelIndex = READ_PIXEL_INDEX(spriteRow, 1) + pixelIndexOffset;
                WRITE_PIXEL(pixelIndex, x, targetMemory, state->spriteIndexBuffer, maxX, priority, palleteOffset);
            case 15:
                pixelIndex = READ_PIXEL_INDEX(spriteRow, 0) + pixelIndexOffset;
                WRITE_PIXEL(pixelIndex, x, targetMemory, state->spriteIndexBuffer, maxX, priority, palleteOffset);
                break;
        }
    }
}