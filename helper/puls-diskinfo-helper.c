/*
 * puls-diskinfo-helper.c
 *
 * Privileged helper for PULS DiskInfo.
 * This small binary runs via pkexec to execute smartctl with root privileges.
 * It validates the device path and runs smartctl, outputting JSON.
 *
 * Copyright (C) 2024 Barın Güzeldemirci
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>

/**
 * is_valid_device:
 * @path: Device node path.
 *
 * Validate that a device path looks like a real block device.
 * Accepts: /dev/sda, /dev/nvme0n1, /dev/hda, /dev/vda, /dev/mmcblk0
 * Rejects everything else to prevent command injection.
 */
static int
is_valid_device (const char *path)
{
    if (path == NULL || strlen (path) < 6)
        return 0;

    if (strncmp (path, "/dev/", 5) != 0)
        return 0;

    const char *name = path + 5;

    for (const char *p = name; *p != '\0'; p++) {
        if (!isalnum ((unsigned char)*p))
            return 0;
    }

    if (strncmp (name, "sd", 2) == 0 ||
        strncmp (name, "nvme", 4) == 0 ||
        strncmp (name, "hd", 2) == 0 ||
        strncmp (name, "vd", 2) == 0 ||
        strncmp (name, "mmcblk", 6) == 0)
        return 1;

    return 0;
}

static void
run_single_command (const char *device, const char *action)
{
    if (!is_valid_device (device)) {
        fprintf (stderr, "Error: Invalid device path: %s\n", device);
        exit (2);
    }

    if (access (device, F_OK) != 0) {
        fprintf (stderr, "Error: Device does not exist: %s\n", device);
        exit (3);
    }

    if (action == NULL) {
        execlp ("smartctl", "smartctl", "-a", "-j", device, (char *)NULL);
    } else if (strcmp (action, "short") == 0) {
        execlp ("smartctl", "smartctl", "-t", "short", device, (char *)NULL);
    } else if (strcmp (action, "long") == 0) {
        execlp ("smartctl", "smartctl", "-t", "long", device, (char *)NULL);
    } else if (strcmp (action, "abort") == 0) {
        execlp ("smartctl", "smartctl", "-X", device, (char *)NULL);
    } else {
        fprintf (stderr, "Error: Invalid action: %s\n", action);
        exit (5);
    }

    perror ("Failed to execute smartctl");
    exit (4);
}

static void
handle_command_persistent (const char *device, const char *action)
{
    if (!is_valid_device (device)) {
        const char *err = "{\n  \"error\": \"Invalid device path\"\n}\n";
        printf ("%zu\n%s", strlen(err), err);
        fflush (stdout);
        return;
    }

    if (access (device, F_OK) != 0) {
        const char *err = "{\n  \"error\": \"Device does not exist\"\n}\n";
        printf ("%zu\n%s", strlen(err), err);
        fflush (stdout);
        return;
    }

    int pfd[2];
    if (pipe (pfd) == -1) {
        const char *err = "{\n  \"error\": \"Failed to create pipe\"\n}\n";
        printf ("%zu\n%s", strlen(err), err);
        fflush (stdout);
        return;
    }

    pid_t pid = fork ();
    if (pid == -1) {
        close (pfd[0]);
        close (pfd[1]);
        const char *err = "{\n  \"error\": \"Failed to fork child process\"\n}\n";
        printf ("%zu\n%s", strlen(err), err);
        fflush (stdout);
        return;
    }

    if (pid == 0) {
        close (pfd[0]);
        dup2 (pfd[1], STDOUT_FILENO);
        dup2 (pfd[1], STDERR_FILENO);
        close (pfd[1]);

        if (action == NULL) {
            execlp ("smartctl", "smartctl", "-a", "-j", device, (char *)NULL);
        } else if (strcmp (action, "short") == 0) {
            execlp ("smartctl", "smartctl", "-t", "short", device, (char *)NULL);
        } else if (strcmp (action, "long") == 0) {
            execlp ("smartctl", "smartctl", "-t", "long", device, (char *)NULL);
        } else if (strcmp (action, "abort") == 0) {
            execlp ("smartctl", "smartctl", "-X", device, (char *)NULL);
        } else {
            fprintf (stderr, "Error: Invalid action: %s\n", action);
            exit (5);
        }
        perror ("execlp");
        exit (4);
    }

    close (pfd[1]);

    char *buf = NULL;
    size_t buf_size = 0;
    size_t total_read = 0;
    char chunk[4096];
    ssize_t n_read;

    while ((n_read = read (pfd[0], chunk, sizeof (chunk))) > 0) {
        char *new_buf = realloc (buf, buf_size + n_read + 1);
        if (!new_buf) {
            free (buf);
            close (pfd[0]);
            const char *err = "{\n  \"error\": \"Out of memory\"\n}\n";
            printf ("%zu\n%s", strlen(err), err);
            fflush (stdout);
            return;
        }
        buf = new_buf;
        memcpy (buf + total_read, chunk, n_read);
        total_read += n_read;
        buf_size += n_read;
    }
    close (pfd[0]);

    if (buf) {
        buf[total_read] = '\0';
    } else {
        buf = strdup ("");
    }

    int status;
    waitpid (pid, &status, 0);

    printf ("%zu\n%s", strlen (buf), buf);
    fflush (stdout);
    free (buf);
}

int
main (int argc, char *argv[])
{
    if (argc >= 2) {
        const char *device = argv[1];
        const char *action = (argc == 3) ? argv[2] : NULL;
        run_single_command (device, action);
        return 0;
    }

    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;

    while ((line_len = getline (&line, &line_cap, stdin)) != -1) {
        while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r' || line[line_len - 1] == ' ')) {
            line[line_len - 1] = '\0';
            line_len--;
        }

        if (line_len == 0)
            continue;

        if (strcmp (line, "QUIT") == 0) {
            break;
        }

        if (strncmp (line, "READ ", 5) == 0) {
            const char *device = line + 5;
            handle_command_persistent (device, NULL);
        } else if (strncmp (line, "TEST ", 5) == 0) {
            char *device = line + 5;
            char *action = strchr (device, ' ');
            if (action) {
                *action = '\0';
                action++;
                handle_command_persistent (device, action);
            } else {
                const char *err = "{\n  \"error\": \"Missing action for TEST command\"\n}\n";
                printf ("%zu\n%s", strlen(err), err);
                fflush (stdout);
            }
        } else {
            const char *err = "{\n  \"error\": \"Unknown command\"\n}\n";
            printf ("%zu\n%s", strlen(err), err);
            fflush (stdout);
        }
    }

    free (line);
    return 0;
}
