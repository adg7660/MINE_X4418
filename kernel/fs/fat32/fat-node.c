#include <printk.h>
#include <task.h>
#include <lib.h>
#include <vfs.h>
#include <errno.h>
#include <stdio.h>
#include <block/block.h>
#include <core/initcall.h>
#include "fat32.h"

long fat32_read_cluster(struct fat32_sb_info * fsbi, u32_t cluster, u8_t *buf){
	u32_t sector = fsbi->first_data_sector + (cluster - 2) * fsbi->sector_per_cluster;
	if (!block_read(fsbi->bdev, buf, sector * fsbi->bytes_per_sector, fsbi->bytes_per_cluster)) {
		color_printk(RED, BLACK, "[FAT32] fat32_read_cluster ERROR!!!!!!!!!!\n");
		return -EIO;
	}
	return 0;
}

long fat32_write_cluster(struct fat32_sb_info * fsbi, u32_t cluster, u8_t *buf){
	u32_t sector = fsbi->first_data_sector + (cluster - 2) * fsbi->sector_per_cluster;
	if (!block_write(fsbi->bdev, buf, sector * fsbi->bytes_per_sector, fsbi->bytes_per_cluster)) {
		color_printk(RED, BLACK, "[FAT32] fat32_write_cluster ERROR!!!!!!!!!!\n");
		return -EIO;
	}
	return 0;
}

static long _fat32_node_read(struct fat32_sb_info * fsbi, u32_t cluster, u8_t *to, u8_t *buf, size_t count, off_t off){
	u8_t *from = buf;
	if (fat32_read_cluster(fsbi, cluster, buf))
		return -EIO;

	if ((unsigned long)buf < TASK_SIZE)
		copy_to_user(to, from + off, count);
	else
		memcpy(to, from + off, count);
	return 0;
}

u32_t fat32_node_read(struct file * filp, u8_t *buf, size_t count, off_t *pos) {
	struct fat32_inode_info * finode = filp->dentry->d_inode->private_index_info;
	struct fat32_sb_info * fsbi = finode->fsbi;

	u32_t cluster = finode->first_cluster;
	off_t off = *pos % fsbi->bytes_per_cluster;
	u32_t ret = 0;
	u32_t r = 0;

	if (!cluster)
		return -EFAULT;

	for (int i = *pos / fsbi->bytes_per_cluster; i > 0; i--) {
		if(!fat32_read_fat_entry(fsbi, &cluster))
			return -ENOSPC;
	}

	char * cached_data = (char *)kmalloc(fsbi->bytes_per_cluster, 0);

	do {
		memset(cached_data, 0, fsbi->bytes_per_cluster);
		int len = count <= fsbi->bytes_per_cluster - off ? count : fsbi->bytes_per_cluster - off;
		if(ret = _fat32_node_read(fsbi, cluster, buf, cached_data, len, off))
			break;
		count -= len;
		buf += len;
		off -= off;
		r += len;
		*pos += len;
	} while (count && (fat32_read_fat_entry(fsbi, &cluster)));

	kfree(cached_data);
	if (!count)
		ret = count;
	return r;
}

bool_t fat32_read_or_new_fat_entry(struct fat32_sb_info * fsbi, unsigned int *pcluster) {
	u32_t next_cluster = *pcluster;
	u32_t cluster = *pcluster;
	if(!fat32_read_fat_entry(fsbi, &next_cluster))
		return FALSE;
	if (next_cluster >= 0x0ffffff8) {
		if (!fat32_find_available_cluster(fsbi, &next_cluster))
			return FALSE;
		fat32_write_fat_entry(fsbi, cluster, next_cluster);
		fat32_write_fat_entry(fsbi, next_cluster, 0x0ffffff8);
		*pcluster = next_cluster;
	}
	return TRUE;
}

static long _fat32_node_write(struct fat32_sb_info * fsbi, u32_t cluster, u8_t *from, u8_t *buf, size_t count, off_t off){
	u8_t *to = buf;
	//TODO：新的cluster可以节省一次读写
	if (fat32_read_cluster(fsbi, cluster, buf))
		return -EIO;

	if ((unsigned long)buf < TASK_SIZE)
		copy_from_user(to + off, from, count);
	else
		memcpy(to + off, from, count);

	if (fat32_write_cluster(fsbi, cluster, buf))
		return -EIO;
	return 0;
}

u32_t fat32_node_write(struct file * filp, u8_t *buf, size_t count, off_t *pos) {
	struct fat32_inode_info * finode = filp->dentry->d_inode->private_index_info;
	struct fat32_sb_info * fsbi = finode->fsbi;

	u32_t cluster = finode->first_cluster;
	off_t off = *pos % fsbi->bytes_per_cluster;
	u32_t ret = 0;
	u32_t w= 0;

	if (!cluster) {
		if(!fat32_find_available_cluster(fsbi, &cluster))
			return -ENOSPC;
		finode->first_cluster = cluster;
		if(fat32_write_fat_entry(fsbi, cluster, 0x0ffffff8))
			return -EFAULT;
	} else {
		for (int i = *pos / fsbi->bytes_per_cluster; i > 0; i--) {
			if(!fat32_read_fat_entry(fsbi, &cluster))
				return -ENOSPC;
		}
	}

	char * cached_data = (char *)kmalloc(fsbi->bytes_per_cluster, 0);

	do {
		memset(cached_data, 0, fsbi->bytes_per_cluster);
		int len = count <= fsbi->bytes_per_cluster - off ? count : fsbi->bytes_per_cluster - off;
		if(ret = _fat32_node_write(fsbi, cluster, buf, cached_data, len, off))
			break;

		count -= len;
		buf += len;
		off -= off;
		w += len;
		*pos += len;
	}  while (count && (cluster = fat32_read_or_new_fat_entry(fsbi, &cluster)));

	kfree(cached_data);
	if (!count)
		ret = count;
	return ret;
}
int fat32_node_find_dirent(struct inode * parent_inode, struct dentry * dest_dentry) {
	struct fat32_inode_info * finode = parent_inode->private_index_info;
	struct fat32_sb_info * fsbi = parent_inode->sb->private_sb_info;
	struct fat_dirent_t * tmpdentry = NULL;
	struct fat_longname_t * tmpldentry = NULL;

	unsigned char *buf = kmalloc(fsbi->bytes_per_cluster, 0);

	unsigned int cluster = finode->first_cluster;

next_cluster:
	if(fat32_read_cluster(fsbi, cluster, buf))
		return ;

	tmpdentry = (struct fat_dirent_t *)buf;

	for (int i = 0; i < fsbi->bytes_per_cluster; i += 32, tmpdentry++) {
		if (tmpdentry->file_attributes == ATTR_LONG_NAME)
			continue;
		if (tmpdentry->dos_file_name[0] == 0xe5 || tmpdentry->dos_file_name[0] == 0x00 || tmpdentry->dos_file_name[0] == 0x05)
			continue;

		tmpldentry = (struct fat_longname_t *)tmpdentry - 1;
		int j = 0;

		//long file/dir name compare
		while (tmpldentry->file_attributes == ATTR_LONG_NAME && tmpldentry->seqno != 0xe5) {
			unsigned short name[13];
			for (int x = 0; x < 5; x++)
				name[x + 0] = tmpldentry->name_utf16_1[x];
			for (int x = 0; x < 6; x++)
				name[x + 5] = tmpldentry->name_utf16_2[x];
			for (int x = 0; x < 2; x++)
				name[x + 11] = tmpldentry->name_utf16_3[x];
			
			
			for (int x = 0; x < 13; x++) {
				if (j > dest_dentry->name_length) {
					if(name[x] != 0xffff) goto continue_cmp_fail;
				} else if (name[x] != (unsigned short)(dest_dentry->name[j++]))
					goto continue_cmp_fail;
			}

			if (j >= dest_dentry->name_length)
				goto find_lookup_success;

			tmpldentry --;
		}

		//short file/dir base name compare
		j = 0;
		for (int x = 0; x < 8; x++) {
			switch (tmpdentry->dos_file_name[x]) {
				case ' ':
					if (!(tmpdentry->file_attributes & ATTR_DIRECTORY)) {
						if (dest_dentry->name[j] == '.')
							continue;
						else if (tmpdentry->dos_file_name[x] == dest_dentry->name[j]) {
							j++;
							break;
						} else
							goto continue_cmp_fail;
					} else {
						if (j < dest_dentry->name_length && tmpdentry->dos_file_name[x] == dest_dentry->name[j]) {
							j++;
							break;
						} else if (j == dest_dentry->name_length)
							continue;
						else
							goto continue_cmp_fail;
					}

				case 'A' ... 'Z':
				case 'a' ... 'z':
					if (tmpdentry->DIR_NTRes & LOWERCASE_BASE)
						if (j < dest_dentry->name_length && tmpdentry->dos_file_name[x] + 32 == dest_dentry->name[j]) {
							j++;
							break;
						} else
							goto continue_cmp_fail;
					else {
						if (j < dest_dentry->name_length && tmpdentry->dos_file_name[x] == dest_dentry->name[j]) {
							j++;
							break;
						} else
							goto continue_cmp_fail;
					}

				case '0' ... '9':
					if (j < dest_dentry->name_length && tmpdentry->dos_file_name[x] == dest_dentry->name[j]) {
						j++;
						break;
					} else
						goto continue_cmp_fail;

				default :
					j++;
					break;
			}
		}
		//short file ext name compare
		if (!(tmpdentry->file_attributes & ATTR_DIRECTORY)) {
			j++;
			for (int x = 8; x < 11; x++) {
				switch (tmpdentry->dos_file_name[x]) {
					case 'A' ... 'Z':
					case 'a' ... 'z':
						if (tmpdentry->DIR_NTRes & LOWERCASE_EXT)
							if (tmpdentry->dos_file_name[x] + 32 == dest_dentry->name[j]) {
								j++;
								break;
							} else
								goto continue_cmp_fail;
						else {
							if (tmpdentry->dos_file_name[x] == dest_dentry->name[j]) {
								j++;
								break;
							} else
								goto continue_cmp_fail;
						}

					case '0' ... '9':
						if (tmpdentry->dos_file_name[x] == dest_dentry->name[j]) {
							j++;
							break;
						} else
							goto continue_cmp_fail;

					case ' ':
						if (tmpdentry->dos_file_name[x] == dest_dentry->name[j]) {
							j++;
							break;
						} else
							goto continue_cmp_fail;

					default :
						goto continue_cmp_fail;
				}
			}
		}
		goto find_lookup_success;

continue_cmp_fail:
		;
	}

	fat32_read_fat_entry(fsbi, &cluster);
	if (cluster < 0x0ffffff7)
		goto next_cluster;

	kfree(buf);
	return NULL;

find_lookup_success:
	return dest_dentry;
}
