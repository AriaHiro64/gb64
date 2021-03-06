
#include "save.h"
#include "gameboy.h"
#include "../memory.h"
#include "debug_out.h"
#include "upgrade.h"
#include "../lzfse/lzfse.h"
#include "../lzfse/lzfse_internal.h"
#include <string.h>

struct SaveTypeSetting gSaveTypeSetting = {
    SAVE_HEADER_VALUE,
    SaveTypeFlash,
};

extern OSMesgQueue     dmaMessageQ;
extern OSMesg          dmaMessageBuf;
extern OSPiHandle	   *handler;
extern OSIoMesg        dmaIOMessageBuf;

#define FLASH_BLOCK_SIZE    0x80

#define ALIGN_FLASH_OFFSET(offset) (((offset) + 0x7F) & ~0x7F)

#define SRAM_ADDR   0x08000000

char gTmpSaveBuffer[FLASH_BLOCK_SIZE];
char gUncompressedMemory[128 * 1024];
char gCompressedMemory[32 * 1024];

union lzfse_memory {
    lzfse_decoder_state decoder;
    lzfse_encoder_state encoder;
};

union lzfse_memory __attribute__((aligned(8))) gLZFSEMemory;

SaveReadCallback gSaveReadCallback;
SaveWriteCallback gSaveWriteCallback;

int writeToUncompressedMemory(void *from, int sramOffset, int length) {
    memCopy(gUncompressedMemory + sramOffset, from, length);
    return 0;
}

int readFromUncompressedData(void *target, int sramOffset, int length) {
    memCopy(target, gUncompressedMemory + sramOffset, length);
    return 0;
}

int compressedChecksum(int length) {
    int result = 0;
    int i;
    for (i = 0; i < length; ++i) {
        result += gCompressedMemory[i];
    }

    return result;
}

int getSaveTypeSize(enum SaveType type)
{
    switch (type)
    {
        case SaveTypeFlash:
            return 128 * 1024;
        case SaveTypeSRAM:
            return 32 * 1024;
        case SaveTypeSRAM3X:
            return 3 * 32 * 1024;
    }

    return 0;
}

#define SRAM_START_ADDR  0x08000000 
#define SRAM_SIZE        0x8000 
#define SRAM_latency     0x5 
#define SRAM_pulse       0x0c 
#define SRAM_pageSize    0xd 
#define SRAM_relDuration 0x2

OSPiHandle gSramHandle;

OSPiHandle * osSramInit(void)
{
    if (gSramHandle.baseAddress == PHYS_TO_K1(SRAM_START_ADDR))
            return(&gSramHandle);

    /* Fill basic information */

    gSramHandle.type = 3;
    gSramHandle.baseAddress = PHYS_TO_K1(SRAM_START_ADDR);

    /* Get Domain parameters */

    gSramHandle.latency = (u8)SRAM_latency;
    gSramHandle.pulse = (u8)SRAM_pulse;
    gSramHandle.pageSize = (u8)SRAM_pageSize;
    gSramHandle.relDuration = (u8)SRAM_relDuration;
    gSramHandle.domain = PI_DOMAIN2;
    gSramHandle.speed = 0;

    /* TODO gSramHandle.speed = */

    zeroMemory(&(gSramHandle.transferInfo), sizeof(gSramHandle.transferInfo));

    /*
        * Put the gSramHandle onto PiTable
        */

    OSIntMask saveMask = osGetIntMask();
    osSetIntMask(OS_IM_NONE);
    gSramHandle.next = __osPiTable;
    __osPiTable = &gSramHandle;
    osSetIntMask(saveMask);
    return(&gSramHandle);
}

int loadFromSRAM(void* target, int sramOffset, int length)
{
    OSIoMesg dmaIoMesgBuf;
    int saveSize = getSaveTypeSize(gSaveTypeSetting.saveType);

    dmaIoMesgBuf.hdr.pri = OS_MESG_PRI_HIGH;
    dmaIoMesgBuf.hdr.retQueue = &dmaMessageQ;
    dmaIoMesgBuf.dramAddr = target;
    dmaIoMesgBuf.devAddr = SRAM_ADDR + sramOffset;
    dmaIoMesgBuf.size = length;

    osInvalDCache(target, length);
    if (osEPiStartDma(&gSramHandle, &dmaIoMesgBuf, OS_READ) == -1)
    {
        return -1;
    }
    (void) osRecvMesg(&dmaMessageQ, NULL, OS_MESG_BLOCK);

    return 0;
}

int saveToSRAM(void* target, int sramOffset, int length)
{
	OSIoMesg dmaIoMesgBuf;

    dmaIoMesgBuf.hdr.pri = OS_MESG_PRI_HIGH;
    dmaIoMesgBuf.hdr.retQueue = &dmaMessageQ;
    dmaIoMesgBuf.dramAddr = target;
    dmaIoMesgBuf.devAddr = SRAM_ADDR + sramOffset;
    dmaIoMesgBuf.size = length;

    osWritebackDCache(target, length);
    if (osEPiStartDma(&gSramHandle, &dmaIoMesgBuf, OS_WRITE) == -1)
    {
        return -1;
    }
	(void) osRecvMesg(&dmaMessageQ, NULL, OS_MESG_BLOCK);
    return 0;
}

int loadFromFlash(void* target, int sramOffset, int length)
{
    OSIoMesg dmaIoMesgBuf;

    if (length / FLASH_BLOCK_SIZE > 0)
    {
        osInvalDCache(target, (length & ~0x7F));
        if (osFlashReadArray(
                &dmaIoMesgBuf, 
                OS_MESG_PRI_NORMAL, 
                sramOffset / FLASH_BLOCK_SIZE,
                target,
                length / FLASH_BLOCK_SIZE,
                &dmaMessageQ
            ) == -1) 
        {
            return -1;
        }
        (void) osRecvMesg(&dmaMessageQ, NULL, OS_MESG_BLOCK);
    }

    if (length % FLASH_BLOCK_SIZE != 0)
    {
        osInvalDCache(gTmpSaveBuffer, FLASH_BLOCK_SIZE);
        if (osFlashReadArray(
                &dmaIoMesgBuf, 
                OS_MESG_PRI_NORMAL, 
                sramOffset / FLASH_BLOCK_SIZE + length / FLASH_BLOCK_SIZE,
                gTmpSaveBuffer,
                1,
                &dmaMessageQ
            )) 
        {
            return -1;
        }
        (void) osRecvMesg(&dmaMessageQ, NULL, OS_MESG_BLOCK);
        memCopy((char*)target + (length & ~0x7F), gTmpSaveBuffer, length % FLASH_BLOCK_SIZE);
    }

    return 0;
}

int saveToFlash(void *from, int sramOffset, int length)
{
    while (length > 0)
    {
        OSIoMesg dmaIoMesgBuf;
        int pageNumber = sramOffset / FLASH_BLOCK_SIZE;

        if (length < FLASH_BLOCK_SIZE)
        {
            memCopy(gTmpSaveBuffer, from, length);
            zeroMemory(gTmpSaveBuffer + length, FLASH_BLOCK_SIZE - length);
            from = gTmpSaveBuffer;
        }

        osWritebackDCache(from, FLASH_BLOCK_SIZE);
        if (osFlashWriteBuffer(
                &dmaIoMesgBuf, 
                OS_MESG_PRI_NORMAL,
                from,
                &dmaMessageQ
            ) == -1
        ) 
        {
            return -1;
        }
        (void) osRecvMesg(&dmaMessageQ, NULL, OS_MESG_BLOCK);
        if (osFlashWriteArray(pageNumber) == -1)
        {
            return -1;
        }

        from = (char*)from + FLASH_BLOCK_SIZE;
        sramOffset += FLASH_BLOCK_SIZE;
        length -= FLASH_BLOCK_SIZE;
    }

    return 0;
}

void loadRAM(struct Memory* memory, enum StoredInfoType storeType)
{
    if (memory->mbc && memory->mbc->flags | MBC_FLAGS_BATTERY)
    {
        int size = RAM_BANK_SIZE * getRAMBankCount(memory->rom);
        switch (storeType)
        {
            case StoredInfoTypeAll:
            case StoredInfoTypeSettingsRAM:
                gSaveReadCallback(
                    memory->cartRam, 
                    ALIGN_FLASH_OFFSET(sizeof(struct GameboySettings)), 
                    size
                );
                break;
            case StoredInfoTypeRAM:
                gSaveReadCallback(
                    memory->cartRam, 
                    0, 
                    size
                );
                break;
        }
    }
}

enum StoredInfoType loadSettings(struct GameBoy* gameboy)
{
    gSaveReadCallback(&gameboy->settings, 0, sizeof(struct GameboySettings));
    if (gameboy->settings.header == GB_SETTINGS_HEADER)
    {
        if (gameboy->settings.version <= 1) {
            gameboy->settings.storedType = getDeprecatedStoredInfoType(gameboy);
        }
    }
    else
    {
        gameboy->settings = gDefaultSettings;
    }
}

struct MiscSaveStateData
{
    struct CPUState cpu;
    char cpuPadding[128 - sizeof(struct CPUState)];
    struct AudioRenderState audioState;
};

int readGameboyState(struct GameBoy* gameboy, SaveReadCallback readCallback, int offset, int gbc)
{
    int sectionSize = RAM_BANK_SIZE * getRAMBankCount(gameboy->memory.rom);
    if (gSaveReadCallback(gameboy->memory.cartRam, offset, sectionSize) == -1)
    {
        return -1;
    }
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);

    sectionSize = gbc ? 0x4000 : 0x2000;
    if (gSaveReadCallback(&gameboy->memory.vram, offset, sectionSize) == -1)
    {
        return -1;
    }
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);

    sectionSize = sizeof(u16) * PALETTE_COUNT;
    if (gSaveReadCallback(&gameboy->memory.vram.colorPalettes, offset, sectionSize) == -1)
    {
        return -1;
    }
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);

    sectionSize = gbc ? MAX_RAM_SIZE : 0x2000;
    if (gSaveReadCallback(gameboy->memory.internalRam, offset, sectionSize) == -1)
    {
        return -1;
    }
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);
    
    sectionSize = sizeof(struct MiscMemory);
    if (gSaveReadCallback(&gameboy->memory.misc, offset, sectionSize) == -1)
    {
        return -1;
    }
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);

    struct MiscSaveStateData miscData;
    sectionSize = sizeof(struct MiscSaveStateData);
    if (gSaveReadCallback(&miscData, offset, sectionSize) == -1)
    {
        return -1;
    }

    gameboy->cpu = miscData.cpu;
    gameboy->memory.audio = miscData.audioState;
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);

    return 0;
}

int loadGameboyState(struct GameBoy* gameboy, enum StoredInfoType storeType)
{
    if (storeType != StoredInfoTypeAll)
    {
        return -1;
    }

    bool gbc = gameboy->memory.rom->mainBank[GB_ROM_H_GBC_FLAG] == GB_ROM_GBC_ONLY || 
        gameboy->memory.rom->mainBank[GB_ROM_H_GBC_FLAG] == GB_ROM_GBC_SUPPORT;

    int offset = 0;
    int sectionSize;
    struct GameboySettings settings;
    sectionSize = sizeof(struct GameboySettings);
    if (gSaveReadCallback(&settings, offset, sectionSize))
    {
        DEBUG_PRINT_F("Could not load header\n");
        return -1;
    }
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);

    if (settings.header != GB_SETTINGS_HEADER || settings.version > GB_SETTINGS_CURRENT_VERSION)
    {
        DEBUG_PRINT_F("Invalid header %X %X\n", settings.header, settings.version);
        return -1;
    }

    // The first version of the emualtor 
    // had the same save layout as the gameboy color
    switch (settings.version)
    {
        case 0:
            gbc = TRUE;
            // intentionally fall through
        case 1:
            settings.compressedSize = 0;
    }

    gameboy->settings = settings;

    if (settings.compressedSize) {
        if (gSaveReadCallback(gCompressedMemory, FLASH_BLOCK_SIZE, settings.compressedSize)) {
            DEBUG_PRINT_F("Failed to read compressed data %d\n", settings.compressedSize);
            return -1;
        }

        DEBUG_PRINT_F("Read %X %X\n", settings.compressedSize, compressedChecksum(gameboy->settings.compressedSize));

        if (lzfse_decode_buffer(gUncompressedMemory, 128 * 1024, gCompressedMemory, settings.compressedSize, &gLZFSEMemory) == 0) {
            DEBUG_PRINT_F("Failed to decompress data %d\n", settings.compressedSize);
            return -1;
        }

        if (readGameboyState(gameboy, readFromUncompressedData, 0, gbc)) {
            return -1;
        }
    } else {
        if (readGameboyState(gameboy, gSaveReadCallback, offset, gbc)) {
            return -1;
        }
    }

    gameboy->memory.bankSwitch(&gameboy->memory, -1, 0);

    if (gameboy->cpu.gbc)
    {
        int bank = READ_REGISTER_DIRECT(&gameboy->memory, REG_VBK) & REG_VBK_MASK;
        gameboy->memory.memoryMap[0x8] = gameboy->memory.vramBytes + (bank ? MEMORY_MAP_SEGMENT_SIZE * 2 : 0);
        gameboy->memory.memoryMap[0x9] = gameboy->memory.memoryMap[0x8] + MEMORY_MAP_SEGMENT_SIZE;

        bank = READ_REGISTER_DIRECT(&gameboy->memory, REG_SVBK) & REG_SVBK_MASK;
        gameboy->memory.memoryMap[0xD] = bank ?
            gameboy->memory.internalRam + bank * MEMORY_MAP_SEGMENT_SIZE :
            gameboy->memory.internalRam + MEMORY_MAP_SEGMENT_SIZE;
        gameboy->memory.memoryMap[0xF] = gameboy->memory.memoryMap[0xD];
    }

    updateToLatestVersion(gameboy);

    return 0;
}

int generateCompressedSaveState(struct GameBoy* gameboy, enum StoredInfoType storeType) {
    bool gbc = gameboy->memory.rom->mainBank[GB_ROM_H_GBC_FLAG] == GB_ROM_GBC_ONLY || 
        gameboy->memory.rom->mainBank[GB_ROM_H_GBC_FLAG] == GB_ROM_GBC_SUPPORT;

    zeroMemory(gUncompressedMemory, sizeof(gUncompressedMemory));

    int offset = 0;
    int sectionSize = 0;

    if (storeType == StoredInfoTypeSettings || storeType == StoredInfoTypeNone)
    {
        return 0;
    }
    
    sectionSize = RAM_BANK_SIZE * getRAMBankCount(gameboy->memory.rom);
    if (writeToUncompressedMemory(gameboy->memory.cartRam, offset, sectionSize)) {
        DEBUG_PRINT_F("Save fail 1 %X $X\n", offset, sectionSize);
        return -1;
    }
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);

    if (storeType == StoredInfoTypeSettingsRAM || storeType == StoredInfoTypeRAM)
    {
        return 0;
    }

    sectionSize = gbc ? 0x4000 : 0x2000;
    if (writeToUncompressedMemory(&gameboy->memory.vram, offset, sectionSize)) {
        DEBUG_PRINT_F("Save fail 2 %X $X\n", offset, sectionSize);
        return -1;
    }
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);

    sectionSize = sizeof(u16) * PALETTE_COUNT;
    if (writeToUncompressedMemory(&gameboy->memory.vram.colorPalettes, offset, sectionSize)) {
        DEBUG_PRINT_F("Save fail 2 %X $X\n", offset, sectionSize);
        return -1;
    }
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);

    sectionSize = gbc ? MAX_RAM_SIZE : 0x2000;
    if (writeToUncompressedMemory(gameboy->memory.internalRam, offset, sectionSize)) {
        DEBUG_PRINT_F("Save fail 3 %X $X\n", offset, sectionSize);
        return -1;
    }
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);
    
    sectionSize = sizeof(struct MiscMemory);
    if (writeToUncompressedMemory(&gameboy->memory.misc, offset, sectionSize)) {
        DEBUG_PRINT_F("Save fail 4 %X $X\n", offset, sectionSize);
        return -1;
    }
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);

    struct MiscSaveStateData miscData;
    miscData.cpu = gameboy->cpu;
    miscData.audioState = gameboy->memory.audio;
    sectionSize = sizeof(struct MiscSaveStateData);
    if (writeToUncompressedMemory(&miscData, offset, sectionSize)) {
        DEBUG_PRINT_F("Save fail 5 %X $X\n", offset, sectionSize);
        return -1;
    }
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);

    return lzfse_encode_buffer(gCompressedMemory, 32 * 1024, gUncompressedMemory, offset, &gLZFSEMemory);
}

int attemptGameboySaveState(struct GameBoy* gameboy, enum StoredInfoType storeType)
{
    if (storeType == StoredInfoTypeNone)
    {
        return -1;
    }

    gameboy->settings.timer = gameboy->memory.misc.time;

    if (gSaveTypeSetting.saveType == SaveTypeFlash && osFlashAllErase())
    {
        DEBUG_PRINT_F("Save fail clear\n");
        return -1;
    }

    gameboy->settings.version = GB_SETTINGS_CURRENT_VERSION;
    gameboy->settings.storedType = storeType;
    gameboy->settings.compressedSize = generateCompressedSaveState(gameboy, storeType);

    DEBUG_PRINT_F("Write %X %X\n", gameboy->settings.compressedSize, compressedChecksum(gameboy->settings.compressedSize));

    if (gameboy->settings.compressedSize == 0 ||
        gameboy->settings.compressedSize > getSaveTypeSize(gSaveTypeSetting.saveType))
    {
        return -1;
    }

    if (gSaveWriteCallback(&gameboy->settings, 0, sizeof(struct GameboySettings))) {
        DEBUG_PRINT_F("Save fail 0 %X $X\n", 0, sizeof(struct GameboySettings));
        return -1;
    }

    if (gSaveWriteCallback(&gCompressedMemory, FLASH_BLOCK_SIZE, gameboy->settings.compressedSize)) {
        DEBUG_PRINT_F("Save fail data %X $X\n", FLASH_BLOCK_SIZE, gameboy->settings.compressedSize);
        return -1;
    }

    return 0;
}

enum StoredInfoType saveGameboyState(struct GameBoy* gameboy)
{
    enum StoredInfoType storeType = StoredInfoTypeAll;

    while (storeType != StoredInfoTypeNone && attemptGameboySaveState(gameboy, storeType)) {
        ++storeType;
    }

    return storeType;
}

int getSaveStateSize(struct GameBoy* gameboy)
{
    int offset = 0;
    int sectionSize = sizeof(struct GameboySettings);
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);
    sectionSize = RAM_BANK_SIZE * getRAMBankCount(gameboy->memory.rom);
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);
    sectionSize = sizeof(struct GraphicsMemory);
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);
    sectionSize = MAX_RAM_SIZE;
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);
    sectionSize = sizeof(struct MiscMemory);
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);
    sectionSize = sizeof(struct MiscSaveStateData);
    offset = ALIGN_FLASH_OFFSET(offset + sectionSize);
    return offset;
}

void initSaveCallbacks()
{
    if (gSaveTypeSetting.header == SAVE_HEADER_VALUE)
    {
        switch (gSaveTypeSetting.saveType)
        {
            case SaveTypeSRAM:
            case SaveTypeSRAM3X:
                gSaveWriteCallback = saveToSRAM;
                gSaveReadCallback = loadFromSRAM;
                osSramInit();
                break;
            default:
                gSaveWriteCallback = saveToFlash;
                gSaveReadCallback = loadFromFlash;
                osFlashInit();
                break;
        }
    }
    else
    {
        gSaveWriteCallback = saveToFlash;
        gSaveReadCallback = loadFromFlash;
        osFlashInit();
    }
}

enum StoredInfoType getStoredInfoType(struct GameBoy* gameboy)
{
    if (gameboy->settings.version == 2) {
        return gameboy->settings.storedType;
    } else {
        return getDeprecatedStoredInfoType(gameboy);
    }
}

enum StoredInfoType getDeprecatedStoredInfoType(struct GameBoy* gameboy)
{
    int saveTypeSize = getSaveTypeSize(gSaveTypeSetting.saveType);
    int ramSize = RAM_BANK_SIZE * getRAMBankCount(gameboy->memory.rom);

    if (getSaveStateSize(gameboy) <= saveTypeSize)
    {
        return StoredInfoTypeAll;
    }
    else if (ramSize + ALIGN_FLASH_OFFSET(sizeof(struct GameboySettings)) <= saveTypeSize)
    {
        return StoredInfoTypeSettingsRAM;
    }
    else if (ramSize <= saveTypeSize)
    {
        return StoredInfoTypeRAM;
    }
    else if (ramSize <= ALIGN_FLASH_OFFSET(sizeof(struct GameboySettings)))
    {
        return StoredInfoTypeSettings;
    }
    else
    {
        return StoredInfoTypeNone;
    }
}