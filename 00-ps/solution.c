#include <solution.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <bits/types/FILE.h>
#include <stdio.h>

#define MAX_SIZE 100
#define MAX_EXE_PATH_LEN 200
#define MAX_READ_SIZE 10000

const char* proc_dir = "/proc/";
const char* cmdline_path = "/cmdline";
const char* exe_path = "/exe";
const char* environ_path = "/environ";

void buildPath(char* path, const char* pid, const char* filename) {
    strcpy(path, proc_dir);
    strcat(path, pid);
    strcat(path, filename);
}

void clear(char* str, int size) {
    for (int i = 0; i < size; ++i) {
        str[i] = 0;
    }
}

void ps(void)
{
    struct dirent* p_dirent;
    int i;
    DIR* p_proc;
    if ((p_proc = opendir(proc_dir)) == NULL) {
        report_error(proc_dir, errno);
        return;
    }
    char filename[20];
    char filepath[MAX_EXE_PATH_LEN];
    char exec_path[MAX_EXE_PATH_LEN];

    char** const argv_buf = malloc(MAX_SIZE * sizeof(char*));
    char** const envp_buf = malloc(MAX_SIZE * sizeof(char*));
    char *argv[MAX_SIZE];
    char *envp[MAX_SIZE];
    for (int j = 0; j < MAX_SIZE; ++j) {
        argv_buf[j] = malloc(MAX_READ_SIZE * sizeof(char));
        envp_buf[j] = malloc(MAX_READ_SIZE * sizeof(char));
    }

    while ((p_dirent = readdir(p_proc)) != NULL) {
        strcpy(filename, p_dirent->d_name);
        char* p_end;
        int pid = (int) strtol(filename, &p_end, 10);
        if (*p_end) {
            continue;
        }

        buildPath(filepath, filename, exe_path);
        clear(exec_path, MAX_EXE_PATH_LEN);
        if (readlink(filepath, exec_path, MAX_EXE_PATH_LEN) == -1) {
            report_error(filepath, errno);
            continue;
        }

        buildPath(filepath, filename, cmdline_path);
        FILE* p_file;
        if ((p_file = fopen(filepath, "r")) == NULL) {
            report_error(filepath, errno);
            continue;
        }
        i = 0;
        while (fscanf(p_file, "%s", argv_buf[i]) != EOF) {
            argv[i] = argv_buf[i];
            ++i;
        }
        argv[i] = NULL;
        fclose(p_file);

        buildPath(filepath, filename, environ_path);
        if ((p_file = fopen(filepath, "r")) == NULL) {
            report_error(filepath, errno);
            continue;
        }
        i = 0;
        while (fgets(envp_buf[i], MAX_READ_SIZE, p_file) != NULL && envp_buf[i][0] != 0) {
            envp[i] = envp_buf[i];
            ++i;
        }
        envp[i] = NULL;
        fclose(p_file);

        report_process(pid, exec_path, argv + 1, envp);
    }

    closedir(p_proc);
    for (int j = 0; j < MAX_SIZE; ++j) {
        free(argv_buf[j]);
        free(envp_buf[j]);
    }
    free(argv_buf);
    free(envp_buf);
}
