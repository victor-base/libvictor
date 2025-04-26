#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "store.h"
#include "victor.h"
#include "index_nsw.h"  /* Para SIHdrNSW */

extern int magic_to_index(uint32_t magic);

int main(int argc, char *argv[]) {
    printf("%s\n", __LIB_VERSION());
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *filename = argv[1];
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Failed to open file");
        return EXIT_FAILURE;
    }

    StoreHDR hdr;
    if (fread(&hdr, sizeof(StoreHDR), 1, fp) != 1) {
        perror("Failed to read StoreHDR");
        fclose(fp);
        return EXIT_FAILURE;
    }

    printf("Header Information:\n");
    printf("  magic:          0x%08X\n", hdr.magic);
    printf("  version:        %u.%u.%u\n", hdr.major, hdr.minor, hdr.patch);
    printf("  hsize:          %u bytes\n", hdr.hsize);
    printf("  elements:       %u\n", hdr.elements);
    printf("  method:         %u\n", hdr.method);
    printf("  dims:           %u\n", hdr.dims);
    printf("  dims_aligned:   %u\n", hdr.dims_aligned);
    printf("  vsize:          %u bytes\n", hdr.vsize);
    printf("  nsize:          %u bytes\n", hdr.nsize);
    printf("  voff:           %lu\n", (unsigned long)hdr.voff);
    printf("  noff:           %lu\n", (unsigned long)hdr.noff);

    int index_type = magic_to_index(hdr.magic);
    if (index_type == -1) {
        fprintf(stderr, "Unknown index type\n");
        fclose(fp);
        return EXIT_FAILURE;
    }



    /* Movernos hasta donde empieza la cabecera del índice */
    if (fseek(fp, sizeof(StoreHDR), SEEK_SET) != 0) {
        perror("Failed to seek to index header");
        fclose(fp);
        return EXIT_FAILURE;
    }

    if (index_type == NSW_INDEX) {
        SIHdrNSW nsw_hdr;

        printf("\nIndex Specific Header (NSW):\n");
        if (fread(&nsw_hdr, sizeof(SIHdrNSW), 1, fp) != 1) {
            perror("Failed to read SIHdrNSW");
            fclose(fp);
            return EXIT_FAILURE;
        }

        printf("  ef_search:      %u\n", nsw_hdr.ef_search);
        printf("  ef_construct:   %u\n", nsw_hdr.ef_construct);
        printf("  odegree_hl:     %u\n", nsw_hdr.odegree_hl);
        printf("  odegree_sl:     %u\n", nsw_hdr.odegree_sl);
        printf("  odegree_computed: %u\n", nsw_hdr.odegree_computed);
        printf("  entry:          %lu\n", (unsigned long)nsw_hdr.entry);
    } else {
        printf("  (No detailed info available for this index type)\n");
    }

    fclose(fp);
    return EXIT_SUCCESS;
}
