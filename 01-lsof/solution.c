#include <solution.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define MAX_FILES_OPEN 10000
#define MAX_PATH_LEN 2000

const char* proc_dir = "/proc/";
const char* fd_dir = "/fd/";
const char* map_files_dir = "/map_files/";
const char* point = ".";
const char* double_point = "..";

int isNumber(char* str) {
    while (*str) {
        if (*str < '0' || *str > '9') {
            return 0;
        }
        ++str;
    }
    return 1;
}

void clearStr(char* str, int size) {
    for (int i = 0; i < size; ++i) {
        str[i] = '\0';
    }
}

int cmp(const void* first_str, const void* second_str) {
    const char* first_str_c = *(char**) first_str;
    const char* second_str_c = *(char**) second_str;
    return strcmp(first_str_c, second_str_c);
}

void reportMapFiles(char** strs, int size) {
    qsort(strs, size, sizeof(char*), cmp);
    for (int j = 0; j < size - 1; ++j) {
        if (strcmp(strs[j], strs[j + 1])) {
            report_file(strs[j]);
        }
    }
    if (size == 1) {
        report_file(strs[0]);
    } else if (size > 1 && !strcmp(strs[size - 1], strs[size - 2])) {
        report_file(strs[size - 1]);
    }
}

void reportFds(char** strs, int size) {
    for (int i = 0; i < size; ++i) {
        report_file(strs[i]);
    }
}

void buildPath(char* const prev_path, const char* const pid, const char* subdir, const char* const fd) {
    prev_path[6] = 0;
    strcat(prev_path, pid);
    strcat(prev_path, subdir);
    if (fd) {
        strcat(prev_path, fd);
    }
}

void lsof(void)
{
    struct dirent* p_dirent;
    DIR* p_proc;
    DIR* p_subdir;
    if ((p_proc = opendir(proc_dir)) == NULL) {
        report_error(proc_dir, errno);
        return;
    }
    char filename[20];
    char path[MAX_PATH_LEN];
    char* path_bufs[MAX_FILES_OPEN];
    for (int i = 0; i < MAX_FILES_OPEN; ++i) {
        path_bufs[i] = (char*) malloc(MAX_PATH_LEN * sizeof(char));
    }
    int idx;
    strcpy(path, proc_dir);
    const char* dirs[2] = {map_files_dir, fd_dir};
    void (*reporters[2])(char**, int) = {reportMapFiles, reportFds};

    while ((p_dirent = readdir(p_proc)) != NULL) {
        strcpy(filename, p_dirent->d_name);
        if (!isNumber(filename)) {
            continue;
        }

        for (int i = 0; i < 2; ++i) {
            const char* subdir = dirs[i];
            buildPath(path, filename, subdir, NULL);

            if ((p_subdir = opendir(path)) == NULL) {
                report_error(path, errno);
                continue;
            }

            idx = 0;
            while ((p_dirent = readdir(p_subdir)) != NULL) {
                if (!strcmp(p_dirent->d_name, point) || !strcmp(p_dirent->d_name, double_point)) {
                    continue;
                }
                buildPath(path, filename, subdir, p_dirent->d_name);
                clearStr(path_bufs[idx], MAX_PATH_LEN);
                long res = readlink(path, path_bufs[idx], MAX_PATH_LEN);
                if (res == -1) {
                    report_error(path, errno);
                    continue;
                } else {
                    path[res] = '\0';
                }
                ++idx;
            }
            reporters[i](path_bufs, idx);
            closedir(p_subdir);
        }
    }

    closedir(p_proc);
    for (int i = 0; i < MAX_FILES_OPEN; ++i) {
        free(path_bufs[i]);
    }
}
