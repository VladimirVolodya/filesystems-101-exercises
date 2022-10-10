#include <solution.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

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
    char filepath[MAX_PATH_LEN];
    strcpy(path, proc_dir);
    const char* dirs[2] = {map_files_dir, fd_dir};

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

            while ((p_dirent = readdir(p_subdir)) != NULL) {
                if (!strcmp(p_dirent->d_name, point) || !strcmp(p_dirent->d_name, double_point)) {
                    continue;
                }
                buildPath(path, filename, subdir, p_dirent->d_name);
                clearStr(filepath, MAX_PATH_LEN);
                if ((readlink(path, filepath, MAX_PATH_LEN)) == -1) {
                    report_error(path, errno);
                    continue;
                }
                report_file(filepath);
            }

            closedir(p_subdir);
        }
    }

    closedir(p_proc);
}
