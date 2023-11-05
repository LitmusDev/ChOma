#include <stdbool.h>
#include <assert.h>

#include "MachOContainer.h"
#include "MachOByteOrder.h"

#include "FileStream.h"

int macho_container_read_at_offset(MachOContainer *machoContainer, uint64_t offset, size_t size, void *outBuf)
{
    return memory_stream_read(&machoContainer->stream, offset, size, outBuf);
}

int macho_container_parse_machos(MachOContainer *machoContainer)
{
    // Read the FAT header
    struct fat_header fatHeader;
    macho_container_read_at_offset(machoContainer, 0, sizeof(fatHeader), &fatHeader);
    FAT_HEADER_APPLY_BYTE_ORDER(&fatHeader, BIG_TO_HOST_APPLIER);

    // Check if the file is a FAT file
    if (fatHeader.magic == FAT_MAGIC || fatHeader.magic == FAT_MAGIC_64)
    {
        printf("FAT header found! Magic: 0x%x.\n", fatHeader.magic);
        bool is64 = fatHeader.magic == FAT_MAGIC_64;

        // Sanity check the number of slices
        if (fatHeader.nfat_arch > 5 || fatHeader.nfat_arch < 1) {
            printf("Error: invalid number of slices (%d), this likely means you are not using an iOS MachOContainer.\n", fatHeader.nfat_arch);
            return -1;
        }

        MachO *allSlices = malloc(sizeof(MachO) * fatHeader.nfat_arch);
        memset(allSlices, 0, sizeof(MachO) * fatHeader.nfat_arch);

        // Iterate over all slices
        for (uint32_t i = 0; i < fatHeader.nfat_arch; i++)
        {
            struct fat_arch_64 arch64 = {0};
            if (is64)
            {
                // Read the arch descriptor
                macho_container_read_at_offset(machoContainer, sizeof(struct fat_header) + i * sizeof(arch64), sizeof(arch64), &arch64);
                FAT_ARCH_64_APPLY_BYTE_ORDER(&arch64, BIG_TO_HOST_APPLIER);
            }
            else
            {
                // Read the FAT arch structure
                struct fat_arch arch = {0};
                macho_container_read_at_offset(machoContainer, sizeof(struct fat_header) + i * sizeof(arch), sizeof(arch), &arch);
                FAT_ARCH_APPLY_BYTE_ORDER(&arch, BIG_TO_HOST_APPLIER);

                // Convert fat_arch to fat_arch_64
                arch64 = (struct fat_arch_64){
                    .cputype = arch.cputype,
                    .cpusubtype = arch.cpusubtype,
                    .offset = (uint64_t)arch.offset,
                    .size = (uint64_t)arch.size,
                    .align = arch.align,
                    .reserved = 0,
                };
            }

            int sliceInitRet = macho_init_from_fat_arch(&allSlices[i], machoContainer, arch64);
            if (sliceInitRet != 0) return sliceInitRet;
        }

        // Add the new slices to the MachOContainer structure
        machoContainer->sliceCount = fatHeader.nfat_arch;
        printf("Found %zu slices.\n", machoContainer->sliceCount);
        machoContainer->slices = allSlices;

    } else {
        // Not FAT? Try parsing it as a single slice MachO
        MachO slice;
        int sliceInitRet = macho_init_from_macho(&slice, machoContainer);
        if (sliceInitRet != 0) return sliceInitRet;

        machoContainer->slices = malloc(sizeof(MachO));
        memset(machoContainer->slices, 0, sizeof(MachO));
        machoContainer->slices[0] = slice;
        machoContainer->sliceCount = 1;
    }
    return 0;
}

void macho_container_free(MachOContainer *machoContainer)
{
    memory_stream_free(&machoContainer->stream);
    if (machoContainer->slices != NULL) {
        for (int i = 0; i < machoContainer->sliceCount; i++) {
            macho_free(&machoContainer->slices[i]);
        }
        free(machoContainer->slices);
    }
}

int macho_container_init_from_memory_stream(MachOContainer *machoContainer, MemoryStream *stream)
{
    machoContainer->stream = *stream;

    if (macho_container_parse_machos(machoContainer) != 0) {
        return -1;
    }

    size_t size = 0;
    if (file_stream_get_size(&machoContainer->stream, &size) != 0) {
        return -1;
    }

    printf("File size 0x%zx bytes, slice count %zu.\n", size, machoContainer->sliceCount);
    return 0;
}

int macho_container_init_from_path(MachOContainer *machoContainer, const char *filePath)
{
    memset(machoContainer, 0, sizeof(*machoContainer));

    MemoryStream stream;
    if (file_stream_init_from_path(&stream, filePath, 0, FILE_STREAM_SIZE_AUTO) != 0) return -1;

    return macho_container_init_from_memory_stream(machoContainer, &stream);
}