/***************************************************
*		版权声明
*
*	本操作系统名为：MINE
*	该操作系统未经授权不得以盈利或非盈利为目的进行开发，
*	只允许个人学习以及公开交流使用
*
*	代码最终所有权及解释权归田宇所有；
*
*	本模块作者：	田宇
*	EMail:		345538255@qq.com
*
*
***************************************************/


#include <printk.h>
#include <task.h>
#include <lib.h>
#include <vfs.h>
#include <errno.h>
#include <stdio.h>
#include <block/block.h>
#include <core/initcall.h>
#include "fat32.h"

static s64_t fatfs_wallclock_mktime(unsigned int year0, unsigned int mon0, unsigned int day, unsigned int hour, unsigned int min, unsigned int sec)
{
	unsigned int year = year0, mon = mon0;
	u64_t ret;

	if(0 >= (int)(mon -= 2))
	{
		mon += 12;
		year -= 1;
	}

	ret = (u64_t)(year / 4 - year / 100 + year / 400 + 367 * mon / 12 + day);
	ret += (u64_t)(year) * 365 - 719499;

	ret *= (u64_t)24;
	ret += hour;

	ret *= (u64_t)60;
	ret += min;

	ret *= (u64_t)60;
	ret += sec;

	return (s64_t)ret;
}

u32_t fatfs_pack_timestamp(struct fat_date date, struct fat_time time)
{
	return (u32_t)fatfs_wallclock_mktime(1980 + date.year, date.month, date.day, time.hours, time.minutes, time.seconds);
}


bool_t fat32_read_fat_entry(struct fat32_sb_info * fsbi, u32_t *fat_entry) {
	u32_t buf[128] = {0};
	if(*fat_entry > 0x0ffffff7)
		return FALSE;
	if(!block_read(fsbi->bdev, buf, (fsbi->first_fat_sector + (*fat_entry >> 7)) * fsbi->bytes_per_sector, fsbi->bytes_per_sector))
		return FALSE;
	*fat_entry = buf[*fat_entry & 0x7f] & 0x0fffffff;
	if(*fat_entry > 0x0ffffff7)
		return FALSE;
	return TRUE;
}


bool_t fat32_write_fat_entry(struct fat32_sb_info * fsbi, u32_t fat_entry, u32_t value) {
	unsigned int buf[128] = {0};

	if(!block_read(fsbi->bdev, buf, (fsbi->first_fat_sector + (fat_entry >> 7)) * fsbi->bytes_per_sector, fsbi->bytes_per_sector))
		return FALSE;
	buf[fat_entry & 0x7f] = (buf[fat_entry & 0x7f] & 0xf0000000) | (value & 0x0fffffff);

	for (int i = 0; i < fsbi->number_of_fat; i++){
		if(!block_write(fsbi->bdev, buf, (fsbi->first_fat_sector + fsbi->sectors_per_fat * i + (fat_entry >> 7)) * fsbi->bytes_per_sector, fsbi->bytes_per_sector))
			return FALSE;
	}
	return TRUE;
}


long FAT32_open(struct inode * inode, struct file * filp) {
	return 1;
}


long FAT32_close(struct inode * inode, struct file * filp) {
	return 1;
}


long FAT32_read(struct file * filp, char * buf, size_t count, off_t *pos) {
	u32_t filesize = filp->dentry->d_inode->i_size;
	
	if(filesize <= (u32_t) *pos)
		return 0;

	if (*pos + count > filesize)
		count = filesize - *pos;
	else
		count;

	return fat32_node_read(filp, buf, count, pos);
}

bool_t fat32_find_available_cluster(struct fat32_sb_info * fsbi, u32_t *fat_entry) {
	u32_t sector_per_fat = fsbi->sectors_per_fat;
	u32_t buf[128];

//	fsbi->fat_fsinfo->FSI_Free_Count & fsbi->fat_fsinfo->FSI_Nxt_Free not exactly,so unuse

	for (int i = 0; i < sector_per_fat; i++) {
		memset(buf, 0, 512);
		if(!block_read(fsbi->bdev, buf, (fsbi->first_fat_sector + i) * fsbi->bytes_per_sector, fsbi->bytes_per_sector))
			return FALSE;

		for (int j = 0; j < 128; j++) {
			if ((buf[j] & 0x0fffffff) == 0) {
				*fat_entry =  (i << 7) + j;
				return TRUE;
			}
		}
	}
	return FALSE;
}


long FAT32_write(struct file * filp, char * buf, size_t count, off_t * pos) {
	int ret = fat32_node_write(filp, buf, count, pos);

	if (*pos > filp->dentry->d_inode->i_size) {
		filp->dentry->d_inode->i_size = *pos;
		filp->dentry->d_inode->sb->sb_ops->write_inode(filp->dentry->d_inode);
	}

	return ret;
}


long FAT32_lseek(struct file * filp, long offset, long origin) {
	//struct index_node *inode = filp->dentry->dir_inode;
	long pos = 0;

	switch (origin) {
		case SEEK_SET:
			pos = offset;
			break;

		case SEEK_CUR:
			pos =  filp->f_pos + offset;
			break;

		case SEEK_END:
			pos = filp->dentry->d_inode->i_size + offset;
			break;

		default:
			return -EINVAL;
			break;
	}

	if (pos < 0 || pos > filp->dentry->d_inode->i_size)
		return -EOVERFLOW;

	filp->f_pos = pos;
	color_printk(GREEN, BLACK, "FAT32 FS(lseek) alert position:%d\n", filp->f_pos);

	return pos;
}


long FAT32_ioctl(struct inode * inode, struct file * filp, unsigned long cmd, unsigned long arg) {
	return -1;
}

long FAT32_readdir(struct file * filp, void * dirent, filldir_t filler) {
	struct fat32_inode_info * finode = filp->dentry->d_inode->private_index_info;
	struct fat32_sb_info * fsbi = filp->dentry->d_inode->sb->private_sb_info;

	unsigned int cluster = 0;
	unsigned long sector = 0;
	unsigned char * buf = NULL;
	char *name = NULL;
	int namelen = 0;
	int i = 0, j = 0, x = 0, y = 0;
	struct fat_dirent_t * tmpdentry = NULL;
	struct fat_longname_t * tmpldentry = NULL;

	buf = kmalloc(fsbi->bytes_per_cluster, 0);

	cluster = finode->first_cluster;

	j = filp->f_pos / fsbi->bytes_per_cluster;

	for (i = 0; i < j; i++) {
		if (!fat32_read_fat_entry(fsbi, &cluster)) {
			color_printk(RED, BLACK, "FAT32 FS(readdir) cluster didn`t exist\n");
			return NULL;
		}
	}

next_cluster:
	sector = fsbi->first_data_sector + (cluster - 2) * fsbi->sector_per_cluster;

	if (!block_read(fsbi->bdev, buf, sector * fsbi->bytes_per_sector, fsbi->bytes_per_cluster)) {
		color_printk(RED, BLACK, "FAT32 FS(readdir) read disk ERROR!!!!!!!!!!\n");
		kfree(buf);
		return NULL;
	}
	tmpdentry = (struct fat_dirent_t *)(buf + filp->f_pos % fsbi->bytes_per_cluster);

	for (i = filp->f_pos % fsbi->bytes_per_cluster; i < fsbi->bytes_per_cluster; i += 32, tmpdentry++, filp->f_pos += 32) {
		if (tmpdentry->file_attributes == ATTR_LONG_NAME)
			continue;
		if (tmpdentry->dos_file_name[0] == 0xe5 || tmpdentry->dos_file_name[0] == 0x00 || tmpdentry->dos_file_name[0] == 0x05)
			continue;

		namelen = 0;
		tmpldentry = (struct fat_longname_t *)tmpdentry - 1;

		if (tmpldentry->file_attributes == ATTR_LONG_NAME && tmpldentry->seqno != 0xe5 && tmpldentry->seqno != 0x00 && tmpldentry->seqno != 0x05) {
			j = 0;
			//long file/dir name read
			while (tmpldentry->file_attributes == ATTR_LONG_NAME  && tmpldentry->seqno != 0xe5 && tmpldentry->seqno != 0x00 && tmpldentry->seqno != 0x05) {
				j++;
				if (tmpldentry->seqno & 0x40)
					break;
				tmpldentry --;
			}

			name = kmalloc(j * 13 + 1, 0);
			memset(name, 0, j * 13 + 1);
			tmpldentry = (struct fat_longname_t *)tmpdentry - 1;

			for (x = 0; x < j; x++, tmpldentry --) {
				for (y = 0; y < 5; y++)
					if (tmpldentry->name_utf16_1[y] != 0xffff && tmpldentry->name_utf16_1[y] != 0x0000)
						name[namelen++] = (char)tmpldentry->name_utf16_1[y];

				for (y = 0; y < 6; y++)
					if (tmpldentry->name_utf16_2[y] != 0xffff && tmpldentry->name_utf16_2[y] != 0x0000)
						name[namelen++] = (char)tmpldentry->name_utf16_2[y];

				for (y = 0; y < 2; y++)
					if (tmpldentry->name_utf16_3[y] != 0xffff && tmpldentry->name_utf16_3[y] != 0x0000)
						name[namelen++] = (char)tmpldentry->name_utf16_3[y];
			}
			goto find_lookup_success;
		}

		name = kmalloc(15, 0);
		memset(name, 0, 15);
		//short file/dir base name compare
		for (x = 0; x < 8; x++) {
			if (tmpdentry->dos_file_name[x] == ' ')
				break;
			if (tmpdentry->DIR_NTRes & LOWERCASE_BASE)
				name[namelen++] = tmpdentry->dos_file_name[x] + 32;
			else
				name[namelen++] = tmpdentry->dos_file_name[x];
		}

		if (tmpdentry->file_attributes & ATTR_DIRECTORY)
			goto find_lookup_success;

		name[namelen++] = '.';

		//short file ext name compare
		for (x = 8; x < 11; x++) {
			if (tmpdentry->dos_file_name[x] == ' ')
				break;
			if (tmpdentry->DIR_NTRes & LOWERCASE_EXT)
				name[namelen++] = tmpdentry->dos_file_name[x] + 32;
			else
				name[namelen++] = tmpdentry->dos_file_name[x];
		}
		if (x == 8)
			name[--namelen] = 0;
		goto find_lookup_success;
	}

	if (fat32_read_fat_entry(fsbi, &cluster)) {
		printf("next_cluster\n");
		goto next_cluster;
	}

	kfree(buf);
	return NULL;

find_lookup_success:

	filp->f_pos += 32;
	return filler(dirent, name, namelen, 0, 0);
}


struct file_operations FAT32_file_ops = {
	.open = FAT32_open,
	.close = FAT32_close,
	.read = FAT32_read,
	.write = FAT32_write,
	.lseek = FAT32_lseek,
	.ioctl = FAT32_ioctl,

	.readdir = FAT32_readdir,
};


long FAT32_create(struct inode * inode, struct dentry * dentry, int mode) {
	return -1;
}


struct dentry * FAT32_lookup(struct inode * parent_inode, struct dentry * dest_dentry) {
	struct fat32_inode_info * finode = parent_inode->private_index_info;
	struct fat32_sb_info * fsbi = parent_inode->sb->private_sb_info;

	unsigned int cluster = 0;
	unsigned long sector = 0;
	unsigned char * buf = NULL;
	int i = 0, j = 0, x = 0;
	struct fat_dirent_t * tmpdentry = NULL;
	struct fat_longname_t * tmpldentry = NULL;
	struct inode * p = NULL;

	buf = kmalloc(fsbi->bytes_per_cluster, 0);

	cluster = finode->first_cluster;

next_cluster:
	sector = fsbi->first_data_sector + (cluster - 2) * fsbi->sector_per_cluster;
	color_printk(BLUE, BLACK, "lookup cluster:%#010x,sector:%#018lx\n", cluster, sector);
	if (!block_read(fsbi->bdev, buf, sector * fsbi->bytes_per_sector, fsbi->bytes_per_cluster)) {
		color_printk(RED, BLACK, "FAT32 FS(lookup) read disk ERROR!!!!!!!!!!\n");
		kfree(buf);
		return NULL;
	}

	tmpdentry = (struct fat_dirent_t *)buf;

	for (i = 0; i < fsbi->bytes_per_cluster; i += 32, tmpdentry++) {
		if (tmpdentry->file_attributes == ATTR_LONG_NAME)
			continue;
		if (tmpdentry->dos_file_name[0] == 0xe5 || tmpdentry->dos_file_name[0] == 0x00 || tmpdentry->dos_file_name[0] == 0x05)
			continue;

		tmpldentry = (struct fat_longname_t *)tmpdentry - 1;
		j = 0;

		//long file/dir name compare
		while (tmpldentry->file_attributes == ATTR_LONG_NAME && tmpldentry->seqno != 0xe5) {
			for (x = 0; x < 5; x++) {
				if (j > dest_dentry->name_length && tmpldentry->name_utf16_1[x] == 0xffff)
					continue;
				else if (j > dest_dentry->name_length || tmpldentry->name_utf16_1[x] != (unsigned short)(dest_dentry->name[j++]))
					goto continue_cmp_fail;
			}
			for (x = 0; x < 6; x++) {
				if (j > dest_dentry->name_length && tmpldentry->name_utf16_2[x] == 0xffff)
					continue;
				else if (j > dest_dentry->name_length || tmpldentry->name_utf16_2[x] != (unsigned short)(dest_dentry->name[j++]))
					goto continue_cmp_fail;
			}
			for (x = 0; x < 2; x++) {
				if (j > dest_dentry->name_length && tmpldentry->name_utf16_3[x] == 0xffff)
					continue;
				else if (j > dest_dentry->name_length || tmpldentry->name_utf16_3[x] != (unsigned short)(dest_dentry->name[j++]))
					goto continue_cmp_fail;
			}

			if (j >= dest_dentry->name_length) {
				goto find_lookup_success;
			}

			tmpldentry --;
		}

		//short file/dir base name compare
		j = 0;
		for (x = 0; x < 8; x++) {
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
			for (x = 8; x < 11; x++) {
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
	p = (struct inode *)kmalloc(sizeof(struct inode), 0);
	memset(p, 0, sizeof(struct inode));
	p->i_size = tmpdentry->file_size;
	p->blocks = (p->i_size + fsbi->bytes_per_cluster - 1) / fsbi->bytes_per_sector;
	p->i_mode |= (tmpdentry->file_attributes & ATTR_DIRECTORY) ? S_IFDIR : S_IFREG;
	p->i_mode |= (S_IRWXU | S_IRWXG | S_IRWXO);
	
	if(tmpdentry->file_attributes & ATTR_READ_ONLY)
	{
		p->i_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
	}

	p->i_ctime = fatfs_pack_timestamp(tmpdentry->create_date, tmpdentry->create_time);
	p->i_atime = fatfs_pack_timestamp(tmpdentry->laccess_date, (struct fat_time){0});
	p->i_mtime = fatfs_pack_timestamp(tmpdentry->lmodify_date, tmpdentry->lmodify_time);

	p->sb = parent_inode->sb;
	p->f_ops = &FAT32_file_ops;
	p->inode_ops = &FAT32_inode_ops;

	p->private_index_info = (struct fat32_inode_info *)kmalloc(sizeof(struct fat32_inode_info), 0);
	memset(p->private_index_info, 0, sizeof(struct fat32_inode_info));
	finode = p->private_index_info;

	finode->first_cluster = (tmpdentry->first_cluster_hi << 16 | tmpdentry->first_cluster_lo) & 0x0fffffff;
	finode->fsbi = fsbi;
	finode->dentry_location = cluster;
	finode->dentry_position = tmpdentry - (struct fat_dirent_t *)buf;
	finode->create_date = tmpdentry->create_date;
	finode->create_time = tmpdentry->create_time;
	finode->write_date = tmpdentry->lmodify_date;
	finode->write_time = tmpdentry->lmodify_time;

	dest_dentry->d_inode = p;
	kfree(buf);
	return dest_dentry;
}


long FAT32_mkdir(struct inode * inode, struct dentry * dentry, int mode) {
	return -1;
}

long FAT32_rmdir(struct inode * inode, struct dentry * dentry) {
	return -1;
}

long FAT32_rename(struct inode * old_inode, struct dentry * old_dentry, struct inode * new_inode, struct dentry * new_dentry) {
	return -1;
}

long FAT32_getattr(struct dentry * dentry, unsigned long * attr) {
	return -1;
}

long FAT32_setattr(struct dentry * dentry, unsigned long * attr) {
	return -1;
}

struct inode_operations FAT32_inode_ops = {
	.create = FAT32_create,
	.lookup = FAT32_lookup,
	.mkdir = FAT32_mkdir,
	.rmdir = FAT32_rmdir,
	.rename = FAT32_rename,
	.getattr = FAT32_getattr,
	.setattr = FAT32_setattr,
};


//// these operation need cache and list
long FAT32_compare(struct dentry * parent_dentry, char * source_filename, char * destination_filename) {
	return -1;
}
long FAT32_hash(struct dentry * dentry, char * filename) {
	return -1;
}
long FAT32_release(struct dentry * dentry) {
	return -1;
}
long FAT32_iput(struct dentry * dentry, struct inode * inode) {
	return -1;
}


struct dentry_operations FAT32_dentry_ops = {
	.compare = FAT32_compare,
	.hash = FAT32_hash,
	.release = FAT32_release,
	.iput = FAT32_iput,
};


void fat32_write_superblock(struct super_block * sb) {}

void fat32_put_superblock(struct super_block * sb) {
	kfree(sb->private_sb_info);
	kfree(sb->root->d_inode->private_index_info);
	kfree(sb->root->d_inode);
	kfree(sb->root);
	kfree(sb);
}

void fat32_write_inode(struct inode * inode) {
	struct fat_dirent_t * fdentry = NULL;
	struct fat_dirent_t * buf = NULL;
	struct fat32_inode_info * finode = inode->private_index_info;
	struct fat32_sb_info * fsbi = inode->sb->private_sb_info;
	unsigned long sector = 0;

	if (finode->dentry_location == 0) {
		color_printk(RED, BLACK, "FS ERROR:write root inode!\n");
		return ;
	}

	sector = fsbi->first_data_sector + (finode->dentry_location - 2) * fsbi->sector_per_cluster;
	buf = (struct fat_dirent_t *)kmalloc(fsbi->bytes_per_cluster, 0);
	memset(buf, 0, fsbi->bytes_per_cluster);
	block_read(fsbi->bdev, buf, sector * fsbi->bytes_per_sector, fsbi->bytes_per_cluster);
	fdentry = buf + finode->dentry_position;

	////alert fat32 dentry data
	fdentry->file_size = inode->i_size;
	fdentry->first_cluster_lo = finode->first_cluster & 0xffff;
	fdentry->first_cluster_hi = (fdentry->first_cluster_hi & 0xf000) | (finode->first_cluster >> 16);

	block_write(fsbi->bdev, buf, sector * fsbi->bytes_per_sector, fsbi->bytes_per_cluster);
	kfree(buf);
}

struct super_block_operations FAT32_sb_ops = {
	.write_superblock = fat32_write_superblock,
	.put_superblock = fat32_put_superblock,

	.write_inode = fat32_write_inode,
};

struct super_block * fat32_read_superblock(struct block_t * block) {
	struct super_block * sbp = NULL;
	struct fat32_inode_info * finode = NULL;
	struct fat_bootsec_t *fbs;
	struct fat32_sb_info * fsbi = NULL;
	char buf[512];
	////super block
	sbp = (struct super_block *)kmalloc(sizeof(struct super_block), 0);
	memset(sbp, 0, sizeof(struct super_block));

	sbp->sb_ops = &FAT32_sb_ops;
	sbp->private_sb_info = (struct fat32_sb_info *)kmalloc(sizeof(struct fat32_sb_info), 0);
	memset(sbp->private_sb_info, 0, sizeof(struct fat32_sb_info));

	fsbi = sbp->private_sb_info;
	fbs = &fsbi->bsec;
	fsbi->bdev = block;

	block_read(block, fbs, 0, sizeof(*fbs));

	fsbi->sector_count = block->blksz;
	fsbi->sector_per_cluster = fbs->sectors_per_cluster;
	fsbi->bytes_per_cluster = fbs->sectors_per_cluster * fbs->bytes_per_sector;
	fsbi->bytes_per_sector = fbs->bytes_per_sector;
	fsbi->first_data_sector = fbs->reserved_sector_count + fbs->e32.sectors_per_fat * fbs->number_of_fat;
	fsbi->first_fat_sector = fbs->reserved_sector_count;
	fsbi->sectors_per_fat = fbs->e32.sectors_per_fat;
	fsbi->number_of_fat = fbs->number_of_fat;
	fsbi->fsinfo_sector_infat = fbs->e32.fs_info_sector;
	fsbi->bootsector_bk_infat = fbs->e32.boot_sector_copy;

	color_printk(BLUE, BLACK, 
				 "FAT32 Boot Sector\n\tfs_info_sector:%#08lx\n\tboot_sector_copy:%#08lx\n\ttotal_sectors_32:%#08lx\n",
				 fbs->e32.fs_info_sector, fbs->e32.boot_sector_copy, fbs->total_sectors_32);

	////directory entry
	sbp->root = (struct dentry *)kmalloc(sizeof(struct dentry), 0);
	memset(sbp->root, 0, sizeof(struct dentry));

	init_list_head(&sbp->root->child_node);
	init_list_head(&sbp->root->subdirs_list);
	sbp->root->parent = sbp->root;
	sbp->root->dir_ops = &FAT32_dentry_ops;
	sbp->root->name = (char *)kmalloc(2, 0);
	sbp->root->name[0] = '/';
	sbp->root->name_length = 1;

	////index node
	sbp->root->d_inode = (struct inode *)kmalloc(sizeof(struct inode), 0);
	memset(sbp->root->d_inode, 0, sizeof(struct inode));
	sbp->root->d_inode->inode_ops = &FAT32_inode_ops;
	sbp->root->d_inode->f_ops = &FAT32_file_ops;
	sbp->root->d_inode->i_size = 0;
	sbp->root->d_inode->blocks = (sbp->root->d_inode->i_size + fsbi->bytes_per_cluster - 1) / fsbi->bytes_per_sector;
	sbp->root->d_inode->i_mode |= S_IFDIR;
	sbp->root->d_inode->sb = sbp;

	////fat32 root inode
	sbp->root->d_inode->private_index_info = (struct fat32_inode_info *)kmalloc(sizeof(struct fat32_inode_info), 0);
	memset(sbp->root->d_inode->private_index_info, 0, sizeof(struct fat32_inode_info));
	finode = (struct fat32_inode_info *)sbp->root->d_inode->private_index_info;
	finode->first_cluster = fbs->e32.root_directory_cluster;
	finode->fsbi = fsbi;
	finode->dentry_location = 0;
	finode->dentry_position = 0;
	finode->create_date = (struct fat_date){0};
	finode->create_time = (struct fat_time){0};
	finode->write_date = (struct fat_date){0};
	finode->write_time = (struct fat_time){0};

	return sbp;
}


struct filesystem_t fat = {
	.name = "FAT32",
	.fs_flags = 0,
	.read_superblock = fat32_read_superblock,
};

static __init void filesystem_sys_init(void)
{
	register_filesystem(&fat);
}

static __exit void filesystem_sys_exit(void)
{
	unregister_filesystem(&fat);
}

core_initcall(filesystem_sys_init);
core_exitcall(filesystem_sys_exit);

