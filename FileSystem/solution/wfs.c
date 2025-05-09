#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <libgen.h>
#include <limits.h>

#include "wfs.h"

int raid_mode;
int num_disks;
struct wfs_disk disks[MAX_DISKS];
off_t dblock_offset = 0;


static struct wfs_inode* num2inode(int num, int disk_ind) {
    struct wfs_disk *disk = &disks[disk_ind];
    return (struct wfs_inode *) (disk->addr + disk->sb->i_blocks_ptr + (num * BLOCK_SIZE));
}

static off_t disk_offset(off_t block_offset) {
    if (raid_mode != 0) {   // if raid is not 0 all disks are the same
        return block_offset;
    } else {                // raid 0 so we need to compute the offset
        return ((block_offset / BLOCK_SIZE) / num_disks) * BLOCK_SIZE;  // offset within disk
    }
}
static int disk_index(off_t block_offset) {
    if (raid_mode != 0) {   // if raid is not 0 all disks are the same
        return 0;
    } else {                // raid 0 so we need to compute the index
        return (block_offset / BLOCK_SIZE) % num_disks;
    }
}

// // maybe update to pass a inode ptr and memcpy it to each sb?
static struct wfs_inode* allocate_inode(struct wfs_inode *new_inode) {

    char *sb = disks[0].addr;
    struct wfs_sb *sblock = disks[0].sb;

    int num_inodes = sblock->num_inodes;

    int inode_bytes = (num_inodes / CHAR_BIT) + (((num_inodes % CHAR_BIT) == 0) ? 0 : 1);
    off_t inode_offset = -1;

    int byte;
    int bit;

    char mask = 0;

    for (byte = 0; byte < inode_bytes; byte++) {

        char bm_byte = *(sb + sblock->i_bitmap_ptr + byte);

        for (bit = 0; bit < CHAR_BIT; bit++) {   

            mask = (1 << bit); // 0000 0001

            if ((mask & ~bm_byte) != 0) {     // found open inode
                inode_offset = (BLOCK_SIZE * bit) + (CHAR_BIT * BLOCK_SIZE * byte);
                goto found;
            }
        }
    }

    return 0; // did not find a open inode

    found:

    new_inode->num = inode_offset / BLOCK_SIZE;
    // initialize all blocks to -1
    for (int i = 0; i < N_BLOCKS; i++) {
        new_inode->blocks[i] = -1;
    }


    // update inode bm of all disks and copy the new inode to all disks
    for (int i = 0; i < num_disks; i++) {
        struct wfs_disk *disk = &disks[i];
        *(disk->addr + disk->sb->i_bitmap_ptr + byte) |= mask;  // set bitmap
        memcpy(disk->addr + disk->sb->i_blocks_ptr + inode_offset, new_inode, sizeof(struct wfs_inode));   // copy inode
    }



    return num2inode(new_inode->num, 0);
}

static struct wfs_inode* get_inode(const char *path) {
    
    printf("getting inode with path %s\n", path);

    struct wfs_disk *disk = &disks[0];

    if (strcmp(path, "/") == 0) {
        return (struct wfs_inode *) (disk->addr + disk->sb->i_blocks_ptr);   // ROOT INODE
    }

    int path_length = strlen(path);
    char temp_path[path_length + 1];
    strcpy(temp_path, path);

    char *token = strtok(temp_path, "/");
    struct wfs_inode *curr_dir = (struct wfs_inode *) (disk->addr + disk->sb->i_blocks_ptr); // start at the root inode

    if (curr_dir->size == 0) {
        printf("Root is empty\n");
        return 0;
    }

    int num_data_blocks;
    int entries_per_block = BLOCK_SIZE / sizeof(struct wfs_dentry);

    off_t block_offset;
    struct wfs_dentry *entries;

    while (token != NULL) {

        if (S_ISDIR(curr_dir->mode) == 0) {
            printf("%s not a directory\n", token);
            return 0;
        }

        //printf("Searching inode num: %d for %s\n", curr_dir->num, token);

        num_data_blocks = curr_dir->size / BLOCK_SIZE;

        for (int b = 0; b < num_data_blocks; b++) { // for each allocated block in directory

            block_offset = curr_dir->blocks[b];
            disk = &disks[disk_index(block_offset)];     // disk this block is currently on
            entries = (struct wfs_dentry *) (disk->addr + disk->sb->d_blocks_ptr + disk_offset(block_offset));

            for (int e = 0; e < entries_per_block; e++) {   // for each entry in block

                struct wfs_dentry *entry = &entries[e]; // current entry
                if (entry->name[0] == '\0') continue; // check next block
                
                //printf("Checking entry: %s\n", entry->name);
                if (strcmp(token, entry->name) == 0) {
                    printf("Found %s\n", entry->name);
                    curr_dir = num2inode(entry->num, 0); // get the next directory to search
                    goto found;
                }
            }
        }

        printf("Did not find %s\n", token);
        return 0;

        found:

        token = strtok(NULL, "/"); // get next token
        if (token == NULL) {
            printf("Done searching\n");
        }
    }

    return curr_dir;

}


// returns off set to the next block -> still have to determine disk and add size to inode
static off_t allocate_block(int inode_num) {

    int block_num = -1;
    struct wfs_sb *sblock_0 = disks[0].sb;

    struct wfs_disk *disk;
    
    if (raid_mode == 0) {

        for (int i = 0; i < sblock_0->num_data_blocks * num_disks; i++) {

            disk = &disks[disk_index(i * BLOCK_SIZE)];
            int bit = (i / num_disks) % CHAR_BIT;
            int byte = (i / num_disks) / CHAR_BIT;
            int mask = (1 << bit);

            char *bm = (disk->addr + disk->sb->d_bitmap_ptr);

            if ((bm[byte] & mask) == 0) {
                block_num = i;
                bm[byte] |= mask;
                break;
            }

        }


        if (block_num == -1) {
            printf("All blocks already allocated\n");
            return -1;
        }
        
        memset(disk->addr + disk->sb->d_blocks_ptr + disk_offset(block_num * BLOCK_SIZE), 0, BLOCK_SIZE);

    } else {

        for (int i = 0; i < sblock_0->num_data_blocks; i++) {

            disk = &disks[0];

            int bit = i % CHAR_BIT;
            int byte = i / CHAR_BIT;
            int mask = (1 << bit);

            char *bm = (disk->addr + disk->sb->d_bitmap_ptr);

            if ((bm[byte] & mask) == 0) {
                block_num = i;

                for (int d = 0; d < num_disks; d++) {
                    disk = &disks[d];

                    bm = (disk->addr + disk->sb->d_bitmap_ptr);
                    bm[byte] |= mask;

                    // set block to 0's
                    memset(disks->addr + disks->sb->d_blocks_ptr + (i * BLOCK_SIZE), 0, BLOCK_SIZE);
                }

                break;
            }

        }


        if (block_num == -1) {
            printf("All blocks already allocated\n");
            return -1;
        }

    }

    
    // update the size on all disks and the blocks array
    for (int i = 0; i < num_disks; i++) {
        struct wfs_disk *disk = &disks[i];
            
        struct wfs_inode *update = (struct wfs_inode *) (disk->addr + disk->sb->i_blocks_ptr + (inode_num * BLOCK_SIZE));

        int block_ind = (update->size < BLOCK_SIZE) ? 0 : ((update->size - 1) / BLOCK_SIZE) + 1;
  

        if (block_ind == IND_BLOCK && update->blocks[IND_BLOCK] == -1) {
            update->blocks[IND_BLOCK] = block_num * BLOCK_SIZE;
        } else if (block_ind < IND_BLOCK) {
            update->blocks[block_ind] = block_num * BLOCK_SIZE;
            update->size += BLOCK_SIZE;
        } else {
            update->size += BLOCK_SIZE;
        }


    }

    return block_num * BLOCK_SIZE;      // always return block offset as if was 1 large disk
}

static void free_block(off_t block_offset, struct wfs_inode *inode) {

    int block_num = block_offset / BLOCK_SIZE;
    struct wfs_disk *disk;

    if (raid_mode == 0) {

        disk = &disks[disk_index(block_offset)];    
        int bit = (block_num / num_disks) % CHAR_BIT;
        int byte = (block_num / num_disks) / CHAR_BIT;
        int mask = (1 << bit);

        char *bm = (disk->addr + disk->sb->d_bitmap_ptr);

        bm[byte] &= ~mask;

        memset(disk->addr + disk->sb->d_blocks_ptr + disk_offset(block_offset), 0, BLOCK_SIZE);

        // decrement the size of the inode unless it is the indirect block
        if (inode->blocks[IND_BLOCK] != block_offset) {
            struct wfs_inode *update;
            for (int i = 0; i < num_disks; i++) {
                update = num2inode(inode->num, i);
                update->size -= BLOCK_SIZE;
            }
        }


    } else {

            
            int bit = block_num % CHAR_BIT;
            int byte = block_num / CHAR_BIT;
            int mask = (1 << bit);

            char *bm;

            for (int i = 0; i < num_disks; i++) {
                disk = &disks[i];

                bm = (disk->addr + disk->sb->d_bitmap_ptr);
                bm[byte] &= ~mask;

                memset(disk->addr + disk->sb->d_blocks_ptr + block_offset, 0, BLOCK_SIZE);

                if (inode->blocks[IND_BLOCK] != block_offset) {
                    struct wfs_inode *update;
                    update = num2inode(inode->num, i);
                    update->size -= BLOCK_SIZE;
                }
            }

    }

}

static void free_inode(struct wfs_inode *inode) {

    int inode_num = inode->num;

    int byte = inode_num / CHAR_BIT;
    int bit = inode_num % CHAR_BIT;
    int mask = (1 << bit);

    struct wfs_disk *disk;
    char *bm;
    struct wfs_inode *update;

    for (int i = 0; i < num_disks; i++) {
        disk = &disks[i];
        bm = (disk->addr + disk->sb->i_bitmap_ptr);
        bm[byte] &= ~mask;

        update = num2inode(inode_num, i);
        memset(update, 0, sizeof(struct wfs_inode));
    }

}

static struct wfs_dentry* alloc_dentry(const char *path, int inum) {

    int path_length = strlen(path);
    char dir[path_length + 1];
    char file[path_length + 1];

    strcpy(dir, path);
    strcpy(file, path);

    char *parent_path = dirname(dir);
    char *filename = basename(file);

    struct wfs_inode *parent = get_inode(parent_path);

    if (parent == 0) {
        printf("Could not get parents inode\n");
        return 0;
    }

    if (S_ISDIR(parent->mode) == 0) {
        printf("Parent is not a directory\n");
        return 0;
    }


    int blocks_alloced = parent->size / BLOCK_SIZE;
    struct wfs_dentry *entries;
    int entries_per_block = BLOCK_SIZE / sizeof(struct wfs_dentry);

    struct wfs_disk *disk;
    off_t block_offset = -1;
    int entry_ind;

    // check all allocated blocks to find a free entry
    for (int i = 0; i < blocks_alloced; i++) {

        disk = &disks[disk_index(parent->blocks[i])];
        entries = (struct wfs_dentry *) (disk->addr + disk->sb->d_blocks_ptr + disk_offset(parent->blocks[i]));

        for (entry_ind = 0; entry_ind < entries_per_block; entry_ind++) {
            
            if (entries[entry_ind].name[0] == '\0') {
                printf("Empty entry at ind: %d\n", entry_ind);
                block_offset = parent->blocks[i];
                goto found;
            }
        }
    }

    // if no free entries were found, try to allocate a block
    if (block_offset == -1) {

        if (blocks_alloced == IND_BLOCK) {
            printf("Directory is full\n");
            return 0;
        }

        block_offset = allocate_block(parent->num);

        if (block_offset == -1) {
            printf("Out of blocks\n");
            return 0;
        }

        disk = &disks[disk_index(block_offset)];
        entries = (struct wfs_dentry *)(disk->addr + disk->sb->d_blocks_ptr + disk_offset(block_offset));
        entry_ind = 0;

        blocks_alloced++;
    }

found:

    if (strlen(entries[entry_ind].name) != 0) {
        printf("Current entry is being used\n");
        return 0;
    }


    // increment the number of links for this inode on all disks
    for (int i = 0; i < num_disks; i++) {
        struct wfs_inode *node = (struct wfs_inode *) (disks[i].addr + disks[i].sb->i_blocks_ptr + (parent->num * BLOCK_SIZE));
        node->nlinks++;
    }

    strcpy(entries[entry_ind].name, filename);
    entries[entry_ind].num = inum;

    if (raid_mode != 0) {
        struct wfs_dentry *toCopy;
        for (int i = 0; i < num_disks; i++) {
            toCopy = (struct wfs_dentry *) (disks[i].addr + disks[i].sb->d_blocks_ptr + disk_offset(block_offset));
            memcpy((char *) &toCopy[entry_ind], (char *) &entries[entry_ind], sizeof(struct wfs_dentry));
        }
    }

    return &entries[entry_ind];
}



static int wfs_getattr(const char *path, struct stat *stbuf) {

    struct wfs_inode *inode = get_inode(path);
    if (inode == 0) {
        printf("Getattr: failed to find path\n");
        return -ENOENT;
    }
    

    stbuf->st_ino = inode->num;
    stbuf->st_mode = inode->mode;
    stbuf->st_uid = inode->uid;
    stbuf->st_gid = inode->gid;
    stbuf->st_size = inode->size;
    stbuf->st_nlink = inode->nlinks + 1;
    stbuf->st_atime = time(NULL);
    stbuf->st_mtime = inode->mtim;
    stbuf->st_ctime = inode->ctim;


    return 0; // Return 0 on success
}

static int wfs_mknod(const char *path, mode_t mode, dev_t dev) {

    if (get_inode(path) != 0) {
        printf("File exists already\n");
        return -EEXIST;
    }

    time_t curr_time = time(NULL);
    struct wfs_inode model = {
        .num = -1,                  /* Inode number */
        .mode = mode,               /* File type and mode */
        .uid = getuid(),            /* User ID of owner */
        .gid = getgid(),            /* Group ID of owner */
        .size = 0,                  /* Total size, in bytes */
        .nlinks = 2,                /* Number of links */

        .atim = curr_time,          /* Time of last access */
        .mtim = curr_time,          /* Time of last modification */
        .ctim = curr_time,          /* Time of last status change */

        .blocks = {0}
    };


    struct wfs_inode *new_inode = allocate_inode(&model);
    if (new_inode == 0) {
        printf("Failed to allocate inode\n");
        return -ENOSPC;
    }

    struct wfs_dentry *entry = alloc_dentry(path, new_inode->num);
    if (entry == 0) {
        printf("Failed to create dentry\n");
        return -ENOSPC;
    }

    return 0;
}

static int wfs_mkdir(const char *path, mode_t mode) {
    return wfs_mknod(path, S_IFDIR, 0);
}

static int wfs_unlink(const char *path) {

    struct wfs_inode *inode = get_inode(path);
    if (!inode) {
        perror("Couldn't find file\n");
        return -ENOENT;
    }

    if (!S_ISREG(inode->mode)) {
        perror("Not regular file\n");
        return -EISDIR;
    }

    // free allocated direct blocks
    for(int i = 0; i < IND_BLOCK; i++){
       if(inode->blocks[i] != -1){
           free_block(inode->blocks[i], inode);
           inode->blocks[i] = -1;
       }
    }

    // free allocated indirect blocks
    if (inode->blocks[IND_BLOCK] != -1) {
        struct wfs_disk *ind_disk = &disks[disk_index(inode->blocks[IND_BLOCK])];
        off_t *ind_blocks = (off_t *) (ind_disk->addr + ind_disk->sb->d_blocks_ptr + disk_offset(inode->blocks[IND_BLOCK]));
        int entries_per_block = BLOCK_SIZE/sizeof(off_t);
        for (int i = 0; i < entries_per_block; i++) {
            if (ind_blocks[i] != 0) {
                free_block(ind_blocks[i], inode);
            }
        }
        free_block(inode->blocks[IND_BLOCK], inode);
        inode->blocks[IND_BLOCK] = -1;
    }

    free_inode(inode);

    int path_length = strlen(path);
    char dir[path_length + 1];
    char file[path_length + 1];

    strcpy(dir, path);
    strcpy(file, path);

    char *parent_path = dirname(dir);
    char *filename = basename(file);

    struct wfs_inode *parent = get_inode(parent_path);

    // free entry in parent
    struct wfs_disk *disk;
    struct wfs_dentry *entries;
    int entries_per_block = BLOCK_SIZE/sizeof(struct wfs_dentry);
    for(int i = 0; i < IND_BLOCK; i++){

        if(parent->blocks[i] == -1) {
            continue;
        }

        off_t boffset = parent->blocks[i];
        disk = &disks[disk_index(boffset)];
        entries = (struct wfs_dentry *) (disk->addr + disk->sb->d_blocks_ptr + disk_offset(boffset));

        for(int j = 0; j < entries_per_block; j++){
            if(strcmp(entries[j].name, filename) == 0){

                if (raid_mode == 0) {
                    memset(&entries[j], 0, sizeof(struct wfs_dentry));
                } else {
                    for (int d = 0; d < num_disks; d++) {
                        entries = (struct wfs_dentry *) (disks[d].addr + disks[d].sb->d_blocks_ptr + disk_offset(boffset));
                        memset(&entries[j], 0, sizeof(struct wfs_dentry));
                    }
                }
                for (int d = 0; d < num_disks; d++) {
                    num2inode(parent->num, d)->nlinks--;
                }
                break;
            }
        }
   }

    return 0;
}


static int wfs_rmdir(const char *path) {

    struct wfs_inode *inode = get_inode(path);
    if (!inode) {
        perror("Couldn't find directory\n");
        return -ENOENT;
    }

    if (!S_ISDIR(inode->mode)) {
        perror("Not a directory\n");
        return -EISDIR;
    }

    if (inode->size != 0) {
        printf("Error: Directory not empty.\n");
        return -ENOTEMPTY; // Directory not empty
    }


    for (int i = 0; i < IND_BLOCK; i++) {
        if (inode->blocks[i] != -1) {
            free_block(inode->blocks[i], inode);
            inode->blocks[i] = -1;
        }
    }

    free_inode(inode);

    int path_length = strlen(path);
    char dir[path_length + 1];
    char file[path_length + 1];

    strcpy(dir, path);
    strcpy(file, path);

    char *parent_path = dirname(dir);
    char *filename = basename(file);

    struct wfs_inode *parent = get_inode(parent_path);

    // free entry in parent
    struct wfs_disk *disk;
    struct wfs_dentry *entries;
    int entries_per_block = BLOCK_SIZE/sizeof(struct wfs_dentry);
    for(int i = 0; i < IND_BLOCK; i++){

        if(parent->blocks[i] == -1) {
            continue;
        }

        off_t boffset = parent->blocks[i];
        disk = &disks[disk_index(boffset)];
        entries = (struct wfs_dentry *) (disk->addr + disk->sb->d_blocks_ptr + disk_offset(boffset));

        for(int j = 0; j < entries_per_block; j++){
            if(strcmp(entries[j].name, filename) == 0) {

                if (raid_mode == 0) {
                    memset(&entries[j], 0, sizeof(struct wfs_dentry));
                } else {
                    for (int d = 0; d < num_disks; d++) {
                        entries = (struct wfs_dentry *) (disks[d].addr + disks[d].sb->d_blocks_ptr + disk_offset(boffset));
                        memset(&entries[j], 0, sizeof(struct wfs_dentry));
                    }
                }
                
                for (int d = 0; d < num_disks; d++) {
                    num2inode(parent->num, d)->nlinks--;
                }
                break;
            }
        }
   }
    
    return 0;
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {

    struct wfs_inode *inode = get_inode(path);
    if (inode == 0) {
        printf("Could not find file\n");
        return -ENOENT;
    }


    off_t start = offset;
    off_t end = start + size - 1;

    int start_block = start / BLOCK_SIZE;
    int end_block = end / BLOCK_SIZE;

    int bytes_read = 0;
    struct wfs_disk *copyTo;

    for (int i = start_block; i <= end_block; i++) {

        off_t block_offset;

        if (i < IND_BLOCK) {   // Direct block

            if (raid_mode == 2) {
                int occurences[num_disks];
                memset(occurences, 0, sizeof(occurences));
                struct wfs_disk *curr_disk;
                for (int j = 0; j < num_disks; j++) {
                    
                    curr_disk = &disks[j];
                    char *block1 = (curr_disk->addr + curr_disk->sb->d_blocks_ptr + num2inode(inode->num, j)->blocks[i]);

                    for (int d = 0; d < num_disks; d++) {

                        char *block2 = (disks[d].addr + disks[d].sb->d_blocks_ptr + num2inode(inode->num, d)->blocks[i]);
                        
                        if (memcmp(block1, block2, BLOCK_SIZE) == 0) {
                            occurences[j]++;
                        }
                    }
                }

                int max_val = 0;
                int max_ind = 0;
                for (int d = 0; d < num_disks; d++) {
                    if (occurences[d] > max_val) {
                        max_val = occurences[d];
                        max_ind = d;
                    }
                }

                block_offset = num2inode(inode->num, max_ind)->blocks[i];
                char *toCopy = (disks[max_ind].addr + disks[max_ind].sb->d_blocks_ptr + block_offset);
                char *copyTo;
                for (int d = 0; d < num_disks; d++) {
                    copyTo = (disks[d].addr + disks[d].sb->d_blocks_ptr + block_offset);
                    memcpy(copyTo, toCopy, BLOCK_SIZE);
                }


            } else {
                block_offset = inode->blocks[i];
            }

            if (block_offset == -1) {
                goto sync;
            }
        } else {                // Indirect block

            if (inode->blocks[IND_BLOCK] == -1) {
                goto sync;
            }
 
            struct wfs_disk *ind_disk = &disks[disk_index(inode->blocks[IND_BLOCK])];
            off_t *ind_blocks = (off_t *) (ind_disk->addr + ind_disk->sb->d_blocks_ptr + disk_offset(inode->blocks[IND_BLOCK]));
            block_offset = ind_blocks[i - IND_BLOCK];

            if (block_offset == 0) {
                goto sync;
            }
        }

        off_t block_start = (start + bytes_read) % BLOCK_SIZE;
        size_t remain = size - bytes_read;
        size_t block_to_read = (remain > BLOCK_SIZE - block_start) ? BLOCK_SIZE - block_start : remain;
        struct wfs_disk *disk = &disks[disk_index(block_offset)];

        memcpy(buf + bytes_read, (char *) disk->addr + disk->sb->d_blocks_ptr + disk_offset(block_offset) + block_start, block_to_read);

        bytes_read += block_to_read;
    }

sync:

    for (int i = 0; i < num_disks; i++) {
        copyTo = &disks[i];
        struct wfs_inode *copy = (struct wfs_inode *) (copyTo->addr + copyTo->sb->i_blocks_ptr + (inode->num * BLOCK_SIZE));

        time_t curr_time = time(NULL);
        copy->atim = curr_time;
    }


    return bytes_read;
}

static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {

    struct wfs_inode *inode = get_inode(path);
    if (inode == 0) {
        printf("Could not find file\n");
        return -ENOENT;
    }

    off_t start = offset;
    off_t end = start + size - 1;

    int start_block = start / BLOCK_SIZE;
    int end_block = end / BLOCK_SIZE;

    int bytes_written = 0;
    int alloc_block = 0;

    for (int i = start_block; i <= end_block; i++) {

        off_t block_offset;

        if (i < IND_BLOCK) {   // Direct block

            block_offset = inode->blocks[i];

            if (block_offset == -1) {
                inode->blocks[i] = allocate_block(inode->num);
                if (inode->blocks[i] == 0) {
                    printf("Failed to allocate block\n");
                    return -ENOSPC;
                }
                alloc_block = 1;
                block_offset = inode->blocks[i];
            }

        
        } else {                // Indirect block

            if (inode->blocks[IND_BLOCK] == -1) {
                inode->blocks[IND_BLOCK] = allocate_block(inode->num);
                if (inode->blocks[IND_BLOCK] == 0) {
                    printf("Failed to allocate block\n");
                    return -ENOSPC;
                }
            }

            
            struct wfs_disk *ind_disk = &disks[disk_index(inode->blocks[IND_BLOCK])];
            off_t *ind_blocks = (off_t *) (ind_disk->addr + ind_disk->sb->d_blocks_ptr + disk_offset(inode->blocks[IND_BLOCK]));
            

            block_offset = ind_blocks[i - IND_BLOCK];
            if (block_offset == 0) {
                ind_blocks[i - IND_BLOCK] = allocate_block(inode->num);
                if (ind_blocks[i - IND_BLOCK] == 0) {
                    printf("Failed to allocate block\n");
                    return -ENOSPC;
                }

                if (raid_mode != 0) {
                    for (int d = 0; d < num_disks; d++) {
                        memcpy(disks[d].addr + disks[d].sb->d_blocks_ptr + disk_offset(inode->blocks[IND_BLOCK]), ind_blocks, BLOCK_SIZE);
                    }
                }
                alloc_block = 1;
                block_offset = ind_blocks[i - IND_BLOCK];
            }
        }


        off_t block_start = (start + bytes_written) % BLOCK_SIZE;
        size_t remain = size - bytes_written;
        size_t block_to_write = (remain > BLOCK_SIZE - block_start) ? BLOCK_SIZE - block_start : remain;

        struct wfs_disk *disk = &disks[disk_index(block_offset)];


        if (raid_mode == 0) {
            memcpy(disk->addr + disk->sb->d_blocks_ptr + disk_offset(block_offset) + block_start, buf + bytes_written, block_to_write);
    
        } else {
            for (int d = 0; d < num_disks; d++) {
                disk = &disks[d];
                memcpy(disk->addr + disk->sb->d_blocks_ptr + disk_offset(block_offset) + block_start, buf + bytes_written, block_to_write);
            }
        }

        bytes_written += block_to_write;

    }


    struct wfs_disk *copyTo;
    for (int i = 0; i < num_disks; i++) {
        copyTo = &disks[i];
        struct wfs_inode *copy = (struct wfs_inode *) (copyTo->addr + copyTo->sb->i_blocks_ptr + (inode->num * BLOCK_SIZE));

        if (alloc_block) {
            int off = (bytes_written % BLOCK_SIZE);
            copy->size -= (off == 0) ? 0 : BLOCK_SIZE - off;
        } else {
            copy->size += bytes_written;
        }
        time_t curr_time = time(NULL);
        copy->mtim = curr_time;
        copy->atim = curr_time;
        copy->ctim = curr_time;

    }


    return bytes_written;
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    
    filler(buf, ".", NULL, 0);  // Current directory
    filler(buf, "..", NULL, 0); // Parent directory

    struct wfs_inode *inode = get_inode(path);
    if (inode == NULL || !S_ISDIR(inode->mode)) {
        return -ENOENT; // Return error if directory doesn't exist
    }

    int num_data_blocks = inode->size / BLOCK_SIZE;
    int entries_per_block = BLOCK_SIZE / sizeof(struct wfs_dentry);

    struct wfs_dentry *entries;
    off_t block_offset;
    struct wfs_disk *disk;

    int first_dblock = offset / BLOCK_SIZE;

    for (int b = first_dblock; b < num_data_blocks; b++) { // for each allocated block in directory

        block_offset = inode->blocks[b];
        disk = &disks[disk_index(block_offset)];     // disk this block is currently on
        entries = (struct wfs_dentry *) (disk->addr + disk->sb->d_blocks_ptr + disk_offset(block_offset));
        int first_dentry = (b == first_dblock) ? (offset % BLOCK_SIZE) / sizeof(struct wfs_dentry) : 0;

        for (int e = first_dentry; e < entries_per_block; e++) {   // for each entry in block

            struct wfs_dentry *entry = &entries[e]; // current entry
            if (entry->name[0] == '\0') continue; // check next block
            
            filler(buf, entry->name, NULL, 0);

        }
    }

    return 0;
}

static struct fuse_operations ops = {
    .getattr = wfs_getattr,
    .mknod   = wfs_mknod,
    .mkdir  = wfs_mkdir,
    .unlink  = wfs_unlink,
    .rmdir   = wfs_rmdir,
    .read    = wfs_read,
    .write   = wfs_write,
    .readdir = wfs_readdir
};


int main(int argc, char *argv[]) {
    // Initialize FUSE with specified operations
    // Filter argc and argv here and then pass it to fuse_main

    char *wfs_args[argc];

    char *arg = argv[1];
    int ind = 0;
    while (arg[0] != '-') { // ./wfs disk1 disk2 disk3 -s -f mnt
        wfs_args[ind] = arg;
        ind++;
        arg = argv[ind+1];
    }

    if (ind == 0) {
        printf("No disks specified\n");
        exit(1);
    }

    // initialize all disks to 0
    memset(disks, 0, sizeof(disks));

    for (int i = 0; i < ind; i++) {
        
        // open disk
        int fd;
        if ((fd = open(wfs_args[i], O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH | S_IWOTH)) < 0) {
            printf("Cannot open disk %s\n", wfs_args[i]);
            exit(1);
        }
        
        // get size of disk
        struct stat st;
        fstat(fd, &st);
        int size = st.st_size;

        // map disk into memory
        char *sb = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED, fd, 0); 
        if (sb == MAP_FAILED) {
            printf("mmap failed\n");
            exit(1);
        }
        
        struct wfs_sb *sblock = (struct wfs_sb *) sb;
        int disk_id = sblock->disk_id;

        // check the number of disks in the sb match the number of disks passed
        if (sblock->num_disks != ind) {
            printf("Differing disk counts\n");
            exit(1);
        }

        // check that the disk_id was properly initialized and can be used as disk index
        if (disk_id >= sblock->num_disks) {
            printf("Disk ID not set properly\n");
            exit(1);
        }

        // check that this disk has not already been passed
        if (disks[disk_id].addr != 0) {
            printf("Disk ID %d already being used\n", disk_id);
            exit(1);
        }

        
        struct wfs_disk disk = {
            .id = disk_id,
            .addr = sb,
            .sb = (struct wfs_sb *) sb
        };

        // copy this disk to the disks array (NOTE: when changing metadata of one disk, must copy it to all disks)
        memcpy(&disks[disk_id], &disk, sizeof( struct wfs_disk ));

        close(fd);
        //printf("wfs: %s\n", wfs_args[i]);
    }

    raid_mode = disks[0].sb->raid_mode;
    num_disks = disks[0].sb->num_disks;

    int ind2 = argc - ind;
    argv[ind] = argv[0];
    

    int val = fuse_main(ind2, argv + ind, &ops, NULL);
    return val;
}
