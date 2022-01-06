#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sfs_api.h"
#include "disk_emu.h"

#define BLOCKSIZE 1024
#define NUM_BLOCKS 1024
#define NUMDATABLOCKS 1020
#define MAXFILENAME 32 // change to 20 if following the pdf

/* global variables */
int currentposition = -1; // current position in directory => sfs_getnextfile()

/* on disk data structures
 * addresses:
 * super block: 0
 * i-Node table: 1
 * directory: 2
 * free byte map: 3
 * data blocks: 4~1023 (1020 data blocks) */

struct superblock
{
    int magic;            // 0xACBD0005
    int blocksize;        // 1024
    int fssize;           // # blks
    int inodetablelength; // # blks
    int rootinode;        // i-Node#
} super;

struct inode
{
    int size;       // file size (in bytes)
    int direct[12]; // 12 pointers to data blocks
    int indirect;   // index of an index block
    int occupied;   // same as "available" of directory entry
};

struct direntry
{
    char *filename;
    int inodenumber;
    int occupied;
};

/* in memory data structures (cache) */
struct superblock supercache;
struct inode inodetablecache[NUMDATABLOCKS];
struct direntry directorycache[NUMDATABLOCKS];
unsigned char freebytemapcache[NUMDATABLOCKS]; // 'E': empty; 'F': full

struct oftentry
{
    int occupied;
    int inode;
    int rwpointer;
};
struct oftentry openfiletable[NUMDATABLOCKS];

/* sfs functions */

void mksfs(int fresh)
{
    if (fresh == 0)
    {
        // open fs from existing disk
        init_disk("disk", BLOCKSIZE, NUM_BLOCKS);
    }
    else
    {
        // create new fs: initialize new disk
        init_fresh_disk("disk", BLOCKSIZE, NUM_BLOCKS);

        // initialize the super block
        super.magic = 0xACBD0005;
        super.blocksize = BLOCKSIZE;
        super.fssize = NUM_BLOCKS;
        super.inodetablelength = NUMDATABLOCKS;
        super.rootinode = 0;                    // the first i-Node is the directory
        write_blocks(0, 1, &super);             // write super block to disk (first data block)
        memcpy(&supercache, &super, BLOCKSIZE); // cache super block in memory

        // inode of the root directory
        inodetablecache[0].occupied = 1;
        inodetablecache[0].size = 0; // = # files = # directory entries

        // initialize i-Node table
        write_blocks(1, 1, inodetablecache);

        // initialize directory
        write_blocks(2, 1, directorycache);

        // initialize free byte map
        for (int i = 0; i < NUMDATABLOCKS; i++)
        {
            freebytemapcache[i] = 'E'; // initialize all blocks to be free
        }
        write_blocks(3, 1, freebytemapcache);

        // initialize datablocks
        char datablocks[BLOCKSIZE * NUMDATABLOCKS];
        write_blocks(4, NUMDATABLOCKS, datablocks);
    }
}

int sfs_getnextfilename(char *fname)
{
    for (int i = currentposition; i < NUMDATABLOCKS - 1; i++)
    {                                                      // i = -1 : 1022
        currentposition++;                                 // cp = 0 : 1023
        if (directorycache[currentposition].occupied == 1) // file available
        {
            fname = directorycache[currentposition].filename;
            break;
        }
    }
    for (int j = currentposition + 1; j < NUMDATABLOCKS; j++)
    {                                        // j = 1 : 1023
        if (directorycache[j].occupied == 1) // if there is a next file in the directory
            return 0;
    }
    // else: all files are returned, back to beginning of the directory
    currentposition = -1;
    return -1;
}

int sfs_getfilesize(const char *path)
{
    for (int i = 0; i < NUMDATABLOCKS; i++)
    { // find the directory entry of the file with the same name
        if (directorycache[i].occupied == 1 && directorycache[i].filename == path)
            // directory entry => i-Node number => i-Node => size
            return inodetablecache[directorycache[i].inodenumber].size;
    }
    printf("file not found\n");
    return -1;
}

int sfs_fopen(char *fname)
{
    if (strlen(fname) > MAXFILENAME)
    {
        printf("length of file should be within %d characters\n", MAXFILENAME);
        return -1;
    }

    /* file exists */
    for (int i = 0; i < NUMDATABLOCKS; i++)
    {
        if (directorycache[i].occupied == 1 && directorycache[i].filename == fname)
        {
            printf("\nOPEN EXISTING FILE : %s\n", fname);
            // found the file from directory, get its i-Node number
            int inodenumber = directorycache[i].inodenumber;
            // put into the open file table
            for (int j = 0; j < NUMDATABLOCKS; j++)
            {
                // find an empty OFT entry
                if (openfiletable[j].occupied == 0)
                {
                    openfiletable[j].occupied = 1;
                    openfiletable[j].inode = inodenumber;
                    // default: set the r/w pointer at the end of the file
                    openfiletable[j].rwpointer = inodetablecache[inodenumber].size;
                    break;
                }
            }
            return i;
        }
    }

    /* file does not exist: create new file */
    printf("\nCREATE NEW FILE : %s\n", fname);

    // allocate an empty i-Node
    int inodenumber;
    for (int i = 0; i < NUMDATABLOCKS; i++)
    {
        if (inodetablecache[i].occupied == 0)
        {
            inodenumber = i;
            break;
        }
    }
    inodetablecache[inodenumber].occupied = 1;
    inodetablecache[inodenumber].size = 0;
    write_blocks(1, 1, inodetablecache);

    // assign an empty directory entry
    int index;
    for (int i = 0; i < NUMDATABLOCKS; i++)
    {
        if (directorycache[i].occupied == 0)
        {
            index = i;
            break;
        }
    }
    directorycache[index].occupied = 1;
    directorycache[index].filename = fname;
    directorycache[index].inodenumber = inodenumber;
    write_blocks(2, 1, directorycache);

    // put into an empty entry in the open file table
    int oftindex;
    for (int i = 0; i < NUMDATABLOCKS; i++)
    {
        if (openfiletable[i].occupied == 0)
        {
            oftindex = i;
            break;
        }
    }
    openfiletable[oftindex].occupied = 1;
    openfiletable[oftindex].inode = inodenumber;
    openfiletable[oftindex].rwpointer = 0;

    return index; // index in the directory = fileID
}

int sfs_fclose(int fileID)
{
    printf("\nCLOSE FILE %d\n", fileID);
    int inodenumber;
    if (directorycache[fileID].occupied == 1)
        // find the file in the directory and get its i-Node number
        inodenumber = directorycache[fileID].inodenumber;
    else
    {
        printf("file not in the directory\n");
        return -1;
    }

    for (int i = 0; i < NUMDATABLOCKS; i++)
    {
        if (openfiletable[i].inode == inodenumber && openfiletable[i].occupied == 1)
        { // remove the file from the open file table
            openfiletable[i].occupied = 0;
            return 0;
        }
        else
        {
            // printf("inodenumber = %d\n", openfiletable[i].inode);
            // printf("occupied = %d\n", openfiletable[i].occupied);
        }
    }
    printf("file already closed\n");
    return -1;
}

int sfs_fwrite(int fileID, const char *buffer, int length)
{
    printf("\nWRITE %d bytes TO FILE %d\n", length, fileID);
    int inodenumber = directorycache[fileID].inodenumber;
    int wpointer; // write from the rwpointer
    for (int i = 0; i < NUMDATABLOCKS; i++)
    {
        if (openfiletable[i].inode == inodenumber && openfiletable[i].occupied == 1)
        {
            wpointer = openfiletable[i].rwpointer;
            openfiletable[i].rwpointer += length; // update the r/w pointer
            break;
        }
    }

    // update file size in i-Node
    inodetablecache[inodenumber].size += length;

    // read the indirect index block
    int indexblock[BLOCKSIZE / sizeof(int)];
    read_blocks(inodetablecache[inodenumber].indirect, 1, indexblock);

    int fileblocks = wpointer / BLOCKSIZE; // # full data blocks occupied by the file
    int address;                           // address of the last data block
    if (fileblocks < 12)
        address = inodetablecache[inodenumber].direct[fileblocks];
    else
        address = indexblock[fileblocks - 12];

    int offset = wpointer % BLOCKSIZE; // offset of rwpointer in the last block
    char buf[BLOCKSIZE];               // content from rwpointer of the last block
    read_blocks(address, 1, buf);
    char content[length + offset]; // total content to write to disk
    strcpy(content, buf);
    // append given buffer to content of the last block
    for (int i = offset, j = 0; j < length; i++, j++)
    {
        content[i] = buffer[j];
        content[i + 1] = 0;
    }

    int bufferblocks = (length + offset) / BLOCKSIZE + 1; // total number of blocks to write

    for (int i = 0; i < bufferblocks; i++)
    {
        for (int j = 0; j < NUMDATABLOCKS; j++)
        {
            if (freebytemapcache[j] == 'E') // find a free block to write
            {
                freebytemapcache[j] = 'F';

                // update i-Node pointer
                if (fileblocks + i < 12)
                    inodetablecache[inodenumber].direct[fileblocks + i] = j;
                else
                    indexblock[fileblocks + i - 12] = j;

                // write one block of content to the free block
                write_blocks(j, 1, &content[i * BLOCKSIZE]);

                break;
            }
        }
    }

    // flush cache back to disk
    write_blocks(1, 1, inodetablecache);
    write_blocks(3, 1, freebytemapcache);

    return length;
}

int sfs_fread(int fileID, char *buffer, int length)
{
    printf("\nREAD %d bytes FROM FILE %d\n", length, fileID);
    int inodenumber = directorycache[fileID].inodenumber;
    int rpointer; // read from the rwpointer
    for (int i = 0; i < NUMDATABLOCKS; i++)
    {
        if (openfiletable[i].inode == inodenumber)
        {
            rpointer = openfiletable[i].rwpointer;
            // reading does not move the rwpointer
            break;
        }
    }

    int fileblocks = rpointer / BLOCKSIZE;   // number of full blocks
    int address;                             // address of the first block to read
    int indexblock[BLOCKSIZE / sizeof(int)]; // indirect index block
    if (fileblocks < 12)
        address = inodetablecache[inodenumber].direct[fileblocks];
    else if (fileblocks == 12)
    {
        // initialize the indirect index block
        for (int i = 0; i < NUMDATABLOCKS; i++)
        {
            if (freebytemapcache[i] == 'E')
                inodetablecache[inodenumber].indirect = i;
            break;
        }
        address = indexblock[fileblocks - 12]; // indexblock[0]
    }
    else // fileblocks > 12
    {
        // access existing indirect index block
        read_blocks(inodetablecache[inodenumber].indirect, 1, indexblock);
        address = indexblock[fileblocks - 12];
    }

    int offset = rpointer % BLOCKSIZE; // offset within the first block to read
    char buf[BLOCKSIZE];
    read_blocks(address, 1, buf);
    int bufferblocks = (length - (BLOCKSIZE - offset)) / BLOCKSIZE + 1; // number of blocks remaining (except the first block) to read
    char *buf1 = &buf[offset];                                          // content of the first block to read from the r/w pointer
    char result[length + 1];                                            // stores content read

    strcpy(result, buf1);

    for (int i = 0; i < bufferblocks; i++)
    {
        // find the address of next block to read from the i-Node
        int address;
        if (fileblocks + 1 + i < 12)
        {
            address = inodetablecache[inodenumber].direct[fileblocks + 1 + i];
            if (address == 0) // pointer uninitialized
                break;
        }
        else
            address = indexblock[fileblocks + 1 + i - 12];

        // read a block to the buffer
        char blockbuf[BLOCKSIZE];
        read_blocks(address, 1, blockbuf);
    }
    strcpy(buffer, result); // => segfault

    return length;
}

int sfs_fseek(int fileID, int loc)
{
    printf("\nSEEK TO LOCATION %d IN FILE %d\n", loc, fileID);
    // find the i-Node number of the file
    int inodenumber = directorycache[fileID].inodenumber;
    int filesize = inodetablecache[inodenumber].size;

    if (loc >= filesize)
    {
        printf("given location larger than file size: %d\n", filesize);
        return -1;
    }

    // update r/w pointer in the open file table
    for (int i = 0; i < NUMDATABLOCKS; i++)
    {
        if (openfiletable[i].inode == inodenumber)
        {
            openfiletable[i].rwpointer = loc;
            break;
        }
    }
    return 0;
}

int sfs_remove(char *file)
{
    for (int i = 0; i < NUMDATABLOCKS; i++)
    {
        struct inode fileinode;
        // remove from directory
        if (directorycache[i].filename == file)
        {
            directorycache[i].occupied = 0;

            // remove from i-Node table
            int inodenumber = directorycache[i].inodenumber;
            fileinode = inodetablecache[inodenumber];
            fileinode.occupied = 0;

            // remove from the open file table
            for (int j = 0; j < NUMDATABLOCKS; j++)
            {
                if (openfiletable[j].inode == inodenumber)
                {
                    openfiletable[j].occupied = 0;
                    break;
                }
            }
            return 0;
        }

        // free the data blocks (modify the free byte map)
        for (int j = 0; j < 12; j++)
        {
            freebytemapcache[fileinode.direct[j]] = 'E';
        }
        for (int j = 0; j < BLOCKSIZE / sizeof(int); j++)
        {
            int indexblock[BLOCKSIZE / sizeof(int)];
            read_blocks(fileinode.indirect, 1, indexblock);
            freebytemapcache[indexblock[j]] = 'E';
        }

        // write modification back to disk
        write_blocks(1, 1, inodetablecache);
        write_blocks(2, 1, directorycache);
        write_blocks(3, 1, freebytemapcache);
    }

    // if no matching file with the given name
    printf("file %s not found\n", file);
    return -1;
}