#include <solution.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

#define MAX_PATH_LEN 1000

const char* proc_dir = "/proc/";
const char* fd_dir = "/fd/";

int isNumber(char* str) {
    while (*str) {
        if (*str < '0' || *str > '9') {
            return 0;
        }
        ++str;
    }
    return 1;
}

void buildPath(char* const prev_path, const char* const pid, const char* const fd) {
    prev_path[6] = 0;
    strcat(prev_path, pid);
    strcat(prev_path, fd_dir);
    if (fd) {
        strcat(prev_path, fd);
    }
}

void lsof(void)
{
    struct dirent* p_dirent;
    DIR* p_proc;
    DIR* p_fd_dir;
    if ((p_proc = opendir(proc_dir)) == NULL) {
        report_error(proc_dir, errno);
        return;
    }
    char filename[20];
    char path[MAX_PATH_LEN];
    char filepath[MAX_PATH_LEN];
    strcpy(path, proc_dir);

    while ((p_dirent = readdir(p_proc)) != NULL) {
        strcpy(filename, p_dirent->d_name);
        if (!isNumber(filename)) {
            continue;
        }

        buildPath(path, filename, NULL);

        if ((p_fd_dir = opendir(path)) == NULL) {
            report_error(path, errno);
            continue;
        }

        while ((p_dirent = readdir(p_fd_dir)) != NULL) {
            buildPath(path, filename, p_dirent->d_name);
            if ((readlink(path, filepath, MAX_PATH_LEN)) == -1) {
                report_error(path, errno);
                continue;
            }
            report_file(filepath);
        }

        closedir(p_fd_dir);
    }

    closedir(p_proc);
}
