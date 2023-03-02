﻿#include "gyro_vmu_flash.h"
#include "gyro_vmu_device.h"
#include "gyro_vmu_vmi.h"
#include "gyro_vmu_vms.h"
#include "gyro_vmu_lcd.h"
#include "gyro_vmu_extra_bg_pvr.h"
#include "gyro_vmu_icondata.h"
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>

#include <gimbal/preprocessor/gimbal_macro_utils.h>

#include "hw/evmu_device_.h"
#include "hw/evmu_memory_.h"

#define GYRO_PATH_MAX_SIZE  1024

static char _lastErrorMsg[VMU_FLASH_LOAD_IMAGE_ERROR_MESSAGE_SIZE] = { '\0' };


const char* gyVmuFlashLastErrorMessage(void) {
    return _lastErrorMsg;
}

size_t gyVmuFlashBytes(const struct VMUDevice* dev) {
    return EVMU_FLASH_SIZE;
}

static inline int tobcd(int n) {
  return ((n/10)<<4)|(n%10);
}

int gyVmuFlashBytesToBlocks(int bytes) {
    int blocks = bytes/VMU_FLASH_BLOCK_SIZE;
    if(bytes % VMU_FLASH_BLOCK_SIZE) ++blocks;
    return blocks;
}

int gyVmuFlashIsIconDataVms(const struct VMUFlashDirEntry* entry) {
    return (memcmp(entry->fileName, VMU_ICONDATA_VMS_FILE_NAME, VMU_FLASH_DIRECTORY_FILE_NAME_SIZE) == 0);
}

int gyVmuFlashIsExtraBgPvr(const struct VMUFlashDirEntry* entry) {
    return (memcmp(entry->fileName, GYRO_VMU_EXTRA_BG_PVR_FILE_NAME, VMU_FLASH_DIRECTORY_FILE_NAME_SIZE) == 0);
}


int gyVmuFlashLoadVMI(VMIFileInfo* info, const char *path) {
    int success = 0;
    size_t bytesRead;

    EVMU_LOG_VERBOSE("Loading VMI Info [%s].", path);
    EVMU_LOG_PUSH();

    memset(info, 0, sizeof(VMIFileInfo));

    FILE* file = fopen(path, "rb");

    if(!file) {
        EVMU_LOG_ERROR("Could not open binary file for reading!");

    } else {

        fread(info, 1, sizeof(VMIFileInfo), file);
        fclose(file);

        //BETTER BE TRUE OR COMPILER PACKED OUR FUCKING STRUCT WRONG!!!!
        assert(sizeof(VMIFileInfo) == VMU_VMI_FILE_SIZE);

       if(!bytesRead) {
            EVMU_LOG_ERROR("Could not read from file!");
        } else {
            if(bytesRead != VMU_VMI_FILE_SIZE) {
                EVMU_LOG_WARNING("File size didn't exactly match expected VMI size, but continuing anyway [%d/%d].", bytesRead, VMU_VMI_FILE_SIZE);
            }
            success = 1;
        }
    }

    EVMU_LOG_POP(1);

    return success;

}

uint8_t* gyVmuFlashLoadVMS(const char *vmspath, size_t* fileSize) {
    assert(vmspath);
    uint8_t* vmsData = NULL;
    if(fileSize) *fileSize = 0;

    EVMU_LOG_VERBOSE("Loading VMS image into buffer.", vmspath);
    EVMU_LOG_PUSH();

    FILE* vmsFp = fopen(vmspath, "rb");
    if(/*!retVal ||*/ !vmsFp) {
        EVMU_LOG_ERROR("Could not open VMS file: [%s]", vmspath);
        goto end;
    }

    size_t fileBytes = 0;
    fseek(vmsFp, 0, SEEK_END); // seek to end of file
    fileBytes = ftell(vmsFp); // get current file pointer
    fseek(vmsFp, 0, SEEK_SET); // seek back to beginning of file
    if(/*!retVal ||*/ !fileBytes) {
        EVMU_LOG_ERROR("Could not retrieve VMS file length");
        goto cleanup_file;
    }

    vmsData = malloc(fileBytes);
    size_t bytesRead = 0;
    bytesRead = fread(vmsData, 1, fileBytes, vmsFp);
    if(fileSize) *fileSize = bytesRead;
    if(/*!retVal ||*/ bytesRead != fileBytes) {
        EVMU_LOG_ERROR("Could not read entire file contents! [bytes read %d/%d]", bytesRead, fileBytes);
        goto cleanup_data;
    }

    goto cleanup_file;

cleanup_data:
    free(vmsData);
    vmsData = NULL;
cleanup_file:
    fclose(vmsFp);
end:

    EVMU_LOG_POP(1);
    return vmsData;
}

static inline size_t _gyread(FILE* fd, void* buff, size_t bytes) {
    size_t bytesRead;
    bytesRead = fread(buff, 1, bytes, fd);
    return bytesRead;
}

int gyinitbios(VMUDevice* dev, FILE* fd) {
    EvmuDevice_* pDevice_ = EVMU_DEVICE_PRISTINE(dev);
    int r=0, t=0;
    memset(pDevice_->pMemory->rom, 0, sizeof(pDevice_->pMemory->rom));
    while(t < (int)sizeof(pDevice_->pMemory->rom) && (r = _gyread(fd, pDevice_->pMemory->rom+t, sizeof(pDevice_->pMemory->rom) - t)) > 0)
        t += r;
    if(r < 0 || t < 0x200)
        return 0;
    return 1;
}

int gyloadbios(VMUDevice* dev, const char *filename)
{
  FILE* fd = fopen(filename, "rb");

  if(fd) {
    if(!gyinitbios(dev, fd)) {
      fclose(fd);
      EVMU_LOG_ERROR("%s: can't load BIOS image", filename);
      return 0;
    } else {
        EVMU_LOG_VERBOSE("Bios image [%s] loaded successfully!", filename);
    }
    fclose(fd);
//    dev->biosLoaded = 1;
    return 1;
  } else {
//      dev->biosLoaded = 0;
    //perror(filename);
    return 0;
  }
}





//=== FLASH BLOCK API ===

VMUFlashRootBlock* gyVmuFlashBlockRoot(struct VMUDevice* dev) {
    EvmuDevice_* pDevice_ = EVMU_DEVICE_PRISTINE(dev);
    return (VMUFlashRootBlock*)(pDevice_->pMemory->flash + VMU_FLASH_BLOCK_SIZE*VMU_FLASH_BLOCK_ROOT);
}

const VMUFlashRootBlock* gyVmuFlashBlockRootConst(const struct VMUDevice* dev) {
    EvmuDevice_* pDevice_ = EVMU_DEVICE_PRISTINE(dev);
    return (const VMUFlashRootBlock*)(pDevice_->pMemory->flash + VMU_FLASH_BLOCK_SIZE*VMU_FLASH_BLOCK_ROOT);
}

uint16_t gyVmuFlashBlockDirectory(const struct VMUDevice* dev) {
    return gyVmuFlashBlockRoot((VMUDevice*)dev)->dirBlock;
}

uint16_t gyVmuFlashBlockFat(const struct VMUDevice* dev) {
    return gyVmuFlashBlockRoot((VMUDevice*)dev)->fatBlock;
}

uint16_t gyVmuFlashBlockCount(const struct VMUDevice* dev) {
#if 1
    return (gyVmuFlashBlockRootConst(dev)->fatSize * VMU_FLASH_BLOCK_SIZE/sizeof(uint16_t));
#else
    return gyVmuFlashBLockRootConst(dev)->totalSize;
#endif
}

unsigned char* gyVmuFlashBlock(struct VMUDevice* dev, uint16_t block) {
    EvmuDevice_* pDevice_ = EVMU_DEVICE_PRISTINE(dev);
    return (block >= gyVmuFlashBlockCount(dev))? NULL : &pDevice_->pMemory->flash[block*VMU_FLASH_BLOCK_SIZE];
}

uint16_t* gyVmuFlashBlockFATEntry(VMUDevice* dev, uint16_t block) {
    return (block >= gyVmuFlashBlockCount(dev))? NULL : (((uint16_t*)gyVmuFlashBlock(dev, gyVmuFlashBlockFat(dev)))+block);
}

uint16_t gyVmuFlashDirEntryCount(const struct VMUDevice* dev) {
    return gyVmuFlashBlockRoot((VMUDevice*)dev)->dirSize*VMU_FLASH_BLOCK_SIZE/VMU_FLASH_DIRECTORY_ENTRY_SIZE;
}

uint16_t gyVmuFlashBlockNext(const VMUDevice* dev, uint16_t block) {
    const uint16_t* fatEntry = gyVmuFlashBlockFATEntry(dev, block);
    return fatEntry? *fatEntry : VMU_FLASH_BLOCK_FAT_UNALLOCATED;
}

/* DATA file blocks are allocated as the first available free block starting at the highest address.
 * GAME file blocks are allocated contiguously starting at address 0.
 */
uint16_t gyVmuFlashBlockAlloc(VMUDevice* dev, uint16_t previous, int fileType) {
    uint16_t    block = VMU_FLASH_BLOCK_FAT_UNALLOCATED;
    uint16_t*   fat   = (uint16_t*)gyVmuFlashBlock(dev, gyVmuFlashBlockFat(dev));

    uint16_t firstBlock;
    uint16_t lastBlock;
    if(fileType == VMU_FLASH_FILE_TYPE_GAME) {
        firstBlock  = 0;
        lastBlock   = gyVmuFlashUserDataBlocks(dev)-1;
    } else if(fileType == VMU_FLASH_FILE_TYPE_DATA) {
        firstBlock  = gyVmuFlashUserDataBlocks(dev)-1;
        lastBlock   = 0;
    } else assert(0); //NO file type? What the shit you trying to pull?

    //Iterate until unallocated block is found.
    int i = firstBlock;
    for(;;) {
        if(fat[i] == VMU_FLASH_BLOCK_FAT_UNALLOCATED) {
            //Claim block
            block = i;
            /* Assume this is the last block in the sequence,
             * until it's passed to the alloc function later
             * as the previous block of a new allocation.
             */
            fat[i] = VMU_FLASH_BLOCK_FAT_LAST_IN_FILE;
            //Zero out contents of block
            memset(gyVmuFlashBlock(dev, block), 0, VMU_FLASH_BLOCK_SIZE);
            //Update fat entry if not first block in series
            if(previous != VMU_FLASH_BLOCK_FAT_UNALLOCATED &&
               previous != VMU_FLASH_BLOCK_FAT_LAST_IN_FILE)
                fat[previous] = block;
            break;
        }


        if(i == lastBlock) break;

        if(fileType == VMU_FLASH_FILE_TYPE_GAME) {
            ++i;
        } else {
            --i;
        }
    }

    return block;
}

/* It's not recommended that you just delete individual blocks randomly.
 * Whatever the previous block was that was pointing to this current block
 * is now a dangling pointer to an unallocated block once it's freed.
 */
void gyVmuFlashBlockFree(VMUDevice* dev, uint16_t block) {
    assert(block < gyVmuFlashBlockCount(dev));
    memset(gyVmuFlashBlock(dev, block), 0, VMU_FLASH_BLOCK_SIZE);
    *gyVmuFlashBlockFATEntry(dev, block) = VMU_FLASH_BLOCK_FAT_UNALLOCATED;
}






//=== FLASH DIRECTORY API ====


VMUFlashDirEntry* gyVmuFlashDirEntryByIndex(VMUDevice* dev, uint16_t index) {
    const VMUFlashRootBlock* root = gyVmuFlashBlockRootConst(dev);

    VMUFlashDirEntry* entry = (VMUFlashDirEntry*)gyVmuFlashBlock(dev, root->dirBlock - (root->dirSize-1));
    return &entry[index];
}

VMUFlashDirEntry* gyVmuFlashDirEntryFind(struct VMUDevice* dev, const char* name) {
    VMUFlashDirEntry* entry = NULL;
    uint16_t block          = 0;
    const size_t len        = strlen(name);
    int match = 0;

    for(int b = 0; b < gyVmuFlashFileCount(dev); ++b) {
        entry = gyVmuFlashFileAtIndex(dev, b);

        if(entry->fileType != VMU_FLASH_FILE_TYPE_DATA &&
           entry->fileType != VMU_FLASH_FILE_TYPE_GAME)
                continue;

        match = 1;

        for(unsigned i = 0; i < VMU_FLASH_DIRECTORY_FILE_NAME_SIZE && i < len; ++i) {
            if(entry->fileName[i] != name[i]) {
                match = 0;
                entry = NULL;
                break;
            }
        }

        if(match) break;
    }

    return match? entry : NULL;
}

VMUFlashDirEntry* gyVmuFlashDirEntryGame(struct VMUDevice* dev) {
    VMUFlashDirEntry* entry = NULL;
    for(unsigned i = 0; i < gyVmuFlashBlockRoot(dev)->dirSize; ++i) {
        entry = gyVmuFlashDirEntryByIndex(dev, i);
        if(entry && entry->fileType == VMU_FLASH_FILE_TYPE_GAME)
            return entry;
    }

    return NULL;
}


uint8_t gyVmuFlashDirEntryIndex(const struct VMUDevice* dev, const VMUFlashDirEntry* entry) {
    const unsigned char* dir = gyVmuFlashBlock(dev, gyVmuFlashBlockDirectory(dev));
    const ptrdiff_t diff = (const unsigned char*)entry - dir;
    assert(diff % sizeof(VMUFlashDirEntry) == 0);
    return diff/sizeof(VMUFlashDirEntry);
}

VMUFlashDirEntry* gyVmuFlashDirEntryDataNext(struct VMUDevice* dev, const VMUFlashDirEntry* prev) {
    const VMUFlashDirEntry*     entry       = NULL;
    uint16_t                    dirIndex    = prev? gyVmuFlashDirEntryIndex(dev, prev) : 0;

    if(prev) {
        //Check if we're at the end
        if(dirIndex == gyVmuFlashDirEntryCount(dev)-1)
            return NULL; //There is no next entry
    } else { //No previous entry, beginning iteration
        //Grab first entry
        prev = gyVmuFlashDirEntryByIndex(dev, 0);
        //Return first entry if it's valid
        if(prev && prev->fileType == VMU_FLASH_FILE_TYPE_DATA) {
            return prev;
        } else return NULL;
    }

    //Start iterating from the next index, looking for the next data file
    for(dirIndex = dirIndex + 1; dirIndex < gyVmuFlashDirEntryCount(dev); ++dirIndex) {
        prev = gyVmuFlashDirEntryByIndex(dev, dirIndex);
        //Found next data file, break
        if(prev->fileType == VMU_FLASH_FILE_TYPE_DATA) {
            entry = prev;
            break;
        }
    }

    return entry; //Should still be NULL if next data file was never found.
}

/* Don't need to update FAT entries, since it should always have the directory
 * as allocated, regardless of whether or not directory entries are currently free.
 */
VMUFlashDirEntry* gyVmuFlashDirEntryAlloc(struct VMUDevice* dev) {
    for(unsigned i = 0; i < gyVmuFlashDirEntryCount(dev); ++i) {
        VMUFlashDirEntry* entry = gyVmuFlashDirEntryByIndex(dev, i);
        if(entry->fileType != VMU_FLASH_FILE_TYPE_GAME && entry->fileType != VMU_FLASH_FILE_TYPE_DATA) {
            return entry;
        }
    }
    return NULL;
}

//Don't have to update FAT table either, data is still ALLOCATED to directory, even if entry is blank.
void gyVmuFlashDirEntryFree(struct VMUFlashDirEntry* entry) {
    memset(entry, 0, sizeof(VMUFlashDirEntry));
}

void gyVmuFlashDirEntryPrint(const VMUDevice* dev, const VMUFlashDirEntry* entry) {

    EVMU_LOG_VERBOSE("Flash Directory Entry - %d", gyVmuFlashDirEntryIndex(dev, entry));
    EVMU_LOG_PUSH();

    char fileNameBuff[VMU_FLASH_DIRECTORY_FILE_NAME_SIZE+1] = { 0 };
    memcpy(fileNameBuff, entry->fileName, VMU_FLASH_DIRECTORY_FILE_NAME_SIZE);
    char typeBuff[20];
    snprintf(typeBuff, sizeof(typeBuff), "UNKNOWN: %d",entry->fileType);
    EVMU_LOG_VERBOSE("%-20s: %40s", "File Type",
           (entry->fileType == VMU_FLASH_FILE_TYPE_GAME)? "GAME" :
           (entry->fileType == VMU_FLASH_FILE_TYPE_DATA)? "DATA" :
           (entry->fileType == VMU_FLASH_FILE_TYPE_NONE)? "NONE":
                                                          typeBuff);
    EVMU_LOG_VERBOSE("%-20s: %40s", "Copy Protection",
           (entry->copyProtection == VMU_FLASH_COPY_PROTECTION_COPY_OK)? "NONE" : "PROTECTED");
    EVMU_LOG_VERBOSE("%-25s: %40u", "First Block",              entry->firstBlock);
    EVMU_LOG_VERBOSE("%-25s: %40s", "File Name",                fileNameBuff);
    EVMU_LOG_VERBOSE("%-25s: %40u", "Creation Century",         GBL_BCD_BYTE_UNPACK(entry->timeStamp[VMU_FLASH_DIRECTORY_DATE_CENTURY]));
    EVMU_LOG_VERBOSE("%-25s: %40u", "Creation Year",            GBL_BCD_BYTE_UNPACK(entry->timeStamp[VMU_FLASH_DIRECTORY_DATE_YEAR]));
    EVMU_LOG_VERBOSE("%-25s: %40u", "Creation Month",           GBL_BCD_BYTE_UNPACK(entry->timeStamp[VMU_FLASH_DIRECTORY_DATE_MONTH]));
    EVMU_LOG_VERBOSE("%-25s: %40u", "Creation Day",             GBL_BCD_BYTE_UNPACK(entry->timeStamp[VMU_FLASH_DIRECTORY_DATE_DAY]));
    EVMU_LOG_VERBOSE("%-25s: %40u", "Creation Hour",            GBL_BCD_BYTE_UNPACK(entry->timeStamp[VMU_FLASH_DIRECTORY_DATE_HOUR]));
    EVMU_LOG_VERBOSE("%-25s: %40u", "Creation Minute",          GBL_BCD_BYTE_UNPACK(entry->timeStamp[VMU_FLASH_DIRECTORY_DATE_MINUTE]));
    EVMU_LOG_VERBOSE("%-25s: %40u", "Creation Second",          GBL_BCD_BYTE_UNPACK(entry->timeStamp[VMU_FLASH_DIRECTORY_DATE_SECOND]));
    EVMU_LOG_VERBOSE("%-25s: %40u", "Creation Weekday",         GBL_BCD_BYTE_UNPACK(entry->timeStamp[VMU_FLASH_DIRECTORY_DATE_WEEKDAY]));
    EVMU_LOG_VERBOSE("%-25s: %40u", "File Size (Blocks)",       entry->fileSize);
    EVMU_LOG_VERBOSE("%-25s: %40u", "Header Offset (Blocks)",   entry->headerOffset);
    EVMU_LOG_VERBOSE("%-25s: %40u", "Unused",                   *(uint32_t*)entry->unused);

    EVMU_LOG_POP(1);

}





//==== HIGH-LEVEL FILE API =====

int gyVmuFlashCheckFormatted(const VMUDevice *dev) {
   const VMUFlashRootBlock* root = gyVmuFlashBlockRootConst(dev);

    for(unsigned i = 0; i < 0xf; ++i)
        if(root->formatted[i] != VMU_FLASH_BLOCK_ROOT_FORMATTED_BYTE)
            return 0;

    return 1;
}


uint16_t gyVmuFlashUserDataBlocks(const struct VMUDevice* dev) {
    VMUFlashRootBlock* root = gyVmuFlashBlockRoot((VMUDevice*)dev);
#if 0 //technically correct based on Sega docs, but apparently the BIOS and games are fucking RETARDED
    return root->saveAreaSize? root->saveAreaBlock - root->saveAreaSize : root->dirBlock - root->dirSize;
#else
    return root->userSize;
#endif
}

VMUFlashMemUsage gyVmuFlashMemUsage(const struct VMUDevice* dev) {
    VMUFlashMemUsage    mem     = { 0, 0, 0, 0 };

    if(gyVmuFlashCheckFormatted(dev)) {

        for(uint16_t b = 0; b < gyVmuFlashUserDataBlocks(dev); ++b) {
            const uint16_t* fatEntry = gyVmuFlashBlockFATEntry(dev, b);

            switch(*fatEntry) {
            case VMU_FLASH_BLOCK_FAT_UNALLOCATED:
                ++mem.blocksFree;
                break;
            case VMU_FLASH_BLOCK_FAT_DAMAGED:
                ++mem.blocksDamaged;
                break;
            default:
                ++mem.blocksUsed;
            }
        }

        const VMUFlashRootBlock* root = gyVmuFlashBlockRootConst(dev);
        //Remaining size after System (FAT + Directory + Root blocks) + Userdata blocks have been taken into account.
        mem.blocksHidden = gyVmuFlashBlockCount(dev) - (root->fatSize + root->dirSize + 1 + gyVmuFlashUserDataBlocks(dev));

    }

    return mem;
}


int gyVmuFlashFileDelete(struct VMUDevice* dev, VMUFlashDirEntry* entry) {
    assert(entry);
    int blocksFreed = 0;
    char name[VMU_FLASH_DIRECTORY_FILE_NAME_SIZE+1] = { '\0' };

    gyVmuFlashDirEntryName(entry, name);

    EVMU_LOG_VERBOSE("Deleting file from flash: [%s]", name);
    EVMU_LOG_PUSH();
    uint16_t block = entry->firstBlock;

    while(block != VMU_FLASH_BLOCK_FAT_LAST_IN_FILE) {
        EVMU_LOG_VERBOSE("[%d] Freeing block: [%d]", blocksFreed++, block);

        uint16_t nextBlock = gyVmuFlashBlockNext(dev, block);
        if(nextBlock == VMU_FLASH_BLOCK_FAT_UNALLOCATED) {
            EVMU_LOG_ERROR("[%d] Unallocated block in the middle of file chain! [%d]", blocksFreed+1, block);
            return blocksFreed;
        }

        gyVmuFlashBlockFree(dev, block);

        block = nextBlock;
    }

    //Clear directory entry
    gyVmuFlashDirEntryFree(entry);
    EVMU_LOG_POP(1);
    return blocksFreed;
}

VMUFlashDirEntry* gyVmuFlashFileCreate(VMUDevice* dev, const VMUFlashNewFileProperties* properties, const unsigned char* data, VMU_LOAD_IMAGE_STATUS* status) {
    int blocks[gyVmuFlashUserDataBlocks(dev)];
    VMUFlashDirEntry* entry = NULL;

    //Can't assume filename is null terminated.
    char fileNameBuff[VMU_FLASH_DIRECTORY_FILE_NAME_SIZE+1] = { 0 };
    memcpy(fileNameBuff, properties->fileName, VMU_FLASH_DIRECTORY_FILE_NAME_SIZE);

    EVMU_LOG_VERBOSE("VMU Flash - Creating file [%s].", fileNameBuff);
    EVMU_LOG_PUSH();

    //=== 1 - Check if we're creating a GAME file while one already exists. ===
    if(properties->fileType == VMU_FLASH_FILE_TYPE_GAME && gyVmuFlashDirEntryGame(dev)) {
        EVMU_LOG_ERROR("Only one GAME file can be present at a time!");
        EVMU_LOG_POP(1);
        *status = VMU_LOAD_IMAGE_GAME_DUPLICATE;
        return NULL;
    }

#if 0
    //=== 2 - Make sure we don't already have a file with the same name. ===
    if(gyVmuFlashDirEntryFind(dev, properties->fileName)) {
        EVMU_LOG_ERROR("File already present with the same name!");
        EVMU_LOG_POP(1);
        *status = VMU_LOAD_IMAGE_NAME_DUPLICATE;
        return NULL;
    }
#endif

    //=== 3 - Check whether there are enough free blocks available for the file. ===
    VMSFileInfo* vmsHeader = (VMSFileInfo*)(properties->fileType == VMU_FLASH_FILE_TYPE_GAME)?
                &data[VMU_FLASH_GAME_VMS_HEADER_OFFSET] : data;
    unsigned totalBytes      = properties->fileSizeBytes;
    unsigned blocksRequired  = gyVmuFlashBytesToBlocks(properties->fileSizeBytes);

    VMUFlashMemUsage memUsage = gyVmuFlashMemUsage(dev);
    if(memUsage.blocksFree < blocksRequired) {
        sprintf(_lastErrorMsg, "Not enough free blocks left on memory unit! [Free: %d, Required: %d]",
                memUsage.blocksFree, blocksRequired);

        EVMU_LOG_ERROR(_lastErrorMsg);
        EVMU_LOG_POP(1);
        *status = VMU_LOAD_IMAGE_INADEQUATE_FREE_BLOCKS;
        return NULL;
    }

    /* Game data must all be stored contiguously starting at block 0,
     * so check whether memory card requires defrag.
     */
    if(properties->fileType == VMU_FLASH_FILE_TYPE_GAME) {
        unsigned contiguousBlocks = gyVmuFlashContiguousFreeBlocks(dev);

        //Defragment card if we couldn't find enough contiguous blocks.
        if(contiguousBlocks < blocksRequired) {
            EVMU_LOG_WARNING("Not enough contiguous blocks available for GAME file [%d/%d]. Defrag required.",
                   contiguousBlocks, blocksRequired);

            if(!gyVmuFlashDefragment(dev, -1)) {
                *status = VMU_LOAD_IMAGE_DEFRAG_FAILED;
                goto end;
            }

            contiguousBlocks = gyVmuFlashContiguousFreeBlocks(dev);
            if(contiguousBlocks < blocksRequired) {
                EVMU_LOG_ERROR("Still not enough contiguous blocks available [%d/%d], Defrag must have failed!",
                       contiguousBlocks, blocksRequired);
                *status = VMU_LOAD_IMAGE_DEFRAG_FAILED;
                goto end;
            }
        }
    }

    //=== 4 - Create Flash Directory Entry for file. ===
    entry = gyVmuFlashDirEntryAlloc(dev);
    if(!entry) {
        EVMU_LOG_ERROR("Could not allocate entry in Flash Directory (too many files present).");
        *status = VMU_LOAD_IMAGE_FILES_MAXED;
        EVMU_LOG_POP(1);
        return NULL;
    }

    EVMU_LOG_VERBOSE("Creating Flash Directory Entry [index: %d]", gyVmuFlashDirEntryIndex(dev, entry));
    EVMU_LOG_PUSH();

    //Fill in Flash Directory Entry for file
    memset(entry, 0, sizeof(VMUFlashDirEntry));
    memcpy(entry->fileName, properties->fileName, VMU_FLASH_DIRECTORY_FILE_NAME_SIZE);
    entry->copyProtection   = properties->copyProtection;
    entry->fileType         = properties->fileType;
    entry->fileSize         = blocksRequired;
    entry->headerOffset     = (entry->fileType == VMU_FLASH_FILE_TYPE_DATA)? 0 : 1;

    //Add timestamp to directory
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    entry->timeStamp[VMU_FLASH_DIRECTORY_DATE_CENTURY]  = GBL_BCD_BYTE_PACK(tm->tm_year/100+19);
    entry->timeStamp[VMU_FLASH_DIRECTORY_DATE_YEAR]     = GBL_BCD_BYTE_PACK(tm->tm_year%100);
    entry->timeStamp[VMU_FLASH_DIRECTORY_DATE_MONTH]    = GBL_BCD_BYTE_PACK(tm->tm_mon+1);
    entry->timeStamp[VMU_FLASH_DIRECTORY_DATE_DAY]      = GBL_BCD_BYTE_PACK(tm->tm_mday);
    entry->timeStamp[VMU_FLASH_DIRECTORY_DATE_HOUR]     = GBL_BCD_BYTE_PACK(tm->tm_hour);
    entry->timeStamp[VMU_FLASH_DIRECTORY_DATE_MINUTE]   = GBL_BCD_BYTE_PACK(tm->tm_min);
    entry->timeStamp[VMU_FLASH_DIRECTORY_DATE_SECOND]   = GBL_BCD_BYTE_PACK(tm->tm_sec);
    entry->timeStamp[VMU_FLASH_DIRECTORY_DATE_WEEKDAY]  = GBL_BCD_BYTE_PACK(tm->tm_wday);

    gyVmuFlashDirEntryPrint(dev, entry);
    EVMU_LOG_POP(1);

    //=== 5 - Allocate FAT Blocks for File ===
    EVMU_LOG_VERBOSE("Allocating FAT Blocks for file [Blocks: %d].", blocksRequired);
    EVMU_LOG_PUSH();

    //Allocate FAT blocks to hold the image, chain them togethers
    memset(blocks, -1, sizeof(int)*gyVmuFlashUserDataBlocks(dev));
    for(unsigned b = 0; b < blocksRequired; ++b) {
        blocks[b] = gyVmuFlashBlockAlloc(dev, (b > 0)? blocks[b-1] : VMU_FLASH_BLOCK_FAT_UNALLOCATED, properties->fileType);

        if(blocks[b] == VMU_FLASH_BLOCK_FAT_UNALLOCATED) {
            EVMU_LOG_ERROR("Failed to allocate FAT block: [%d/%d]", b, blocksRequired);
            *status = VMU_LOAD_IMAGE_INADEQUATE_FREE_BLOCKS;
            goto clean_fat_blocks;
        }
    }

    EVMU_LOG_POP(1);

    //=== 6 - Write VMS File  ===
    EVMU_LOG_VERBOSE("Writing VMS File Data.");
    EVMU_LOG_PUSH();

    const unsigned  blocksToWrite   = gyVmuFlashBytesToBlocks((int)totalBytes);
    unsigned        bytesLeft       = totalBytes;

    for(unsigned b = 0; b < blocksToWrite; ++b) {
        const int bytesForBlock =
                (bytesLeft > VMU_FLASH_BLOCK_SIZE)? VMU_FLASH_BLOCK_SIZE : bytesLeft;

        unsigned char* block = gyVmuFlashBlock(dev, blocks[b]);
        if(!block) {
            EVMU_LOG_ERROR("Failed to retrieve block [%d] while writing data: [%d/%d]", blocks[b], b, blocksToWrite);
            *status = VMU_LOAD_IMAGE_DEVICE_READ_ERROR;
            goto clean_fat_blocks;
        }

        memcpy(block,
               (data + b*VMU_FLASH_BLOCK_SIZE),
               bytesForBlock);

        const uint16_t* fatEntry = gyVmuFlashBlockFATEntry(dev, blocks[b]);
        assert(fatEntry);

        EVMU_LOG_VERBOSE("[%d] Writing %d bytes. [Block: %d, Next: %d]",
               b, bytesForBlock, blocks[b], fatEntry? *fatEntry : -1);

        bytesLeft -= bytesForBlock;
    }
    entry->firstBlock = blocks[0];

    if(bytesLeft != 0) {
        EVMU_LOG_VERBOSE("Failed to write entire file: [%d/%d bytes]", totalBytes - bytesLeft, totalBytes);
        *status = VMU_LOAD_IMAGE_DEVICE_WRITE_ERROR;
        goto clean_fat_blocks;
    }

    const VMSFileInfo* vms = gyVmuFlashDirEntryVmsHeader(dev, entry);
    if(!vms) {
        EVMU_LOG_ERROR("Could not verify VMS header that was written to device!");
        *status = VMU_LOAD_IMAGE_DEVICE_READ_ERROR;
        goto clean_fat_blocks;
    }

    if(!gyVmuFlashIsIconDataVms(entry)) {
        gyVmuPrintVMSFileInfo(vms);
    } else {
        gyVmuIconDataPrint((const IconDataFileInfo*)vms);
    }

    //Assuming shit all worked out successfully!
    *status = VMU_LOAD_IMAGE_SUCCESS;
    goto end;

clean_fat_blocks:
    for(unsigned b = 0; b < gyVmuFlashUserDataBlocks(dev); ++b) {
        if(blocks[b] == -1) break;
        gyVmuFlashBlockFree(dev, blocks[b]);
    }
clean_dir_entry:
    entry->fileType = VMU_FLASH_FILE_TYPE_NONE;
    entry = NULL;
end:
    EVMU_LOG_POP(2);

    return entry;
}


int gyVmuFlashContiguousFreeBlocks(const struct VMUDevice* dev) {
    int contiguousBlocks    = 0;
    unsigned userDataBlocks = gyVmuFlashUserDataBlocks(dev);

    for(unsigned i = 0; i < userDataBlocks; ++i) {
        if(*gyVmuFlashBlockFATEntry((VMUDevice*)dev, i) != VMU_FLASH_BLOCK_FAT_UNALLOCATED) {
            break;
        } else {
            ++contiguousBlocks;
        }
    }

    return contiguousBlocks;
}

int gyVmuFlashFileRead(struct VMUDevice* dev, const struct VMUFlashDirEntry* entry, unsigned char* buffer, int includeHeader) {
    size_t bytesRead = 0;
    const size_t byteSize = entry->fileSize * VMU_FLASH_BLOCK_SIZE;
    bytesRead = gyVmuFlashFileReadBytes(dev, entry, buffer, byteSize, includeHeader?  0 : entry->headerOffset * VMU_FLASH_BLOCK_SIZE, includeHeader);
    return (bytesRead == byteSize)? 1 : 0;
}

int gyVmuFlashDefragment(struct VMUDevice* dev, int newUserSize) {
    VMUDevice tempDevice;
    unsigned char tempFlash[EVMU_FLASH_SIZE] = { '\0' };

    int success = 1;
    EVMU_LOG_VERBOSE("Defragmenting VMU Flash Storage");
    EVMU_LOG_PUSH();

    const int fileCount                 = gyVmuFlashFileCount(dev);
    const VMUFlashMemUsage origMemUsage = gyVmuFlashMemUsage(dev);

    if(fileCount) {
        memcpy(&tempDevice, dev, sizeof(VMUDevice));

        EVMU_LOG_VERBOSE("Uninstalling all files.");
        EVMU_LOG_PUSH();

        VMUFlashDirEntry* dirEntries[gyVmuFlashDirEntryCount(dev)];
        memset(dirEntries, 0, sizeof(VMUFlashDirEntry*)*gyVmuFlashDirEntryCount(dev));

        size_t beginBlocks = 0;
        size_t endBlocks = 0;
        for(int f = 0; f < fileCount; ++f) {
            dirEntries[f] = gyVmuFlashFileAtIndex(dev, f);
        }

        for(int f = 0; f < fileCount; ++f) {
            VMUFlashDirEntry* entry = dirEntries[f];
            beginBlocks += entry->fileSize;
            if(!entry) {
                EVMU_LOG_ERROR("Failed to retrieve directory entry for file: [index %d]", f);
                goto failure;
            }

            size_t bytes = gyVmuFlashFileDelete(dev, entry);
            endBlocks += bytes;
            if(!bytes) {
                EVMU_LOG_ERROR("Deleting file [%d] didn't free any blocks on device!", f);
                goto failure;
            }
        }

        //Ensure we actually got the device to a clean state
        int newFileCount = gyVmuFlashFileCount(dev);
        if(newFileCount) {
            EVMU_LOG_ERROR("Not all files were deleted: [%d files]", newFileCount);
            goto failure;
        }

        if(beginBlocks != endBlocks) {
            EVMU_LOG_ERROR("Byte count before clear [%d] does not equal cleared byte count [%d]!", beginBlocks, endBlocks);
            goto failure;
        }

        VMUFlashMemUsage memUsage = gyVmuFlashMemUsage(dev);
        if(memUsage.blocksFree < endBlocks) {
            EVMU_LOG_ERROR("Less filesystem blocks available than were just uninstalled! [free: %d, expected: >%d]", memUsage.blocksFree, endBlocks);
            goto failure;
        }

        for(int b = 0; b < gyVmuFlashBlockCount(dev); ++b) {
            const uint16_t* block = gyVmuFlashBlockFATEntry((VMUDevice*)dev, b);
            if(*block != VMU_FLASH_BLOCK_FAT_UNALLOCATED) EVMU_LOG_VERBOSE("FAT[%x] = %d", b, *block);
        }

        if(newUserSize != -1) gyVmuFlashBlockRoot(dev)->userSize = newUserSize;

        EVMU_LOG_POP(1);

        EVMU_LOG_VERBOSE("Reinstalling all files.");
        EVMU_LOG_PUSH();
        for(int f = 0; f < fileCount; ++f) {
            unsigned char tempImage[EVMU_FLASH_SIZE];
            //Read file from temporary device to temporary buffer
            const VMUFlashDirEntry* entry = gyVmuFlashFileAtIndex(&tempDevice, f);
            if(!entry) {
                EVMU_LOG_ERROR("Failed to retrieve directory entry on tempDevice for file: [file %d]", f);
                goto failure;
            }

            if(!gyVmuFlashFileRead(&tempDevice, entry, tempImage, 1)) {
                EVMU_LOG_ERROR("Failed to extract file from temporary device: [file %d]", f);
                goto failure;
            }

            //Write file from buffer to device
            VMU_LOAD_IMAGE_STATUS status;
            VMUFlashNewFileProperties fileProperties;
            gyVmuFlashNewFilePropertiesFromDirEntry(&fileProperties, entry);
            entry = gyVmuFlashFileCreate(dev, &fileProperties, tempImage, &status);
            if(!entry || status != VMU_LOAD_IMAGE_SUCCESS) {
                EVMU_LOG_ERROR("Failed to write file back to device: [file %d]", f);
                goto failure;
            }
        }

        //Verify that everything has been reinstalled.
        memUsage = gyVmuFlashMemUsage(dev);
        newFileCount = gyVmuFlashFileCount(dev);
        if(newFileCount != fileCount) {
            EVMU_LOG_ERROR("Final file count does not match initial file count! [%d => %d]", fileCount, newFileCount);
            goto failure;
        }

        if(memUsage.blocksUsed != origMemUsage.blocksUsed) {
            EVMU_LOG_ERROR("Final used block count does not match initial used block count! [%d => %d]", origMemUsage.blocksUsed, memUsage.blocksUsed);
            goto failure;
        }

        EVMU_LOG_POP(1);

    } else if(newUserSize != -1) gyVmuFlashBlockRoot(dev)->userSize = newUserSize;

    goto end;


failure:
    success = 0;
    memcpy(dev, &tempDevice, sizeof(VMUDevice));
    EVMU_LOG_POP(1);
end:
    EVMU_LOG_POP(1);
    return success;
}

int gyVmuFlashFileCount(const struct VMUDevice* dev) {
    int count = 0;
    const VMUFlashDirEntry* entry = NULL;

    for(int i = gyVmuFlashDirEntryCount(dev)-1; i >= 0; --i) {
        entry = gyVmuFlashDirEntryByIndex((VMUDevice*)dev, (uint16_t)i);
        if(entry && (entry->fileType == VMU_FLASH_FILE_TYPE_DATA || entry->fileType == VMU_FLASH_FILE_TYPE_GAME)) {
            ++count;
        }
    }
    return count;
}

void gyVmuFlashDirEntryName(const VMUFlashDirEntry* entry, char* buffer) {
    memcpy(buffer, entry->fileName, VMU_FLASH_DIRECTORY_FILE_NAME_SIZE);
    buffer[VMU_FLASH_DIRECTORY_FILE_NAME_SIZE] = '\0';
    for(int i = VMU_FLASH_DIRECTORY_FILE_NAME_SIZE-1; i >= 0; --i) {
        if(buffer[i] == ' ') buffer[i] = '\0';
        else return;
    }
}

VMSFileInfo* gyVmuFlashDirEntryVmsHeader(const VMUDevice* dev, const struct VMUFlashDirEntry* dirEntry) {
    return (VMSFileInfo*)gyVmuFlashBlock(dev, dirEntry->firstBlock + dirEntry->headerOffset);
}

VMUFlashDirEntry* gyVmuFlashFileAtIndex(const VMUDevice* dev, int fileIdx) {
    int count = 0;
    VMUFlashDirEntry* entry = NULL;

    for(int i = gyVmuFlashDirEntryCount(dev)-1; i >= 0; --i) {
        entry = gyVmuFlashDirEntryByIndex((VMUDevice*)dev, (uint8_t)i);
        if(entry->fileType == VMU_FLASH_FILE_TYPE_DATA || entry->fileType == VMU_FLASH_FILE_TYPE_GAME) {
            if(fileIdx == count) {
                return entry;
            }
            if(++count > fileIdx) break;
        }
    }
    return NULL;
}

size_t gyVmuFlashFileReadBytes(struct VMUDevice* dev, const struct VMUFlashDirEntry* entry, unsigned char* buffer, size_t bytes, size_t offset, int includeHeader) {
    assert(entry && buffer);
    size_t bytesRead = 0;
    
    const VMSFileInfo* vmsHeader = gyVmuFlashDirEntryVmsHeader(dev, entry);
    if(!vmsHeader) return bytesRead;
    //else if(!includeHeader) startOffset += gyVmuVmsFileInfoHeaderSize(vmsHeader);

    size_t startBlockNum = offset / VMU_FLASH_BLOCK_SIZE;
    size_t endBlockNum = (offset + bytes) / VMU_FLASH_BLOCK_SIZE;

    size_t numBlocks = endBlockNum - startBlockNum;
    uint16_t curBlock = entry->firstBlock;

    //Seek to startBlock
    for(unsigned b = 0; b < startBlockNum; ++b) {
        curBlock = gyVmuFlashBlockNext(dev, curBlock);
        if(curBlock == VMU_FLASH_BLOCK_FAT_UNALLOCATED) return bytesRead; //FUCKED
    }

    //Read start block (starting at offset)
    if(includeHeader || entry->headerOffset != curBlock) {
        size_t startBlockByteOffset = offset % VMU_FLASH_BLOCK_SIZE;
        size_t startBlockBytesLeft = VMU_FLASH_BLOCK_SIZE - startBlockByteOffset;
        size_t startBlockBytes = (bytes < startBlockBytesLeft)? bytes : startBlockBytesLeft;
        const unsigned char* firstBlockData = gyVmuFlashBlock(dev, curBlock);
        if(!firstBlockData) return bytesRead;
        memcpy(buffer, firstBlockData+startBlockByteOffset, startBlockBytes);
        bytesRead += startBlockBytes;
    }
    curBlock = gyVmuFlashBlockNext(dev, curBlock);

    //Read each block beyond the start block
    //for(unsigned b = 1; b < numBlocks; ++b) {
    while(bytesRead < bytes) {
        const unsigned char* data = gyVmuFlashBlock(dev, curBlock);
        if(!data) break; // Jesus fucking CHRIST

        if(includeHeader || curBlock != entry->headerOffset) {
            const size_t bytesLeft = bytes - bytesRead;
            const size_t byteCount = (VMU_FLASH_BLOCK_SIZE < bytesLeft)?
                        VMU_FLASH_BLOCK_SIZE : bytesLeft;

            memcpy(&buffer[bytesRead], data, byteCount);
            bytesRead += byteCount;
        }

        curBlock = gyVmuFlashBlockNext(dev, curBlock);
        if(curBlock == VMU_FLASH_BLOCK_FAT_UNALLOCATED) break;
    }

    return bytesRead;
}

void gyVmuFlashNewFilePropertiesFromVmi(VMUFlashNewFileProperties* fileProperties, const VMIFileInfo* vmi) {
    assert(fileProperties && vmi);

    memcpy(fileProperties->fileName, vmi->fileNameOnVms, VMU_FLASH_DIRECTORY_FILE_NAME_SIZE);
    fileProperties->fileSizeBytes = vmi->fileSize;

    switch((vmi->fileMode&VMU_VMI_FILE_INFO_FILE_MODE_GAME_MASK) >> VMU_VMI_FILE_INFO_FILE_MODE_GAME_POS) {
    case VMI_FILE_MODE_GAME_DATA:
        fileProperties->fileType = VMU_FLASH_FILE_TYPE_DATA;
        break;
    case VMI_FILE_MODE_GAME_GAME:
        fileProperties->fileType = VMU_FLASH_FILE_TYPE_GAME;
        break;
    default:
        fileProperties->fileType = VMU_FLASH_FILE_TYPE_NONE;
    }

    switch((vmi->fileMode&VMU_VMI_FILE_INFO_FILE_MODE_PROTECT_MASK) >> VMU_VMI_FILE_INFO_FILE_MODE_PROTECT_POS) {
    case VMI_FILE_MODE_PROTECTED_COPY_OK:
        fileProperties->copyProtection = VMU_FLASH_COPY_PROTECTION_COPY_OK;
        break;
    case VMI_FILE_MODE_PROTECTED_COPY_PROTECTED:
        fileProperties->copyProtection = VMU_FLASH_COPY_PROTECTION_COPY_PROTECTED;
        break;
    default:
        fileProperties->copyProtection = VMU_FLASH_COPY_PROTECTION_COPY_UNKNOWN;
    }
}

void gyVmuFlashVmiFromDirEntry(VMIFileInfo* vmi, const VMUDevice* dev, const VMUFlashDirEntry* entry, const char* vmsName) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

    const VMSFileInfo* vms = gyVmuFlashDirEntryVmsHeader(dev, entry);

    memset(vmi, 0, sizeof(VMIFileInfo));

    //Description (VMS DC Description)
    memcpy(vmi->description, vms->dcDesc, VMU_VMS_FILE_INFO_DC_DESC_SIZE);

    //Copyright
    memcpy(vmi->copyright, VMU_FLASH_VMI_EXPORT_COPYRIGHT_STRING, sizeof(VMU_FLASH_VMI_EXPORT_COPYRIGHT_STRING));


    //Creation Date
    vmi->creationYear       = GBL_BCD_BYTE_PACK(tm->tm_year);
    vmi->creationMonth      = GBL_BCD_BYTE_PACK(tm->tm_mon+1);
    vmi->creationDay        = GBL_BCD_BYTE_PACK(tm->tm_mday);
    vmi->creationWeekday    = GBL_BCD_BYTE_PACK(tm->tm_wday);
    vmi->creationHour       = GBL_BCD_BYTE_PACK(tm->tm_hour);
    vmi->creationMinute     = GBL_BCD_BYTE_PACK(tm->tm_min);
    vmi->creationSecond     = GBL_BCD_BYTE_PACK(tm->tm_sec);

    //VMI Version
    vmi->vmiVersion = VMU_VMI_VERSION;

    //File Number
    vmi->fileNumber = 1;

    //.VMS Resource Name
    size_t nameLen = strlen(vmsName);
    if(nameLen > VMU_VMI_FILE_INFO_VMS_RESOURCE_NAME_SIZE) {
        EVMU_LOG_WARNING("Converting FlashEntry to VMI File: VMS Resource name length is too long! [name: %s, bytes: %u/%u]",
               vmsName, nameLen, VMU_VMI_FILE_INFO_VMS_RESOURCE_NAME_SIZE);
        nameLen = VMU_VMI_FILE_INFO_VMS_RESOURCE_NAME_SIZE;
    }
    memcpy(vmi->vmsResourceName, vmsName, nameLen);

    //Filename on VMS (VMS VMU Description)
    memcpy(vmi->fileNameOnVms, entry->fileName, VMU_FLASH_DIRECTORY_FILE_NAME_SIZE);
    vmi->fileSize = vms->dataBytes + gyVmuVmsFileInfoHeaderSize(vms);

    //File Mode
    VMI_FILE_MODE_GAME      mode;
    switch(entry->fileType) {
    case VMU_FLASH_FILE_TYPE_GAME:
        mode = VMI_FILE_MODE_GAME_GAME;
        break;
    case VMU_FLASH_FILE_TYPE_DATA:
    default:
        mode = VMI_FILE_MODE_GAME_DATA;
        break;
    }

    VMI_FILE_MODE_PROTECTED copy;
    switch(entry->copyProtection) {
    case VMU_FLASH_COPY_PROTECTION_COPY_PROTECTED:
        copy = VMI_FILE_MODE_PROTECTED_COPY_PROTECTED;
        break;
    case VMU_FLASH_COPY_PROTECTION_COPY_OK:
    default:
        copy = VMI_FILE_MODE_PROTECTED_COPY_OK;
        break;
    };

    vmi->fileMode = (uint16_t)(mode << VMU_VMI_FILE_INFO_FILE_MODE_GAME_POS)
                  | (uint16_t)(copy << VMU_VMI_FILE_INFO_FILE_MODE_PROTECT_POS);



    //Unknown
    vmi->unknown = 0;

    //File Size
    vmi->fileSize = (entry->fileType == VMU_FLASH_FILE_TYPE_DATA)?
                gyVmuVmsFileInfoHeaderSize(vms) + vms->dataBytes :
                entry->fileSize * VMU_FLASH_BLOCK_SIZE;

    //Checksum
    vmi->checksum = gyVmuVMIChecksumGenerate(vmi);
}


void gyVmuFlashNewFilePropertiesFromDirEntry(VMUFlashNewFileProperties* fileProperties, const VMUFlashDirEntry* entry) {
    assert(fileProperties && entry);

    memcpy(fileProperties->fileName, entry->fileName, VMU_FLASH_DIRECTORY_FILE_NAME_SIZE);
    fileProperties->fileSizeBytes = entry->fileSize * VMU_FLASH_BLOCK_SIZE;
    fileProperties->fileType = entry->fileType;
    fileProperties->copyProtection = entry->copyProtection;
}

#if 0
const VMUFlashDirEntry* gyVmuFlashLoadImageVmsVmi(struct VMUDevice* dev, const char* vmiPath, const char* vmsPath, VMU_LOAD_IMAGE_STATUS* status) {
    const VMUFlashDirEntry* dirEntry = NULL;
    VMIFileInfo vmi;

    EVMU_LOG_VERBOSE("Loading VMI/VMS images into flash: [%s]", vmsPath);
    EVMU_LOG_PUSH();

    if(!gyVmuFlashLoadVMI(&vmi, vmiPath)) {
        EVMU_LOG_ERROR("Failed to load VMI file: [%s]", vmiPath);
        *status = VMU_LOAD_IMAGE_OPEN_FAILED;

    } else {
        gyVmuFlashPrintVMIFileInfo(&vmi);
        const VMI_FILE_MODE_GAME vmiFileType = (VMI_FILE_MODE_GAME)(vmi.fileMode&VMU_VMI_FILE_INFO_FILE_MODE_GAME_MASK);

        //Check if we're trying to load a game and already have a game!
        if(vmiFileType == VMI_FILE_MODE_GAME_GAME && gyVmuFlashDirEntryGame(dev)) {
            EVMU_LOG_WARNING("Game file already exists in current flash image!");
            *status = VMU_LOAD_IMAGE_GAME_DUPLICATE;
        } else {

            GYFile* fp = NULL;
            int retVal = gyFileOpen(vmsPath, "rb", &fp);

            if(!retVal || !fp) {
                EVMU_LOG_ERROR("Failed to open VMS file: [%s]!", vmsPath);
                *status = VMU_LOAD_IMAGE_OPEN_FAILED;
            } else {
                size_t fileSize = 0;
                retVal = gyFileLength(fp, &fileSize);

                if(!retVal || !fileSize) {
                    EVMU_LOG_ERROR("Failed to retrieve file size.");
                    *status = VMU_LOAD_IMAGE_READ_FAILED;

                } else {
                    size_t blockSize = fileSize / VMU_FLASH_BLOCK_SIZE;
                    VMUFlashMemUsage flashMem = gyVmuFlashMemUsage(dev);

                    if(flashMem.blocksFree < blockSize) {
                        EVMU_LOG_ERROR("VMU does not have enough free blocks to load file: [%d/%d]", flashMem.blocksFree, blockSize);
                        *status = VMU_LOAD_IMAGE_INADEQUATE_FREE_BLOCKS;
                    } else {

                        unsigned char* data = malloc(fileSize);
                        size_t bytesRead = 0;
                        retVal = gyFileRead(fp, data, fileSize, &bytesRead);

                        if(!retVal || bytesRead != fileSize) {
                            EVMU_LOG_ERROR("Failed to read file contents! [Bytes read: %d/%d]", bytesRead, fileSize);
                            *status = VMU_LOAD_IMAGE_READ_FAILED;
                        } else {
                            VMSFileInfo* vms = (vmiFileType == VMI_FILE_MODE_GAME_GAME)?
                                        (VMSFileInfo*)&data[VMU_FLASH_GAME_VMS_HEADER_OFFSET] :
                                        (VMSFileInfo*)data;

                            EVMU_LOG_VERBOSE("Loaded VMS Image into RAM. [%d bytes]", bytesRead);
                            gyVmuPrintVMSFileInfo(vms);

                            VMUFlashNewFileProperties fileProperties;
                            gyVmuFlashNewFilePropertiesFromVmi(&fileProperties, &vmi);

                            dirEntry = gyVmuFlashFileCreate(dev, &fileProperties, data, status);
                        }

                        free(data);
                    }
                }

                retVal = gyFileClose(&fp);
                if(!retVal) {
                    EVMU_LOG_ERROR("Failed to close file handler!");
                }
            }
        }
    }
    EVMU_LOG_POP(1);
    return dirEntry;
}
#endif


int gyVmuVmiFindVmsPath(const char* vmiPath, char* vmsPath) {
    int success = 0;
    char basePath[GYRO_PATH_MAX_SIZE] = { '\0' };
    char tmpVmiPath[GYRO_PATH_MAX_SIZE] = { '\0' };

    EVMU_LOG_VERBOSE("Trying to find VMS file corresponding VMI: [%s]", vmiPath);
    EVMU_LOG_PUSH();

    strcpy(tmpVmiPath, vmiPath);
    const char*curTok  = strtok(tmpVmiPath, ".");
    strcpy(basePath, curTok);

    strcpy(vmsPath, basePath);
    strcat(vmsPath, ".vms");
    EVMU_LOG_VERBOSE("Trying the same filename with .VMS extension: [%s]", vmsPath);
    EVMU_LOG_PUSH();
    FILE* fp = fopen(vmsPath, "rb");
    int wasOpen = (int)fp;
    fclose(fp);

    if(/*retVal &&*/ wasOpen) {
        EVMU_LOG_VERBOSE("Success! File exists.");
        EVMU_LOG_POP(1);
        success = 1;
        goto end;
    }
    EVMU_LOG_VERBOSE("Nope.");
    EVMU_LOG_POP(1);
    VMIFileInfo vmiHeader;
    char vmsFileName[VMU_VMI_FILE_INFO_VMS_RESOURCE_NAME_SIZE] = { '\0' };

    if(gyVmuFlashLoadVMI(&vmiHeader, vmiPath)) {
        gyVmuVmiFileInfoResourceNameGet(&vmiHeader, vmsFileName);

        for(int i = strlen(basePath)-1; i >= 0; --i) {
            if(basePath[i] == '\\' || basePath[i] == '/') {
                basePath[i+1] = '\0';
                break;
            }
        }

        strcat(basePath, vmsFileName);
        strcat(basePath, ".vms");

        EVMU_LOG_VERBOSE("Trying filename referenced by VM file: [%s]", basePath);
        EVMU_LOG_PUSH();

        fp = fopen(basePath, "rb");
        wasOpen = (int)fp;
        fclose(fp);

        if(/*retVal &&*/ wasOpen) {
            EVMU_LOG_VERBOSE("Success. File exists.");
            strcpy(vmsPath, basePath);
            success = 1;

        } else {
            success = 0;
        }
        EVMU_LOG_POP(1);

    } else {
        success = 0;
    }

end:
    if(!success) EVMU_LOG_WARNING("None found!");
    else EVMU_LOG_VERBOSE("Found VMS: [%s]", vmsPath);
    EVMU_LOG_POP(1);
    return success;
}

int gyVmuVmsFindVmiPath(const char* vmsPath, char* vmiPath) {
    assert(vmsPath);
    int success = 0;
    char basePath[GYRO_PATH_MAX_SIZE] = { '\0' };
    char tmpVmsPath[GYRO_PATH_MAX_SIZE] = { '\0' };
    EVMU_LOG_VERBOSE("Trying to find VMI file corresponding VMS: [%s]", vmsPath);
    EVMU_LOG_PUSH();

    strcpy(tmpVmsPath, vmsPath);
    const char*curTok  = strtok(tmpVmsPath, ".");
    strcpy(basePath, curTok);

    strcat(basePath, ".vmi");
    FILE* fp = fopen(basePath, "rb");
    int wasOpen = (int)fp;
    if(fp) fclose(fp);

    if(/*!retVal ||*/ !wasOpen) {
        EVMU_LOG_WARNING("None found!");
        success = 0;
    } else {
        strcpy(vmiPath, basePath);
        EVMU_LOG_VERBOSE("Found VMI: [%s]", vmiPath);
        success = 1;
    }
    EVMU_LOG_POP(1);
    return success;
}

VMUFlashDirEntry* gyVmuFlashLoadImage(VMUDevice* dev, const char* path, VMU_LOAD_IMAGE_STATUS* status) {
    char tempPath[GYRO_PATH_MAX_SIZE];
    char basePath[GYRO_PATH_MAX_SIZE];
    char fileName[GYRO_PATH_MAX_SIZE];
    int success = 0;
    VMUFlashDirEntry* entry = NULL;

    //Extract file name
    int nameStartPos = strlen(path)-1;
    for(; nameStartPos >= 0; --nameStartPos) {
        if(path[nameStartPos] == '/' || path[nameStartPos] == '\\') {
           ++nameStartPos;
            break;
        }
    }

    strcpy(fileName, &path[nameStartPos]);

    strcpy(tempPath, path);
    const char*curTok  = strtok(tempPath, ".");
    strcpy(basePath, curTok);
    const char*prevTok = NULL;
    char ext[100] = { 0 };

    EVMU_LOG_VERBOSE("Loading Generic Flash Image [%s]", path);
    EVMU_LOG_PUSH();

    if(dev->lcdFile) {
        gyVmuLcdFileUnload(dev->lcdFile);
        dev->lcdFile = NULL;
    }

    //Load files with special file names
    if(strcmp(fileName, VMU_ICONDATA_VMS_FILE_NAME) == 0 ||
            strcmp(fileName, "ICONDATA.VMS") == 0)
    {
        entry = gyVmuFlashLoadIconDataVms(dev, path, status);

    } else {    //Load based on file extension

        while(curTok) {
            prevTok = curTok;
            curTok = strtok(NULL, ".");
        }

        if(prevTok) {
            for(unsigned i = 0; i < strlen(prevTok); ++i)
                ext[i] = tolower(prevTok[i]);

            if(!strcmp(ext, "bin") || !strcmp(ext, "vmu")) {
                entry = gyVmuFlashLoadImageBin(dev, path, status);
            } else if(!strcmp(ext, "dcm")) {
                entry = gyVmuFlashLoadImageDcm(dev, path, status);
            } else if(!strcmp(ext, "dci")) {
                entry = gyVmuFlashLoadImageDci(dev, path, status);

            } else if(!strcmp(ext, "vms")) {
                //We have to find the corresponding VMI file
                char vmiPath[GYRO_PATH_MAX_SIZE];
                if(gyVmuVmsFindVmiPath(path, vmiPath)) {
                    entry = gyVmuFlashLoadImageVmiVms(dev, vmiPath, path, status);
                } else {
                    *status = VMU_LOAD_IMAGE_VMS_NO_VMI;
                }
            } else if(!strcmp(ext, "vmi")) {
                //We have to find the corresponding VMS file
                char vmsPath[GYRO_PATH_MAX_SIZE];
                if(gyVmuVmiFindVmsPath(path, vmsPath)) {
                    entry = gyVmuFlashLoadImageVmiVms(dev, path, vmsPath, status);
                } else {
                    *status = VMU_LOAD_IMAGE_VMI_NO_VMS;
                }

            } else {
                *status = VMU_LOAD_IMAGE_UNKNOWN_FORMAT;
            }

        } else {
            EVMU_LOG_ERROR("No extension found on file!");

        }
    }

    EVMU_LOG_POP(1);
    return entry;

}

VMUFlashDirEntry* gyVmuFlashCreateFileVmiVms(struct VMUDevice* dev, const struct VMIFileInfo* vmi, const uint8_t* vms, VMU_LOAD_IMAGE_STATUS* status) {
    assert(dev && vmi && vms);

    EVMU_LOG_VERBOSE("Creating new flash file from raw VMI and VMS data.");
    EVMU_LOG_PUSH();
    gyVmuFlashPrintVMIFileInfo(vmi);

    const VMSFileInfo* vmsHeader = (VMSFileInfo*)((vmi->fileMode&VMU_VMI_FILE_INFO_FILE_MODE_GAME_MASK)>>VMU_VMI_FILE_INFO_FILE_MODE_GAME_POS == VMI_FILE_MODE_GAME_GAME)?
                &vms[VMU_FLASH_GAME_VMS_HEADER_OFFSET] :
                vms;
    gyVmuPrintVMSFileInfo(vmsHeader);

    VMUFlashNewFileProperties fileProperties;
    gyVmuFlashNewFilePropertiesFromVmi(&fileProperties, vmi);

    //If this is a special kind of file, lets print its extra data and shit
    if(memcmp(fileProperties.fileName, GYRO_VMU_EXTRA_BG_PVR_FILE_NAME, sizeof(fileProperties.fileName)) == 0) {
        VmuExtraBgPvrFileInfo payload;
        gyVmuExtraBgPvrFileInfo(vmsHeader, &payload);
        gyVmuExtraBgPvrFileInfoPrint(&payload);
    }

    VMUFlashDirEntry* dirEntry = gyVmuFlashFileCreate(dev, &fileProperties, vms, status);

    EVMU_LOG_POP(1);

    return dirEntry;
}

VMUFlashDirEntry* gyVmuFlashLoadImageVmiVms(struct VMUDevice* dev, const char* vmipath, const char* vmspath, VMU_LOAD_IMAGE_STATUS* status) {
    VMUFlashDirEntry* dirEntry = NULL;
    EVMU_LOG_VERBOSE("Load VMI+VMS file pair: [vmi: %s, vms: %s]", vmipath, vmspath);
    EVMU_LOG_PUSH();

    VMIFileInfo vmi;
    if(!gyVmuFlashLoadVMI(&vmi, vmipath)) {
        *status = VMU_LOAD_IMAGE_OPEN_FAILED;
        goto end;
    }

    uint8_t* vms = gyVmuFlashLoadVMS(vmspath, NULL);
    if(!vms) {
        *status = VMU_LOAD_IMAGE_OPEN_FAILED;
        goto end;
    }

    dirEntry = gyVmuFlashCreateFileVmiVms(dev, &vmi, vms, status);

    free(vms);

end:
    EVMU_LOG_POP(1);
    return dirEntry;
}


VMUFlashDirEntry* gyVmuFlashLoadImageDcm(struct VMUDevice* dev, const char* path, VMU_LOAD_IMAGE_STATUS* status) {
    EvmuDevice_* pDevice_ = EVMU_DEVICE_PRISTINE(dev);
    FILE* file = NULL;

    EVMU_LOG_VERBOSE("Loading DCM Flash Image from file: [%s]", path);
    EVMU_LOG_PUSH();

    if (!(file = fopen(path, "rb"))) {
        EVMU_LOG_ERROR("Could not open binary file for reading!");
        EVMU_LOG_POP(1);
        *status = VMU_LOAD_IMAGE_OPEN_FAILED;
        return NULL;
    }

    //Clear ROM
    memset(pDevice_->pMemory->flash, 0, sizeof(pDevice_->pMemory->flash));

    size_t bytesRead   = 0;
    size_t bytesTotal  = 0;

    size_t fileLen = 0;
    fseek(file, 0, SEEK_END); // seek to end of file
    fileLen = ftell(file); // get current file pointer
    fseek(file, 0, SEEK_SET); // seek back to beginning of file

    size_t toRead = fileLen < sizeof(pDevice_->pMemory->flash)? fileLen : sizeof(pDevice_->pMemory->flash);

    if(fileLen != sizeof(pDevice_->pMemory->flash)) {
        EVMU_LOG_WARNING("File size does not match flash size. Probaly not a legitimate image. [File Size: %u, Flash Size: %u]", fileLen, sizeof(pDevice_->pMemory->flash));
    }

    int retVal = fread(pDevice_->pMemory->flash, 1, toRead, file);

    if(!retVal || toRead != bytesRead) {
        EVMU_LOG_ERROR("All bytes were not read properly! [Bytes Read: %u/%u]", bytesRead, toRead);
    }

    fclose(file);

    gyVmuFlashNexusByteOrder(pDevice_->pMemory->flash, EVMU_FLASH_SIZE);

    EVMU_LOG_VERBOSE("Read %d bytes.", bytesTotal);
    //assert(bytesTotal >= 0);
    //assert(bytesTotal == sizeof(pDevice_->pMemory->flash));

    *status = VMU_LOAD_IMAGE_SUCCESS;
    gyVmuFlashRootBlockPrint(dev);
    gyVmuFlashPrintFilesystem(dev);

    if(!gyVmuFlashCheckFormatted(dev)) {
        strncpy(_lastErrorMsg, "Root Block does not contain the proper format sequence!", sizeof(_lastErrorMsg));
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        *status = VMU_LOAD_IMAGE_FLASH_UNFORMATTED;
    }

    EVMU_LOG_POP(1);

    return NULL;
}

VMUFlashDirEntry* gyVmuFlashLoadImageDci(struct VMUDevice* dev, const char* path, VMU_LOAD_IMAGE_STATUS* status) {
    VMUFlashDirEntry tempEntry;
    VMUFlashDirEntry* entry = NULL;
    uint8_t dataBuffer[EVMU_FLASH_SIZE] = { 0 };

    EVMU_LOG_VERBOSE("Loading DCI image from file: [%s]", path);
    EVMU_LOG_PUSH();

    FILE* fp = NULL;
    if(!(fp = fopen(path, "rb"))) {
        snprintf(_lastErrorMsg,
                 sizeof(_lastErrorMsg),
                 "Failed to open file!");
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        *status = VMU_LOAD_IMAGE_OPEN_FAILED;
        goto end;
    }

    size_t bytesRead = 0;
    if((bytesRead = fread(&tempEntry, 1, sizeof(VMUFlashDirEntry), fp) )
            != sizeof(VMUFlashDirEntry))
    {
        snprintf(_lastErrorMsg,
                 sizeof(_lastErrorMsg),
                 "Failed to read directory entry! [Bytes read: %d/%d]",
                 bytesRead,
                 sizeof(VMUFlashDirEntry));
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        *status = VMU_LOAD_IMAGE_READ_FAILED;
        goto cleanup_file;
    }

    if(tempEntry.fileType == VMU_FLASH_FILE_TYPE_GAME && gyVmuFlashDirEntryGame(dev)) {
        snprintf(_lastErrorMsg,
                 sizeof(_lastErrorMsg),
                 "Only one GAME file may be present at a time, and the current image already has one!");
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        *status = VMU_LOAD_IMAGE_GAME_DUPLICATE;
        goto cleanup_file;
    }

    VMUFlashMemUsage memUsage = gyVmuFlashMemUsage(dev);
    if(memUsage.blocksFree < tempEntry.fileSize) {
        snprintf(_lastErrorMsg,
                 sizeof(_lastErrorMsg),
                 "Not enough free blocks available on device. [Available: %d, Required: %d]",
                 memUsage.blocksFree,
                 tempEntry.fileSize);
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        *status = VMU_LOAD_IMAGE_INADEQUATE_FREE_BLOCKS;
        goto cleanup_file;
    }

#if 0
    if(gyVmuFlashDirEntryFind(dev, tempEntry.fileName)) {
        snprintf(_lastErrorMsg,
                 sizeof(_lastErrorMsg),
                 "A file already exists with the same name!");
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        *status = VMU_LOAD_IMAGE_NAME_DUPLICATE;
        goto cleanup_file;
    }
#endif

    if(!(entry = gyVmuFlashDirEntryAlloc(dev))) {
        snprintf(_lastErrorMsg,
                 sizeof(_lastErrorMsg),
                 "Failed to allocate new Flash Directory Entry!");
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        *status = VMU_LOAD_IMAGE_FILES_MAXED;
        goto cleanup_file;
    }

    const size_t fileSize = tempEntry.fileSize * VMU_FLASH_BLOCK_SIZE;
    if((bytesRead = fread(dataBuffer, 1, fileSize, fp)) != fileSize)
    {
        snprintf(_lastErrorMsg,
                 sizeof(_lastErrorMsg),
                 "Failed to read entire contents of file. [Bytes read: %d/%d]",
                 bytesRead,
                 fileSize);
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        *status = VMU_LOAD_IMAGE_READ_FAILED;
        goto cleanup_file;
    }

    gyVmuFlashNexusByteOrder(dataBuffer, tempEntry.fileSize * VMU_FLASH_BLOCK_SIZE);

    VMUFlashNewFileProperties properties;
    gyVmuFlashNewFilePropertiesFromDirEntry(&properties, &tempEntry);

    entry = gyVmuFlashFileCreate(dev, &properties, dataBuffer, status);

cleanup_file:
    if(fclose(fp)) {
        EVMU_LOG_WARNING("File was not closed gracefully for some reason...");
    }
end:
    EVMU_LOG_POP(1);
    return entry;
}

/* Time to de-fuck the bytes for a DCI image!
 * Shit's all in reverse byte-order, in sets of 4
 * If this shit isn't divisible by four, PAD IT!!!
 */
void gyVmuFlashNexusByteOrder(uint8_t* data, size_t bytes) {
    assert(!(bytes % 4));
    size_t wordCount = bytes / 4;
    if(bytes % 4) ++wordCount;
    for(size_t w = 0; w < wordCount; ++w) {
        for(int b = 0; b < 2; ++b) {
            const uint8_t tempByte  = data[w*4 + b];
             data[w*4 + b]          = data[w*4 + 4-b-1];
             data[w*4 + 4-b-1]      = tempByte;
        }
    }
}


VMUFlashDirEntry* gyVmuFlashLoadImageBin(struct VMUDevice* dev, const char* path, VMU_LOAD_IMAGE_STATUS* status) {
    EvmuDevice_* pDevice_ = EVMU_DEVICE_PRISTINE(dev);
    FILE* file = NULL;

    EVMU_LOG_VERBOSE("Loading .VMU/.BIN Flash image from file: [%s]", path);
    EVMU_LOG_PUSH();

    if (!(file = fopen(path, "rb"))) {
        EVMU_LOG_ERROR("Could not open binary file for reading!");
        EVMU_LOG_POP(1);
        *status = VMU_LOAD_IMAGE_OPEN_FAILED;
        return NULL;
    }

    //Clear ROM
    memset(pDevice_->pMemory->flash, 0, sizeof(pDevice_->pMemory->flash));

    size_t bytesRead   = 0;
    size_t bytesTotal  = 0;

    size_t fileLen = 0;
    fseek(file, 0, SEEK_END); // seek to end of file
    fileLen = ftell(file); // get current file pointer
    fseek(file, 0, SEEK_SET); // seek back to beginning of file

    size_t toRead = fileLen < sizeof(pDevice_->pMemory->flash)? fileLen : sizeof(pDevice_->pMemory->flash);

    if(fileLen != sizeof(pDevice_->pMemory->flash)) {
        EVMU_LOG_WARNING("File size does not match flash size. Probaly not a legitimate image. [File Size: %u, Flash Size: %u]", fileLen, sizeof(pDevice_->pMemory->flash));
    }

    bytesRead = fread(pDevice_->pMemory->flash, 1, toRead, file);

    if(/*!retVal ||*/ toRead != bytesRead) {
        EVMU_LOG_ERROR("All bytes were not read properly! [Bytes Read: %u/%u]", bytesRead, toRead);
    }

    fclose(file);

    EVMU_LOG_VERBOSE("Read %d bytes.", bytesTotal);
    //assert(bytesTotal >= 0);
    //assert(bytesTotal == sizeof(pDevice_->pMemory->flash));

    *status = VMU_LOAD_IMAGE_SUCCESS;
    gyVmuFlashRootBlockPrint(dev);
    gyVmuFlashPrintFilesystem(dev);

    if(!gyVmuFlashCheckFormatted(dev)) {
        strncpy(_lastErrorMsg, "Root Block does not contain the proper format sequence!", sizeof(_lastErrorMsg));
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        *status = VMU_LOAD_IMAGE_FLASH_UNFORMATTED;
    }

    EVMU_LOG_POP(1);

    return NULL;
}

int gyVmuFlashFormatDefault(struct VMUDevice* dev) {
    VMUFlashRootBlock root;
    memset(&root, 0, sizeof(VMUFlashRootBlock));

    root.volumeLabel.vmu.customColor    = 0;
    root.volumeLabel.vmu.a              = 255;
    root.volumeLabel.vmu.r              = 255;
    root.volumeLabel.vmu.g              = 255;
    root.volumeLabel.vmu.b              = 255;
    root.totalSize                      = 256;
    root.partition                      = 0;
    root.rootBlock                      = VMU_FLASH_BLOCK_ROOT;
    root.fatBlock                       = VMU_FLASH_BLOCK_FAT_DEFAULT;
    root.fatSize                        = VMU_FLASH_BLOCK_FAT_SIZE_DEFAULT;
    root.dirBlock                       = VMU_FLASH_BLOCK_DIRECTORY_DEFAULT;
    root.dirSize                        = VMU_FLASH_BLOCK_DIRECTORY_SIZE_DEFAULT;
    root.iconShape                      = 0;
    root.saveAreaBlock                  = 31;
    root.saveAreaSize                   = 0;
    root.userSize                       = 200;
    root.executionFile                  = 0;
    return gyVmuFlashFormat(dev, &root);
}

int gyVmuFlashFormat(struct VMUDevice* dev, const VMUFlashRootBlock* rootVal) {
    EvmuDevice_* pDevice_ = EVMU_DEVICE_PRISTINE(dev);
    assert(sizeof(VMUFlashDirEntry) == 32);
    int success = 1;
    //Clear all of Flash
    memset(pDevice_->pMemory->flash, 0, gyVmuFlashBytes(dev));
    VMUFlashRootBlock* root = gyVmuFlashBlockRoot(dev);

    //Copy in new root values, so we can start using accessors without shitting
    memcpy(root, rootVal, sizeof(VMUFlashRootBlock));

    for(int b = 0; b < gyVmuFlashBlockCount(dev); ++b) {
        uint16_t* fatEntry = gyVmuFlashBlockFATEntry(dev, b);
        *fatEntry = VMU_FLASH_BLOCK_FAT_UNALLOCATED;
    }

    //Initialize all the stupid, default, static root-block shit
    memset(root->formatted, VMU_FLASH_ROOT_BLOCK_FORMATTED_BYTE, VMU_FLASH_ROOT_BLOCK_FORMATTED_SIZE);

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    root->timeStamp[VMU_FLASH_DIRECTORY_DATE_CENTURY]  = GBL_BCD_BYTE_PACK(tm->tm_year/100+19);
    root->timeStamp[VMU_FLASH_DIRECTORY_DATE_YEAR]     = GBL_BCD_BYTE_PACK(tm->tm_year%100);
    root->timeStamp[VMU_FLASH_DIRECTORY_DATE_MONTH]    = GBL_BCD_BYTE_PACK(tm->tm_mon+1);
    root->timeStamp[VMU_FLASH_DIRECTORY_DATE_DAY]      = GBL_BCD_BYTE_PACK(tm->tm_mday);
    root->timeStamp[VMU_FLASH_DIRECTORY_DATE_HOUR]     = GBL_BCD_BYTE_PACK(tm->tm_hour);
    root->timeStamp[VMU_FLASH_DIRECTORY_DATE_MINUTE]   = GBL_BCD_BYTE_PACK(tm->tm_min);
    root->timeStamp[VMU_FLASH_DIRECTORY_DATE_SECOND]   = GBL_BCD_BYTE_PACK(tm->tm_sec);
    root->timeStamp[VMU_FLASH_DIRECTORY_DATE_WEEKDAY]  = GBL_BCD_BYTE_PACK(tm->tm_wday);

    //memset(root->unused1, 0, VMU_FLASH_ROOT_BLOCK_UNUSED1_SIZE);

    //No fucking idea what these are, but the Sega-formatted VMUs have them!!!
    uint8_t* rootBytes = (uint8_t*)root;

    //allocate FAT block in the FAT
    uint16_t* fatFat = gyVmuFlashBlockFATEntry(dev, gyVmuFlashBlockFat(dev));
    if(!fatFat) {
        EVMU_LOG_ERROR("Could not retrieve FAT entry for FAT block: %d", gyVmuFlashBlockFat(dev));
        success = 0;
    } else *fatFat = VMU_FLASH_BLOCK_FAT_LAST_IN_FILE;

    //allocate Root block in the FAT
    uint16_t* rootFat = gyVmuFlashBlockFATEntry(dev, VMU_FLASH_BLOCK_ROOT);
    if(!fatFat) {
        EVMU_LOG_ERROR("Could not retrieve FAT entry for Root block: %d", VMU_FLASH_BLOCK_ROOT);
        success = 0;
    } else *rootFat = VMU_FLASH_BLOCK_FAT_LAST_IN_FILE;

    //allocate Directory blocks in the FAT
    int dirStartBlock = root->dirBlock;
    int dirEndBlock = root->dirBlock - root->dirSize + 1;
    for(int b = dirStartBlock; b >= dirEndBlock; --b) {
        uint16_t* fatEntry = gyVmuFlashBlockFATEntry(dev, b);
        if(!fatEntry) {
            EVMU_LOG_ERROR("Couldn't find fat entry [%d] when allocating directory!", b);
            success = 0;
        } else *fatEntry = (b == dirEndBlock)? VMU_FLASH_BLOCK_FAT_LAST_IN_FILE : b-1;
    }

    return success;
}

void gyVmuFlashRootBlockPrint(const struct VMUDevice* dev) {
    EVMU_LOG_VERBOSE("VMU Flash - Root [block: %d]", VMU_FLASH_BLOCK_ROOT);
    EVMU_LOG_PUSH();

    VMUFlashRootBlock* root = gyVmuFlashBlockRoot((VMUDevice*)dev);
    char buffer[100];
    char dateStr[200];
    int formatOk = 1;

    for(int i = 0; i < VMU_FLASH_ROOT_BLOCK_FORMATTED_SIZE; ++i) {
        int val = root->formatted[i];
        if(val != VMU_FLASH_ROOT_BLOCK_FORMATTED_BYTE) {
            EVMU_LOG_WARNING("Root.formattedSegment[%d] = %d (should be %d)", i, val, VMU_FLASH_ROOT_BLOCK_FORMATTED_BYTE);
            formatOk = 0;
        }
    }

    EVMU_LOG_VERBOSE("%-20s: %40s", "Formatted",        formatOk? "Ok" : "Unformatted");
    EVMU_LOG_VERBOSE("%-20s: %40s", "Use Custom Color", root->volumeLabel.vmu.customColor? "Yes" : "No");
    sprintf(buffer, "<%d, %d, %d, %d>", root->volumeLabel.vmu.r, root->volumeLabel.vmu.g, root->volumeLabel.vmu.b, root->volumeLabel.vmu.a);
    EVMU_LOG_VERBOSE("%-20s: %40s", "Color",            buffer);


    const char* pWeekDay;
    switch(GBL_BCD_BYTE_UNPACK(root->timeStamp[VMU_FLASH_DIRECTORY_DATE_WEEKDAY])) {
    case 0: pWeekDay = "Sunday"; break;
    case 1: pWeekDay = "Monday"; break;
    case 2: pWeekDay = "Tuesday"; break;
    case 3: pWeekDay = "Wednesday"; break;
    case 4: pWeekDay = "Thursday"; break;
    case 5: pWeekDay = "Friday"; break;
    case 6: pWeekDay = "Saturday"; break;
    default: pWeekDay = "Invalid Weekday"; break;
    }

    sprintf(dateStr, "%u/%u/%u%u (%s) %u:%u:%u",
            GBL_BCD_BYTE_UNPACK(root->timeStamp[VMU_FLASH_DIRECTORY_DATE_MONTH]),
            GBL_BCD_BYTE_UNPACK(root->timeStamp[VMU_FLASH_DIRECTORY_DATE_DAY]),
            GBL_BCD_BYTE_UNPACK(root->timeStamp[VMU_FLASH_DIRECTORY_DATE_CENTURY]),
            GBL_BCD_BYTE_UNPACK(root->timeStamp[VMU_FLASH_DIRECTORY_DATE_YEAR]),
            pWeekDay,
            GBL_BCD_BYTE_UNPACK(root->timeStamp[VMU_FLASH_DIRECTORY_DATE_HOUR]),
            GBL_BCD_BYTE_UNPACK(root->timeStamp[VMU_FLASH_DIRECTORY_DATE_MINUTE]),
            GBL_BCD_BYTE_UNPACK(root->timeStamp[VMU_FLASH_DIRECTORY_DATE_SECOND]));

    EVMU_LOG_VERBOSE("%-20s: %40s", "Date Formatted",   dateStr);
    EVMU_LOG_VERBOSE("%-20s: %40u", "Total Size",       root->totalSize);
    EVMU_LOG_VERBOSE("%-20s: %40u", "Partition",        root->partition);
    EVMU_LOG_VERBOSE("%-20s: %40u", "Root Block",       root->rootBlock);
    EVMU_LOG_VERBOSE("%-20s: %40u", "Fat Block",        root->fatBlock);
    EVMU_LOG_VERBOSE("%-20s: %40u", "Fat Size",         root->fatSize);
    EVMU_LOG_VERBOSE("%-20s: %40u", "Directory Block",  root->dirBlock);
    EVMU_LOG_VERBOSE("%-20s: %40u", "Directory Size",   root->dirSize);
    EVMU_LOG_VERBOSE("%-20s: %40u", "Save Area Block",  root->saveAreaBlock);
    EVMU_LOG_VERBOSE("%-20s: %40u", "Save Area Size",   root->saveAreaSize);
    EVMU_LOG_VERBOSE("%-20s: %40u", "Icon Shape",       root->iconShape);
    EVMU_LOG_VERBOSE("%-20s: %40u", "User Size",        root->userSize);
    EVMU_LOG_VERBOSE("%-20s: %40u", "Execution File",   root->executionFile);

    //Scout for new, undocumented shit!
    uint8_t* rootData = (uint8_t*)root;
    for(unsigned i = offsetof(VMUFlashRootBlock, executionFile) + sizeof(root->executionFile); i < VMU_FLASH_BLOCK_SIZE; ++i) {
        if(rootData[i] != 0) {
            EVMU_LOG_WARNING("Unknown Value: Root[%u] = %x", i, rootData[i]);
        }
    }

    EVMU_LOG_VERBOSE("Fat Entries");
    EVMU_LOG_PUSH();

    for(int b = 0; b < gyVmuFlashBlockCount(dev); ++b) {
        char lineBuff[300]  = { '\0' };
        char blockBuff[30]  = { '\0' };
        char strBuff[10]    = { '\0' };

        int i;

        for(i = 0; i < 5 && b + i < gyVmuFlashBlockCount(dev); ++i) {

            const uint16_t* block = gyVmuFlashBlockFATEntry((VMUDevice*)dev, b+i);

            switch(*block) {
            case VMU_FLASH_BLOCK_FAT_UNALLOCATED:
                strcpy(strBuff, "FREE");
                break;
            case VMU_FLASH_BLOCK_FAT_LAST_IN_FILE:
                strcpy(strBuff, "EOF");
                break;
            case VMU_FLASH_BLOCK_FAT_DAMAGED:
                strcpy(strBuff, "DMG");
                break;
            default: {
                int intVal = *block;
                sprintf(strBuff, "%u", intVal);
            }
            }

            sprintf(blockBuff, "[%03u] = % 4s ", b+i, strBuff);
            strcat(lineBuff, blockBuff);
        }
        b += i - 1;

        EVMU_LOG_VERBOSE("%s", lineBuff);

    }

    EVMU_LOG_POP(1);

    gyVmuFlashPrintFilesystem(dev);

    EVMU_LOG_POP(1);
}

int gyVmuFlashPrintFilesystem(const struct VMUDevice* dev) {
    int success = 1;
    EVMU_LOG_VERBOSE("Dumping VMU Filesystem");
    EVMU_LOG_PUSH();
    VMUFlashMemUsage memUsage = gyVmuFlashMemUsage(dev);
    int fileCount = gyVmuFlashFileCount(dev);

     EVMU_LOG_VERBOSE("%-20s: %40d", "Blocks Used",     memUsage.blocksUsed);
     EVMU_LOG_VERBOSE("%-20s: %40d", "Free",            memUsage.blocksFree);
     EVMU_LOG_VERBOSE("%-20s: %40d", "File Count",      fileCount);
    EVMU_LOG_PUSH();
    int num = 0;
    for(int d = 0; d < gyVmuFlashDirEntryCount(dev); ++d) {
        char name[VMU_VMS_FILE_INFO_DC_DESC_SIZE+1] = { '\0' };
        const VMUFlashDirEntry* dirEntry = gyVmuFlashDirEntryByIndex((VMUDevice*)dev, d);

        if(dirEntry->fileType != VMU_FLASH_FILE_TYPE_GAME && dirEntry->fileType != VMU_FLASH_FILE_TYPE_DATA) continue;
        //if(dirEntry->fileType == VMU_FLASH_FILE_TYPE_NONE) continue;
        const VMSFileInfo* vms = gyVmuFlashDirEntryVmsHeader(dev, dirEntry);
        if(!vms) {
            EVMU_LOG_ERROR("Failed to retrieve VMS header of file? [dirEntry type: %d, dirEntry size: %d, first block: %d]", dirEntry->fileType, dirEntry->fileSize, dirEntry->firstBlock);
        } else {
            gyVmuVmsHeaderDcDescGet(vms, name);
            EVMU_LOG_VERBOSE("[%d] - %s", num++, name);
            EVMU_LOG_PUSH();
            gyVmuFlashDirEntryPrint(dev, dirEntry);
            EVMU_LOG_POP(1);
        }
    }

    EVMU_LOG_POP(1);



    EVMU_LOG_POP(1);
    return success;
}


int gyVmuFlashExportImage(struct VMUDevice* dev, const char* path) {
    EvmuDevice_* pDevice_ = EVMU_DEVICE_PRISTINE(dev);
    FILE* fp = NULL;
    int success = 1;

    EVMU_LOG_VERBOSE("Exporting VMU Flash image: [%s]", path);
    EVMU_LOG_PUSH();

    fp = fopen(path, "wb");

    if(/*!retVal ||*/ !fp) {
        EVMU_LOG_ERROR("Failed to create the file!");
        success = 0;
        goto end;
    }

    size_t bytesWritten = fwrite(pDevice_->pMemory->flash, sizeof(char), EVMU_FLASH_SIZE, fp);

    if(bytesWritten < EVMU_FLASH_SIZE) {
        EVMU_LOG_ERROR("Couldn't write entire file! [bytes written %d/%d]", bytesWritten, EVMU_FLASH_SIZE);
        success = 0;
    }

    if(fclose(fp) || !success) {
        EVMU_LOG_ERROR("Could not gracefully close file.");
    }

end:
    EVMU_LOG_POP(1);
    return success;
}



int gyVmuFlashExportDcm(struct VMUDevice* dev, const char* path) {
    EvmuDevice_* pDevice_ = EVMU_DEVICE_PRISTINE(dev);
    uint8_t data[EVMU_FLASH_SIZE];
    FILE* fp = NULL;
    int success = 1;

    EVMU_LOG_VERBOSE("Exporting DCM (Nexus) Flash Image: [%s]", path);
    EVMU_LOG_PUSH();

    fp = fopen(path, "wb");;

    if(/*!retVal ||*/ !fp) {
        EVMU_LOG_ERROR("Failed to create the file!");
        success = 0;
        goto end;
    }

    memcpy(data, pDevice_->pMemory->flash, EVMU_FLASH_SIZE);
    gyVmuFlashNexusByteOrder(data, EVMU_FLASH_SIZE);

    size_t bytesWritten = fwrite(data, sizeof(char), EVMU_FLASH_SIZE, fp);

    if(bytesWritten < EVMU_FLASH_SIZE) {
        EVMU_LOG_ERROR("Couldn't write entire file! [bytes written %d/%d]", bytesWritten, EVMU_FLASH_SIZE);
        success = 0;
    }

    if(fclose(fp) || !success) {
        EVMU_LOG_ERROR("Could not gracefully close file.");
    }

end:
    EVMU_LOG_POP(1);
    return success;
}




VMUFlashDirEntry* gyVmuFlashDirEntryIconData(struct VMUDevice* dev) {
    return gyVmuFlashDirEntryFind(dev, VMU_ICONDATA_VMS_FILE_NAME);
}

VMUFlashDirEntry* gyVmuFlashDirEntryExtraBgPvr(struct VMUDevice* dev) {
    return gyVmuFlashDirEntryFind(dev, GYRO_VMU_EXTRA_BG_PVR_FILE_NAME);
}


VMUFlashDirEntry* gyVmuFlashLoadIconDataVms(struct VMUDevice* dev, const char* path, VMU_LOAD_IMAGE_STATUS* status) {
    VMUFlashDirEntry*   entry       = NULL;
    uint8_t*            dataBuffer  = NULL;
    size_t              bytesRead;

    EVMU_LOG_VERBOSE("Loading ICONDATA_VMS File [%s].", path);
    EVMU_LOG_PUSH();

   const VMUFlashDirEntry* dirEntry = gyVmuFlashDirEntryIconData(dev);

    if(dirEntry) {
        snprintf(_lastErrorMsg,
                 sizeof(_lastErrorMsg),
                 "An ICONDATA.VMS file is already present in flash. [File Index: %d, First Block: %d, Size: %d]",
                 gyVmuFlashDirEntryIndex(dev, dirEntry),
                 dirEntry->firstBlock,
                 dirEntry->fileSize);
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        *status = VMU_LOAD_IMAGE_NAME_DUPLICATE;
        goto done;
    }

    FILE* file = fopen(path, "rb");

    if(!file) {
        snprintf(_lastErrorMsg,
                 sizeof(_lastErrorMsg),
                 "Could not open binary file for reading!");
        EVMU_LOG_ERROR("%s", _lastErrorMsg);

    } else {
        size_t fileSize = 0;
        fseek(file, 0, SEEK_END); // seek to end of file
        fileSize = ftell(file); // get current file pointer
        fseek(file, 0, SEEK_SET); // seek back to beginning of file
        fclose(file);
        if(/*!retVal ||*/ !fileSize) {
            snprintf(_lastErrorMsg,
                     sizeof(_lastErrorMsg),
                     "Could not determine file size! [0 bytes]");
            EVMU_LOG_ERROR("%s", _lastErrorMsg);
            *status = VMU_LOAD_IMAGE_READ_FAILED;
            goto done;
        }

        dataBuffer = malloc(sizeof(uint8_t)*fileSize);

        bytesRead = fread(dataBuffer, 1, fileSize, file);
        fclose(file);

       if(!bytesRead) {
           snprintf(_lastErrorMsg,
                    sizeof(_lastErrorMsg),
                    "Could not read from file!");
            EVMU_LOG_ERROR("%s", _lastErrorMsg);
            *status = VMU_LOAD_IMAGE_READ_FAILED;
        } else {
            if(bytesRead != fileSize) {
                EVMU_LOG_WARNING("Could not actually read entirety of file, but continuing anyway [%d/%d].", bytesRead, fileSize);
                *status = VMU_LOAD_IMAGE_READ_FAILED;
            }

            gyVmuIconDataPrint((const IconDataFileInfo*)dataBuffer);

            VMUFlashNewFileProperties properties;
            gyVmuFlashNewFilePropertiesFromIconDataVms(&properties, bytesRead);
            entry = gyVmuFlashFileCreate(dev, &properties, dataBuffer, status);
        }
    }

    free(dataBuffer);
done:
    EVMU_LOG_POP(1);
    return entry;

}


void gyVmuFlashNewFilePropertiesFromIconDataVms(VMUFlashNewFileProperties* fileProperties, size_t byteSize) {
    assert(fileProperties && byteSize);

    memcpy(fileProperties->fileName, VMU_ICONDATA_VMS_FILE_NAME, sizeof(VMU_ICONDATA_VMS_FILE_NAME));
    fileProperties->fileSizeBytes   = byteSize;
    fileProperties->fileType        = VMU_FLASH_FILE_TYPE_DATA;
    fileProperties->copyProtection  = 0;
}


int gyVmuFlashExportVms(const struct VMUDevice* dev, const struct VMUFlashDirEntry* entry, const char* path) {
    int success = 1;
    assert(dev && entry && path);

    char entryName[VMU_FLASH_DIRECTORY_FILE_NAME_SIZE+1] = { '\0' };
    size_t fileSize = entry->fileSize * VMU_FLASH_BLOCK_SIZE;

    memcpy(entryName, entry->fileName, VMU_FLASH_DIRECTORY_FILE_NAME_SIZE);

    EVMU_LOG_VERBOSE("Exporting file [%s] to VMS file: [%s]", entryName, path);
    EVMU_LOG_PUSH();


    VMSFileInfo* vmsImg = malloc(sizeof(uint8_t) * fileSize);

    if(!gyVmuFlashFileRead(dev, entry, vmsImg, 1)) {
        strncpy(_lastErrorMsg, "Failed to retrieve the file from flash!", sizeof(_lastErrorMsg));
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        success = 0;
        goto free_img;
    }

    FILE* fp = fopen(path, "wb");
    if(/*!retVal ||*/ !fp) {
        strncpy(_lastErrorMsg, "Failed to create the file!", sizeof(_lastErrorMsg));
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        success = 0;
        goto free_img;
    }

    if(!fwrite(vmsImg, fileSize, 1, fp)) {
        strncpy(_lastErrorMsg, "Failed to write all bytes to the file.", sizeof(_lastErrorMsg));
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        success = 0;
    }

clean_file:
    fclose(fp);

free_img:
    free(vmsImg);
done:
    EVMU_LOG_POP(1);
    return success;

}


int gyVmuFlashExportRaw(const struct VMUDevice* dev, const struct VMUFlashDirEntry* entry, const char* path) {
    int success = 1;
    assert(dev && entry && path);

    char entryName[VMU_FLASH_DIRECTORY_FILE_NAME_SIZE+1] = { '\0' };
    size_t fileSize = entry->fileSize * VMU_FLASH_BLOCK_SIZE;

    memcpy(entryName, entry->fileName, VMU_FLASH_DIRECTORY_FILE_NAME_SIZE);

    EVMU_LOG_VERBOSE("Exporting file [%s] to Raw Binary file: [%s]", entryName, path);
    EVMU_LOG_PUSH();

    assert(entry->fileType == VMU_FLASH_FILE_TYPE_DATA);

    VMSFileInfo* vmsImg = malloc(sizeof(uint8_t) * fileSize);

    if(!gyVmuFlashFileRead(dev, entry, vmsImg, 1)) {
        strncpy(_lastErrorMsg, "Failed to retrieve the file from flash!", sizeof(_lastErrorMsg));
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        success = 0;
        goto free_img;
    }

    FILE* fp = fopen(path, "wb");
    if(/*!retVal ||*/ !fp) {
        strncpy(_lastErrorMsg, "Failed to create the file!", sizeof(_lastErrorMsg));
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        success = 0;
        goto free_img;
    }

    size_t headerSize   = gyVmuVmsFileInfoHeaderSize(vmsImg);
    size_t bytesToWrite = fileSize - headerSize;


    if(!fwrite(((uint8_t*)vmsImg + headerSize), bytesToWrite, 1, fp)) {
        strncpy(_lastErrorMsg, "Failed to write all bytes to the file.", sizeof(_lastErrorMsg));
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        success = 0;
    }

clean_file:
    fclose(fp);

free_img:
    free(vmsImg);
done:
    EVMU_LOG_POP(1);
    return success;

}


int gyVmuFlashExportVmi(const struct VMUDevice* dev, const struct VMUFlashDirEntry* entry, const char* path) {
    int success = 1;
    assert(dev && entry && path);

    char tempFilePath[GYRO_PATH_MAX_SIZE] = { '\0' };
    char entryName[VMU_FLASH_DIRECTORY_FILE_NAME_SIZE+1] = { '\0' };
    size_t fileSize = sizeof(VMIFileInfo);
    memcpy(entryName, entry->fileName, VMU_FLASH_DIRECTORY_FILE_NAME_SIZE);

    EVMU_LOG_VERBOSE("Exporting file [%s] to VMI file: [%s]", entryName, path);
    EVMU_LOG_PUSH();

    strncpy(tempFilePath, path, GYRO_PATH_MAX_SIZE);
    const char* curTok  = strtok(tempFilePath, "/\\");
    const char* prevTok  = NULL;

    while(curTok) {
        prevTok = curTok;
        curTok = strtok(NULL, "/\\");
    }
    assert(prevTok);
    prevTok = strtok(prevTok, ".");
    assert(prevTok);

    EVMU_LOG_VERBOSE("Extracted resource name: %s", prevTok);

    VMIFileInfo vmi;
    gyVmuFlashVmiFromDirEntry(&vmi, dev, entry, prevTok);

    FILE* fp = fopen(path, "wb");
    if(/*!retVal || */!fp) {
        strncpy(_lastErrorMsg, "Failed to create the file!", sizeof(_lastErrorMsg));
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        success = 0;
        goto done;
    }

    if(!fwrite(&vmi, fileSize, 1, fp)) {
        strncpy(_lastErrorMsg, "Failed to write all bytes to the file.", sizeof(_lastErrorMsg));
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        success = 0;
    }


clean_file:
    fclose(fp);
done:
    EVMU_LOG_POP(1);
    return success;

}

int gyVmuFlashExportDci(const struct VMUDevice* dev, const struct VMUFlashDirEntry* entry, const char* path) {
    int success = 1;
    assert(dev && entry && path);

    char entryName[VMU_FLASH_DIRECTORY_FILE_NAME_SIZE+1] = { '\0' };
    size_t paddedFileSize = entry->fileSize * VMU_FLASH_BLOCK_SIZE;
    size_t bytesWritten = 0;

    memcpy(entryName, entry->fileName, VMU_FLASH_DIRECTORY_FILE_NAME_SIZE);

    EVMU_LOG_VERBOSE("Exporting file [%s] to DCI file: [%s]", entryName, path);
    EVMU_LOG_PUSH();

    size_t          dataSize   = sizeof(uint8_t) * paddedFileSize + sizeof(VMUFlashDirEntry);
    uint8_t*        data       = malloc(dataSize + dataSize%4? 4-dataSize%4 : 0);
    uint8_t*        vms        = data + sizeof(VMUFlashDirEntry);
    VMSFileInfo*    vmsHeader  = (VMSFileInfo*)(vms + entry->headerOffset * VMU_FLASH_BLOCK_SIZE);

    memset(data, 0, dataSize);
    memcpy(data, entry, sizeof(VMUFlashDirEntry));

    if(!gyVmuFlashFileRead(dev, entry, vms, 1)) {
        strncpy(_lastErrorMsg, "Failed to retrieve the file from flash!", sizeof(_lastErrorMsg));
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        success = 0;
        goto free_img;
    }

    size_t bytesToWrite   = dataSize;// + sizeof(VMUFlashDirEntry);

    FILE* fp = fopen(path, "wb");
    if(/*!retVal ||*/ !fp) {
        strncpy(_lastErrorMsg, "Failed to create the file!", sizeof(_lastErrorMsg));
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        success = 0;
        goto free_img;
    }

    gyVmuFlashNexusByteOrder(vms, dataSize);

    if(!fwrite(data, 1, bytesToWrite, fp)) {
        strncpy(_lastErrorMsg, "Failed to write all bytes to the file.", sizeof(_lastErrorMsg));
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        success = 0;
    }


clean_file:
    fclose(fp);

free_img:
    free(data);
done:
    EVMU_LOG_POP(1);
    return success;

}


uint16_t gyVmuFlashFileCalculateCRC(struct VMUDevice* dev, const VMUFlashDirEntry* dirEntry) {
    assert(dev && dirEntry);

    if(dirEntry->fileType == VMU_FLASH_FILE_TYPE_GAME) return 0; //Doesn't even fucking use the CRC!!

    uint16_t crc = 0;
    VMSFileInfo* vms = gyVmuFlashDirEntryVmsHeader(dev, dirEntry);
    assert(vms);


    size_t bytesLeft = dirEntry->fileType == VMU_FLASH_FILE_TYPE_DATA?
                gyVmuVmsFileInfoHeaderSize(vms) + vms->dataBytes :
                dirEntry->fileSize * VMU_FLASH_BLOCK_SIZE;

    //have to set this equal to 0 to get the right CRC!
    uint16_t prevCrc = vms->crc;
    vms->crc = 0;

    for(uint16_t block = dirEntry->firstBlock;
        block != VMU_FLASH_BLOCK_FAT_LAST_IN_FILE;
        block = gyVmuFlashBlockNext(dev, block))
    {
        size_t bytes = (bytesLeft > VMU_FLASH_BLOCK_SIZE)?
                    VMU_FLASH_BLOCK_SIZE :
                    bytesLeft;

        uint8_t* data = gyVmuFlashBlock(dev, block);
        assert(data);
        if(!data) {
            return 0;
        }

        crc = gyVmuVMSFileInfoCrcCalc(data, bytes, &crc);

        bytesLeft -= bytes;

    }

    //assert(!bytesLeft);

    vms->crc = prevCrc;

    return crc;

}


VMUFlashDirEntry* gyVmuFlashLoadArmBinary(struct VMUDevice* dev, const char* path, VMU_LOAD_IMAGE_STATUS* status) {
    VMUFlashDirEntry*   entry       = NULL;
    uint8_t*            dataBuffer  = NULL;
    size_t              bytesRead;

    EVMU_LOG_VERBOSE("Loading ARM Binary File [%s].", path);
    EVMU_LOG_PUSH();
#if 0
   const VMUFlashDirEntry* dirEntry = gyVmuFlashDirEntryIconData(dev);

    if(dirEntry) {
        snprintf(_lastErrorMsg,
                 sizeof(_lastErrorMsg),
                 "An ICONDATA.VMS file is already present in flash. [File Index: %d, First Block: %d, Size: %d]",
                 gyVmuFlashDirEntryIndex(dev, dirEntry),
                 dirEntry->firstBlock,
                 dirEntry->fileSize);
        EVMU_LOG_ERROR("%s", _lastErrorMsg);
        *status = VMU_LOAD_IMAGE_NAME_DUPLICATE;
        goto done;
    }
#endif
    FILE* file = fopen(path, "rb");

    if(!file) {
        snprintf(_lastErrorMsg,
                 sizeof(_lastErrorMsg),
                 "Could not open binary file for reading!");
        EVMU_LOG_ERROR("%s", _lastErrorMsg);

    } else {
        size_t fileSize = 0;
        fseek(file, 0, SEEK_END); // seek to end of file
        fileSize = ftell(file); // get current file pointer
        fseek(file, 0, SEEK_SET); // seek back to beginning of file
        if(/*!retVal ||*/ !fileSize) {
            snprintf(_lastErrorMsg,
                     sizeof(_lastErrorMsg),
                     "Could not determine file size! [0 bytes]");
            EVMU_LOG_ERROR("%s", _lastErrorMsg);
            *status = VMU_LOAD_IMAGE_READ_FAILED;
            goto done;
        }

        dataBuffer = calloc(sizeof(uint8_t), fileSize);
/*
        VMSFileInfo* pVms = (VMSFileInfo*)dataBuffer;
        pVms->iconCount = 1;
        pVms->eyecatchType = 0;
        pVms->dataBytes = fileSize;
        pVms->animSpeed = 0;
        strcpy(pVms->vmuDesc, "lolz.       M23");
        strcpy(pVms->dcDesc, "lolz.M23");
        strcpy(pVms->creatorApp, "lolz.M23");
*/
        bytesRead = fread(dataBuffer, 1, fileSize, file);
        fclose(file);

       if(!bytesRead) {
           snprintf(_lastErrorMsg,
                    sizeof(_lastErrorMsg),
                    "Could not read from file!");
            EVMU_LOG_ERROR("%s", _lastErrorMsg);
            *status = VMU_LOAD_IMAGE_READ_FAILED;
        } else {
            if(bytesRead != fileSize) {
                EVMU_LOG_WARNING("Could not actually read entirety of file, but continuing anyway [%d/%d].", bytesRead, fileSize);
                *status = VMU_LOAD_IMAGE_READ_FAILED;
            }

            VMUFlashNewFileProperties properties;
            strcpy(properties.fileName, "fucking.M23");
            properties.fileType = VMU_FLASH_FILE_TYPE_DATA;
            properties.fileSizeBytes = fileSize;
            properties.copyProtection = VMU_FLASH_COPY_PROTECTION_COPY_OK;
            entry = gyVmuFlashFileCreate(dev, &properties, dataBuffer, status);
            if(entry) {
              //  pVms = gyVmuFlashDirEntryVmsHeader(dev, entry);
               // pVms->crc = gyVmuFlashFileCalculateCRC(dev, entry);

            }
        }
    }

    free(dataBuffer);
done:
    EVMU_LOG_POP(1);
    return entry;


}
