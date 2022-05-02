/**
 * @file mailFS.c
 * @author Joshua Kissoon (kissoonjoshua@outlook.com)
 * @brief 
 * @version 1.0
 * @date 2022-05-01
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#define FUSE_USE_VERSION 29

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <stddef.h>
#include "mail.h"

#define MAX_NAME_LENGTH 256
#define MAX_DATA 4096
#define MAX_DATA_SIZE 65535 

typedef struct stats {
    char *name;
    char *path;
    char *dir;
    mode_t mode;
    nlink_t nlink;
    uid_t uid;
    gid_t gid;
    off_t size;
    blksize_t blksize;
    time_t atime;
    time_t mtime;
    int sent;
} st;

static struct superblock {
    st stats[MAX_DATA];
    char *data_bitmap[MAX_DATA];
    char username[MAX_NAME_LENGTH];
    char password[MAX_NAME_LENGTH];
} sb;

char *default_folders[] = {"/INBOX", "/OUTBOX"};

static struct options {
	int mail_help;
} options;

#define OPTION(t, p) {t, offsetof(struct options, p), 1}

static const struct fuse_opt option_spec[] = {
	OPTION("-h", mail_help),
	OPTION("--help", mail_help),
	FUSE_OPT_END
};

int get_open_slot() {
    for(int i = 0; i < MAX_DATA; i++) {
        if(sb.stats[i].path == NULL) return i;
    }
    return -1;
}

int check_data(const char *path) {
    for(int i = 0; i < MAX_DATA; i++) {
        if(sb.stats[i].path == NULL) continue;
        if(strcmp(path, sb.stats[i].path) == 0) return i;
    }
    return -1;
}

void get_names(const char *path, char *name, char *dir) {
    char *d, *n;
    d = strdup(path);
    n = strdup(path);
    strcpy(dir, dirname(d));
    strcpy(name, basename(n));
    free(d);
    free(n);
}

void fill_dirs(const char *path, void *buf, fuse_fill_dir_t filler) {
    for(int i = 0; i < MAX_DATA; i++) {
        if(sb.stats[i].dir == NULL) continue;
        if(strcmp(path, sb.stats[i].dir) == 0) filler(buf, sb.stats[i].name, NULL, 0);
    }
}

void create_data(const char *path, int slot, mode_t mode, int opt) {
    if(opt) {
        sb.data_bitmap[slot] = (char*)malloc(sizeof(char) * MAX_DATA_SIZE);
        strcpy(sb.data_bitmap[slot], "");
        sb.stats[slot].nlink = 1;
        sb.stats[slot].sent = 0;
    } else {
        sb.stats[slot].nlink = 2;
    }
    sb.stats[slot].path = (char*)malloc(sizeof(char) * MAX_NAME_LENGTH);
    strcpy(sb.stats[slot].path, path);
    sb.stats[slot].dir = (char*)malloc(sizeof(char) * MAX_NAME_LENGTH);
    sb.stats[slot].name = (char*)malloc(sizeof(char) * MAX_NAME_LENGTH);
    get_names(path, sb.stats[slot].name, sb.stats[slot].dir);
    sb.stats[slot].mode = mode;
    sb.stats[slot].uid = getuid();
    sb.stats[slot].gid = getgid();
    sb.stats[slot].size = MAX_DATA_SIZE;
    sb.stats[slot].blksize = 512;
    sb.stats[slot].atime = time(NULL);
    sb.stats[slot].mtime = time(NULL);
}

void delete_data(const char *path, int slot) {
    free(sb.stats[slot].path);
    free(sb.stats[slot].dir);
    free(sb.stats[slot].name);
    sb.stats[slot].path = NULL;
    sb.stats[slot].dir = NULL;
    sb.stats[slot].name = NULL;
    if(sb.data_bitmap[slot] != NULL) {
        free(sb.data_bitmap[slot]);
        sb.data_bitmap[slot] = NULL;
    }
}

char *get_filename(char *header) {
    char *loc = strstr(header, "Subject:");
    loc[strcspn(loc, "\r\n")] = 0;
    loc += 9;
    return loc;
}

int validate_email(char *data, char *to, char *body) {
    char *save;
    char *token;
    
    token = strtok_r(data, "[]", &save);
    if(token != NULL) {
        strcpy(to, token);
        token = strtok_r(NULL, "[]", &save);
        if(token != NULL) {
            strcpy(body, token);
            return 1;
        }
    }
    return 0;
}

static int mail_getattr(const char *path, struct stat *stbuf) {
	memset(stbuf, 0, sizeof(struct stat));

	if(strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0700;
		stbuf->st_nlink = 2;
	} else {
        int slot = check_data(path);
        if(slot == -1) {
            return -ENOENT;
        }
        stbuf->st_mode = sb.stats[slot].mode;       
        stbuf->st_nlink = sb.stats[slot].nlink;       
        stbuf->st_uid = sb.stats[slot].uid;       
        stbuf->st_gid = sb.stats[slot].gid;       
        stbuf->st_size = sb.stats[slot].size;       
        stbuf->st_blksize = sb.stats[slot].blksize;       
        stbuf->st_atime = sb.stats[slot].atime;       
        stbuf->st_mtime = sb.stats[slot].mtime;       
    }
	return 0;
}

static int mail_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	(void)offset;
	(void)fi;

    if(strcmp(path, "/") == 0 || check_data(path) != -1) {
        filler(buf, ".", NULL, 0);
        filler(buf, "..", NULL, 0);
        fill_dirs(path, buf, filler);
    } else {
		return -ENOENT;
    }
	return 0;
}

static int mail_mkdir(const char *path, mode_t mode) {
    (void)mode;
    int slot = get_open_slot();
    if(slot == -1 || check_data(path) != -1) {
        return -1;
    }
    create_data(path, slot, S_IFDIR | 0700, 0);

	return 0;
}

static int mail_rmdir(const char *path) {
    int slot = check_data(path);
    if(slot == -1) {
        return -ENOENT;
    }
    delete_data(path, slot);

	return 0;
}

static int mail_mknod(const char *path, mode_t mode, dev_t dev) {
    (void)mode;
    (void)dev;
    int slot = get_open_slot();
    if(slot == -1 || check_data(path) != -1) {
        return -1;
    }
    create_data(path, slot, S_IFREG | 0700, 1);

	return 0;
}

static int mail_unlink(const char *path) {
    int slot = check_data(path);
    if(slot == -1) {
        return -ENOENT;
    }
    delete_data(path, slot);

	return 0;
}

static int mail_open(const char *path, struct fuse_file_info *fi) {
    (void)fi;

	return 0;
}

static int mail_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	(void)fi;
    int slot = check_data(path);
    if(slot == -1) {
        return -ENOENT;
    }
    memcpy(buf, sb.data_bitmap[slot] + offset, size);

	return strlen(sb.data_bitmap[slot] - offset);
}

static int mail_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	(void)fi;
    int slot = check_data(path);
    if(slot == -1) {
        return -ENOENT;
    }
    strcpy(sb.data_bitmap[slot], buf);

	return size;
}

static int mail_release(const char *path, struct fuse_file_info *fi) {
    (void)fi;
    int slot = check_data(path);
    if(slot == -1) {
        return -ENOENT;
    }
    char to[MAX_NAME_LENGTH];
    char body[MAX_DATA_SIZE];
    if(strcmp(sb.stats[slot].dir, default_folders[1]) == 0 && sb.stats[slot].sent == 0) {
        if(validate_email(sb.data_bitmap[slot], to, body)) {
            send_mail(sb.username, sb.password, sb.stats[slot].name, to, body);
            sb.stats[slot].sent = 1;
        }
    }

    return 0;
}

static int mail_truncate(const char *path, off_t offset) {
    return 0;
}

static int mail_utimens(const char *path, const struct timespec tv[2]) {
    int slot = check_data(path);
    if(slot == -1) {
        return -ENOENT;
    }
    sb.stats[slot].atime = (time_t)tv[0].tv_sec;
    sb.stats[slot].mtime = (time_t)tv[1].tv_sec;

    return 0;
}

static int mail_chown(const char *path, uid_t uid, gid_t gid) {
    int slot = check_data(path);
    if(slot == -1) {
        return -ENOENT;
    }
    sb.stats[slot].uid = (uid_t)uid;
    sb.stats[slot].gid = (gid_t)gid;

    return 0;
}

static int mail_chmod(const char *path, mode_t mode) {
    int slot = check_data(path);
    if(slot == -1) {
        return -ENOENT;
    }
    sb.stats[slot].mode = (mode_t)mode;

    return 0;
}

static void mail_help() {
	printf("Usage: ./mailFS [options] <mountpoint>\n\n");
}

static void mail_destroy(void *data) {
    printf("\nMailFS shutting down\n");
}

int fetch_folder(char *folder) {
    char UIDs[MAX_DATA_SIZE];
    char header[MAX_DATA_SIZE];
    char data[MAX_DATA_SIZE];
    char path[MAX_NAME_LENGTH];
    char *filename;
    char *uid;
    char *save = NULL;

    if(!request(sb.username, sb.password, folder, 0, -1, UIDs)) return 1;
    uid = strtok_r(UIDs, " ", &save);
    while(uid != NULL) {
        if(!request(sb.username, sb.password, folder, 1, atoi(uid), header)) return 1;
        strcpy(data, header);
        filename = get_filename(header);
        snprintf(path, MAX_NAME_LENGTH, "%s/%s", folder, filename);
        mail_mknod(path, 0, 0);
        if(!request(sb.username, sb.password, folder, 2, atoi(uid), header)) return 1;
        strcat(data, header);
        mail_write(path, data, strlen(data), 0, 0);
        uid = strtok_r(NULL, " ", &save);
    }

    return 0;
}

static void *mail_init() {
    printf("\nMounting MailFS...\n");
    int destroy_flag = 0;

    mail_mkdir(default_folders[0], 0);
    mail_mkdir(default_folders[1], 0);
    destroy_flag = fetch_folder(default_folders[0]);

    if(destroy_flag) {
        printf("\nError retrieving emails!\n");
        mail_destroy(NULL);
    }    

    printf("MailFS mounted\n");
	return NULL;
}

static const struct fuse_operations mail_ops = {
	.init       = mail_init,
    .destroy    = mail_destroy,
	.getattr	= mail_getattr,
	.readdir	= mail_readdir,
	.read		= mail_read,
	.mkdir		= mail_mkdir,
	.rmdir		= mail_rmdir,
	.mknod		= mail_mknod,
	.unlink		= mail_unlink,
	.utimens	= mail_utimens,
	.open		= mail_open,
	.write		= mail_write,
	.chown		= mail_chown,
	.chmod		= mail_chmod,
    .truncate   = mail_truncate,
	.release	= mail_release,
};

int main(int argc, char *argv[]) {
	int ret = 0;

	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1) {
		return 1;
    }

	if (options.mail_help) {
		mail_help();
        return 0;
	}

    FILE *fp = fopen("config", "r");
    fscanf(fp, "%s\n%s", sb.username, sb.password);
    fclose(fp);

	ret = fuse_main(args.argc, args.argv, &mail_ops, NULL);
	fuse_opt_free_args(&args);
	return ret;
}