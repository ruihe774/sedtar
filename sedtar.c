#include <archive.h>
#include <archive_entry.h>
#include <linux/limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static const char *program_name;

static void fatal(const char *filename, const char *msg) {
    if (msg) {
        fprintf(stderr, "%s: %s\n", filename ? filename : program_name, msg);
    } else {
        perror(filename ? filename : program_name);
    }
    exit(1);
}

static void fatal_archive(const char *filename, struct archive *a) {
    fatal(filename, archive_error_string(a));
}

static int copy_data(struct archive *aout, struct archive *ain) {
    for (;;) {
        char buf[BUFSIZ];
        int r = archive_read_data(ain, buf, BUFSIZ);
        if (r == 0) {
            return ARCHIVE_OK;
        }
        if (r < ARCHIVE_OK) {
            return r;
        }
        r = archive_write_data(aout, buf, r);
        if (r < ARCHIVE_OK) {
            return -r;
        }
    }
}

int main(int argc, char *argv[], char *envp[]) {
    program_name = argv[0];
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        fprintf(stderr, "usage: %s EXPRESSION [FILE...]\n", program_name);
        return 2;
    }

    pid_t pid;
    int inpipe[2], outpipe[2];
    if (pipe(inpipe) || pipe(outpipe)) {
        fatal(NULL, NULL);
    }
    posix_spawn_file_actions_t file_actions;
    if (posix_spawn_file_actions_init(&file_actions) ||
        posix_spawn_file_actions_adddup2(&file_actions, inpipe[0], STDIN_FILENO) ||
        posix_spawn_file_actions_addclose(&file_actions, inpipe[1]) ||
        posix_spawn_file_actions_adddup2(&file_actions, outpipe[1], STDOUT_FILENO) ||
        posix_spawn_file_actions_addclose(&file_actions, outpipe[0])) {
        fatal(NULL, NULL);
    }
    if (posix_spawn(&pid, "/usr/bin/sed", &file_actions, NULL,
                    (char *[]){"sed", "--sandbox", "--unbuffered", "--null-data", argv[1], NULL}, envp)) {
        fatal(NULL, NULL);
    }
    posix_spawn_file_actions_destroy(&file_actions);
    close(inpipe[0]);
    close(outpipe[1]);

    FILE *sed_w = fdopen(inpipe[1], "w");
    setvbuf(sed_w, NULL, _IONBF, 0);
    FILE *sed_r = fdopen(outpipe[0], "r");

    char out_filename[PATH_MAX];
    out_filename[0] = '\0';
    if (realpath("/dev/stdout", out_filename) == NULL) {
        // do nothing
    }

    struct archive *aout = archive_write_new();
    if (aout == NULL) {
        fatal(NULL, "failed to initialize archive writer");
    }
    if (archive_write_set_format_filter_by_ext_def(aout, out_filename, ".tar") != ARCHIVE_OK) {
        fatal_archive(NULL, aout);
    }
    if (archive_write_open_filename(aout, NULL) != ARCHIVE_OK) {
        fatal_archive(NULL, aout);
    }

    for (int i = 2; i <= argc; ++i) {
        const char *filename = argv[i];
        if (filename == NULL) {
            if (argc == 2) {
                filename = "-";
            } else {
                break;
            }
        }
        if (filename[0] == '-' && filename[1] == '\0') {
            filename = NULL;
        }
        const char *display_filename = filename ? filename : "STDIN";

        struct archive *ain = archive_read_new();
        if (ain == NULL) {
            fatal(display_filename, "failed to initialize archive reader");
        }
        if (archive_read_support_filter_all(ain) != ARCHIVE_OK || archive_read_support_format_all(ain) != ARCHIVE_OK) {
            fatal_archive(display_filename, ain);
        }
        if (archive_read_open_filename(ain, filename, BUFSIZ) != ARCHIVE_OK) {
            fatal_archive(display_filename, ain);
        }

        for (;;) {
            struct archive_entry *entry;
            int r = archive_read_next_header(ain, &entry);
            if (r == ARCHIVE_EOF) {
                break;
            }
            if (r != ARCHIVE_OK) {
                fatal(display_filename, archive_error_string(ain));
            }

            const char *pathname = archive_entry_pathname(entry);
            const size_t len = strlen(pathname) + 1;
            if (fwrite_unlocked(pathname, 1, len, sed_w) != len) {
                fatal(pathname, "sed failed");
            }
            char newpath[PATH_MAX];
            char *p = newpath;
            for (; p < newpath + PATH_MAX; ++p) {
                const int ch = fgetc_unlocked(sed_r);
                if (ch == EOF || ch == '\0') {
                    *p = '\0';
                    break;
                }
                *p = ch;
            }
            if (p == newpath + PATH_MAX) {
                fatal(pathname, "path length limit exceeded after sed");
            }
            if (p == newpath) {
                fprintf(stderr, "%s: empty filename after sed; skipping\n", pathname);
                continue;
            }
            archive_entry_set_pathname(entry, newpath);

            r = archive_write_header(aout, entry);
            if (r != ARCHIVE_OK) {
                fatal_archive(newpath, aout);
            }
            if (archive_entry_size(entry)) {
                r = copy_data(aout, ain);
                if (r < ARCHIVE_OK) {
                    fatal_archive(pathname, ain);
                } else if (r > ARCHIVE_OK) {
                    fatal_archive(newpath, aout);
                }
            }
            if (archive_write_finish_entry(aout) != ARCHIVE_OK) {
                fatal_archive(newpath, aout);
            }
        }

        if (archive_read_free(ain) != ARCHIVE_OK) {
            fatal_archive(display_filename, ain);
        }
    }

    if (archive_write_free(aout) != ARCHIVE_OK) {
        fatal_archive(NULL, aout);
    }

    fclose(sed_w);
    fclose(sed_r);

    int wstatus;
    if (waitpid(pid, &wstatus, 0) == -1) {
        fatal(NULL, NULL);
    }

    return WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 1;
}
