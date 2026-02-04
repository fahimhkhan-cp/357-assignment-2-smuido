#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_INODES 1024
#define NAME_LEN 32

typedef struct {
    int in_use;
    char type; // 'd' or 'f'
} inode_meta_t;

typedef struct {
    uint32_t inode;
    char name[NAME_LEN]; // raw bytes, may not be null-terminated
} dirent_disk_t;

static inode_meta_t inodes[MAX_INODES];
static uint32_t cwd = 0;

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

static int dir_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}

static void write_name_field(char out[NAME_LEN], const char *in) {
    // truncate/pad with '\0'
    memset(out, 0, NAME_LEN);
    if (!in) return;
    strncpy(out, in, NAME_LEN); // if in is >=32, no null guaranteed, that's okay
}

static void print_name_field(const char raw[NAME_LEN]) {
    // print up to first '\0', else all 32
    int n = 0;
    while (n < NAME_LEN && raw[n] != '\0') n++;
    if (n == 0) { printf("(empty)"); return; }
    printf("%.*s", n, raw);
}

static int load_inodes_list(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return -1;

    while (1) {
        uint32_t ino;
        char type;
        size_t r1 = fread(&ino, sizeof(uint32_t), 1, fp);
        if (r1 != 1) break; // EOF or partial
        size_t r2 = fread(&type, sizeof(char), 1, fp);
        if (r2 != 1) break;

        if (ino >= MAX_INODES || (type != 'd' && type != 'f')) {
            fprintf(stderr, "Ignoring invalid inode record: ino=%u type=%c\n", ino, type);
            continue;
        }
        inodes[ino].in_use = 1;
        inodes[ino].type = type;
    }

    fclose(fp);
    return 0;
}

static int save_inodes_list(const char *filename) {
    FILE *fp = fopen(filename, "wb"); // truncate + rewrite
    if (!fp) return -1;

    for (uint32_t i = 0; i < MAX_INODES; i++) {
        if (!inodes[i].in_use) continue;
        uint32_t ino = i;
        char type = inodes[i].type;
        if (fwrite(&ino, sizeof(uint32_t), 1, fp) != 1) { fclose(fp); return -1; }
        if (fwrite(&type, sizeof(char), 1, fp) != 1) { fclose(fp); return -1; }
    }
    fclose(fp);
    return 0;
}

static FILE *open_inode_file(uint32_t ino, const char *mode) {
    char path[64];
    snprintf(path, sizeof(path), "%u", ino);
    return fopen(path, mode);
}

static int find_entry_in_dir(uint32_t dir_ino, const char *name, uint32_t *out_ino) {
    FILE *fp = open_inode_file(dir_ino, "rb");
    if (!fp) return -1;

    dirent_disk_t e;
    char target[NAME_LEN];
    write_name_field(target, name);

    while (fread(&e.inode, sizeof(uint32_t), 1, fp) == 1) {
        if (fread(e.name, NAME_LEN, 1, fp) != 1) break;
        if (memcmp(e.name, target, NAME_LEN) == 0) {
            if (out_ino) *out_ino = e.inode;
            fclose(fp);
            return 1; // found
        }
    }

    fclose(fp);
    return 0; // not found
}

static int append_entry_to_dir(uint32_t dir_ino, uint32_t child_ino, const char *name) {
    FILE *fp = open_inode_file(dir_ino, "ab");
    if (!fp) return -1;

    dirent_disk_t e;
    e.inode = child_ino;
    write_name_field(e.name, name);

    if (fwrite(&e.inode, sizeof(uint32_t), 1, fp) != 1) { fclose(fp); return -1; }
    if (fwrite(e.name, NAME_LEN, 1, fp) != 1) { fclose(fp); return -1; }

    fclose(fp);
    return 0;
}

static int alloc_inode(char type, uint32_t *out_ino) {
    for (uint32_t i = 0; i < MAX_INODES; i++) {
        if (!inodes[i].in_use) {
            inodes[i].in_use = 1;
            inodes[i].type = type;
            *out_ino = i;
            return 0;
        }
    }
    return -1; // no space
}

static void cmd_ls(void) {
    FILE *fp = open_inode_file(cwd, "rb");
    if (!fp) { fprintf(stderr, "ls: cannot open cwd inode file\n"); return; }

    while (1) {
        uint32_t ino;
        char name[NAME_LEN];
        if (fread(&ino, sizeof(uint32_t), 1, fp) != 1) break;
        if (fread(name, NAME_LEN, 1, fp) != 1) break;

        printf("%u ", ino);
        print_name_field(name);
        printf("\n");
    }
    fclose(fp);
}

static void cmd_cd(const char *name) {
    if (!name) { fprintf(stderr, "cd: missing name\n"); return; }

    uint32_t ino;
    int found = find_entry_in_dir(cwd, name, &ino);
    if (found <= 0) { fprintf(stderr, "cd: no such directory\n"); return; }
    if (ino >= MAX_INODES || !inodes[ino].in_use || inodes[ino].type != 'd') {
        fprintf(stderr, "cd: not a directory\n");
        return;
    }
    cwd = ino;
}

static void cmd_mkdir(const char *name) {
    if (!name) { fprintf(stderr, "mkdir: missing name\n"); return; }

    // if exists, error
    int exists = find_entry_in_dir(cwd, name, NULL);
    if (exists == 1) { fprintf(stderr, "mkdir: already exists\n"); return; }
    if (exists < 0) { fprintf(stderr, "mkdir: cannot read directory\n"); return; }

    uint32_t new_ino;
    if (alloc_inode('d', &new_ino) != 0) { fprintf(stderr, "mkdir: no free inodes\n"); return; }

    // create new directory file with . and ..
    FILE *fp = open_inode_file(new_ino, "wb");
    if (!fp) { fprintf(stderr, "mkdir: cannot create inode file\n"); inodes[new_ino].in_use = 0; return; }

    // write "."
    uint32_t dot_ino = new_ino;
    char dot_name[NAME_LEN]; write_name_field(dot_name, ".");
    fwrite(&dot_ino, sizeof(uint32_t), 1, fp);
    fwrite(dot_name, NAME_LEN, 1, fp);

    // write ".."
    uint32_t dotdot_ino = cwd;
    char dotdot_name[NAME_LEN]; write_name_field(dotdot_name, "..");
    fwrite(&dotdot_ino, sizeof(uint32_t), 1, fp);
    fwrite(dotdot_name, NAME_LEN, 1, fp);

    fclose(fp);

    // add entry to parent directory
    if (append_entry_to_dir(cwd, new_ino, name) != 0) {
        fprintf(stderr, "mkdir: failed to update parent dir\n");
        // (Optional cleanup: remove inode file, mark inode free)
    }
}

static void cmd_touch(const char *name) {
    if (!name) { fprintf(stderr, "touch: missing name\n"); return; }

    int exists = find_entry_in_dir(cwd, name, NULL);
    if (exists == 1) return; // do nothing
    if (exists < 0) { fprintf(stderr, "touch: cannot read directory\n"); return; }

    uint32_t new_ino;
    if (alloc_inode('f', &new_ino) != 0) { fprintf(stderr, "touch: no free inodes\n"); return; }

    FILE *fp = open_inode_file(new_ino, "wb");
    if (!fp) { fprintf(stderr, "touch: cannot create inode file\n"); inodes[new_ino].in_use = 0; return; }
    fwrite(name, 1, strlen(name), fp);
    fwrite("\n", 1, 1, fp);
    fclose(fp);

    if (append_entry_to_dir(cwd, new_ino, name) != 0) {
        fprintf(stderr, "touch: failed to update directory\n");
        // (Optional cleanup)
    }
}

static void repl(void) {
    char line[256];

    while (1) {
        printf("> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            // EOF
            break;
        }

        // strip newline
        line[strcspn(line, "\n")] = 0;

        // tokenize
        char *cmd = strtok(line, " \t");
        char *arg = strtok(NULL, " \t");
        char *extra = strtok(NULL, " \t");

        if (!cmd) continue;

        if (strcmp(cmd, "exit") == 0) {
            if (arg || extra) fprintf(stderr, "exit: takes no arguments\n");
            else break;
        } else if (strcmp(cmd, "ls") == 0) {
            if (arg || extra) fprintf(stderr, "ls: takes no arguments\n");
            else cmd_ls();
        } else if (strcmp(cmd, "cd") == 0) {
            if (!arg || extra) fprintf(stderr, "cd: usage: cd <name>\n");
            else cmd_cd(arg);
        } else if (strcmp(cmd, "mkdir") == 0) {
            if (!arg || extra) fprintf(stderr, "mkdir: usage: mkdir <name>\n");
            else cmd_mkdir(arg);
        } else if (strcmp(cmd, "touch") == 0) {
            if (!arg || extra) fprintf(stderr, "touch: usage: touch <name>\n");
            else cmd_touch(arg);
        } else {
            fprintf(stderr, "Unknown command: %s\n", cmd);
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <fs_directory>\n", argv[0]);
        return 1;
    }
    const char *fsdir = argv[1];

    if (!dir_exists(fsdir)) {
        fprintf(stderr, "Error: '%s' is not a directory\n", fsdir);
        return 1;
    }
    if (chdir(fsdir) != 0) die("chdir");

    memset(inodes, 0, sizeof(inodes));
    if (load_inodes_list("inodes_list") != 0) die("open inodes_list");

    // Requirement 3: start at inode 0, must be a directory and in use
    if (0 >= MAX_INODES || !inodes[0].in_use || inodes[0].type != 'd') {
        fprintf(stderr, "Error: inode 0 is not a valid directory\n");
        return 1;
    }
    cwd = 0;

    repl();

    if (save_inodes_list("inodes_list") != 0) die("write inodes_list");
    return 0;
}