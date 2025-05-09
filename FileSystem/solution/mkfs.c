#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "wfs.h"

int main(int argc, char *argv[]) {

    int raid_mode = -1;      // 0-0, 1-1, 2-1v
    int num_inodes = -1;
    int data_blocks = -1;
    char *disks[MAX_DISKS];
    int ind = 0;

    for (int i = 1; i < argc; i+=2) {

        char *arg = argv[i];

        if (arg[0] == '-') {


            if (i == (argc - 1)) {
                printf("Invalid arguments\n");
                exit(1);
            }

            char *arg2 = argv[i+1];

            if (arg2[0] == '-') {
                printf("Invalid arguments\n");
                exit(1);
            }

            switch(arg[1]) {
                case 'r':
                    if (strcmp(arg2, "0") == 0) raid_mode = 0;
                    else if (strcmp(arg2, "1") == 0) raid_mode = 1;
                    else if (strcmp(arg2, "1v") == 0) raid_mode = 2;
                    else {
                        printf("Invalid raid\n");
                        exit(1);
                    }
                    break;
                case 'i':
                    num_inodes = atoi(arg2);
                    if (num_inodes <= 0) {
                        printf("Invalid inode count\n");
                        exit(1);
                    }
                    int offset_i = (num_inodes % 32);
                    num_inodes += (offset_i == 0) ? 0 : 32 - (offset_i);
                    break;
                case 'b':
                    data_blocks = atoi(arg2);
                    int offset_d = (data_blocks % 32);
                    data_blocks += (offset_d == 0) ? 0 : 32 - (offset_d);
                    break;
                case 'd':
                    disks[ind] = arg2;
                    ind++;
                    break;
                default:
                    printf("Invalid argument\n");
                    exit(1);
            }

            
        } else {
            printf("Invalid arguments\n");
            exit(1);
        }
    
    }
    
    if (ind < 2) exit(1);



    off_t inode_bm = sizeof(struct wfs_sb);
    int inode_bytes = (num_inodes / 8) + (((num_inodes % 8) == 0) ? 0 : 1);
    off_t data_bm = inode_bm + inode_bytes;
    off_t bm_end = data_bm + (data_blocks / 8);
    off_t inodes = bm_end + ((bm_end % BLOCK_SIZE == 0) ? 0 : BLOCK_SIZE - (bm_end % BLOCK_SIZE));
    off_t data = inodes + (BLOCK_SIZE * num_inodes);

    struct wfs_sb sblock = {
        .num_inodes = num_inodes,
        .num_data_blocks = data_blocks,
        .i_bitmap_ptr = inode_bm,
        .d_bitmap_ptr = data_bm,
        .i_blocks_ptr = inodes,
        .d_blocks_ptr = data,
        .raid_mode = raid_mode,
        .disk_id = -1,
        .num_disks = ind
    };

    time_t curr_time = time(NULL);

    struct wfs_inode root = {
        .num = 0,                   /* Inode number */
        .mode = S_IFDIR,     /* File type and mode */
        .uid = getuid(),                   /* User ID of owner */
        .gid = getgid(),                   /* Group ID of owner */
        .size = 0,                  /* Total size, in bytes */
        .nlinks = 1,                /* Number of links */

        .atim = curr_time,          /* Time of last access */
        .mtim = curr_time,          /* Time of last modification */
        .ctim = curr_time,          /* Time of last status change */

        .blocks = {0}
    };


    for (int i = 0; i < ind; i++) {

        int fd = open(disks[i], O_RDWR);
        if (fd < 0) {
            printf("Failed to open disk: %s\n", disks[i]);
            exit(1);
        }

        struct stat st;
        fstat(fd, &st);
        int size = st.st_size;

        if (data + (BLOCK_SIZE * data_blocks) >= size) {
            printf("not big enough\n");
            exit(-1);
        }

        char *sb = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); 

        memset(sb, 0, size);

        memcpy(sb, &sblock, sizeof(struct wfs_sb));
        memcpy(sb + inodes, &root, sizeof(struct wfs_inode));

        memset( sb + inode_bm, 0, (int) (bm_end - inode_bm)); // initialize both bitmaps to zeros

        struct wfs_sb *ptr = (struct wfs_sb *) sb;
        ptr->disk_id = i;

        *(sb + inode_bm) |= (1 << 0);

        munmap(sb, size);
        close(fd);
    }

    return 0;
}