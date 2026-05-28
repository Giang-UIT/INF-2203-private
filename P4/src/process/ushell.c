#include <stdio.h>

#include <core/compiler.h>
#include <core/ctype.h>
#include <core/errno.h>
#include <core/macros.h>
#include <core/sprintf.h>
#include <core/string.h>

#include <unistd.h>
#include <sys/types.h>

#include <limits.h>
#include <stdbool.h>

#define SH_NAME      "mush - the munix shell"
#define SH_PREFIX    "mush: "
#define SH_PROMPT    "$ "
#define SH_LINEBUFSZ 256 ///< Size of a line buffer (bytes)
#define SH_ARGVSZ    16  ///< Size of an argument vector (number of args)
#define SH_CMDVSZ    5   ///< Size of a command vector (number of commands)

#define SH_DEBUG_PARSE 0
#define SH_DEBUG_EXEC  1

/**
 * Struct to hold argc and argv for a single parsed command
 */
struct parsed_cmd {
    size_t argc;
    char  *argv[SH_ARGVSZ];
    char   separator;
};

char cwd[PATH_MAX];
int  waiting_for_input;

typedef int shcmd_fn(int argc, char *argv[]);

struct shcmd {
    const char *name;
    shcmd_fn   *fn;
};

static const struct shcmd SH_BUILTINS[];

/* === Utilities === */

#define reporterr(res, fmt, ...) \
    fprintf(stderr, SH_PREFIX "[%s] %s: " fmt, strerror(-res), __func__, \
            ##__VA_ARGS__)

/* === Input === */

/**
 * Read a line of input
 */
static ssize_t sh_line_read(const char *prompt, char *buf, size_t size)
{
    if (prompt) printf("%s", prompt);

    for (;;) {
        /* Read a line of input using the `read` system call.
         * We assume that the OS is doing line buffering for us (TTY mode). */
        ssize_t res = read(0, buf, size - 1);

        if (res == -EAGAIN) continue; // Non-blocking wait for input
        if (res < 0) return res;      // Error
        if (res == 0) return 0;       // End of file

        buf[res] = '\0'; // Add null terminator
        return res;
    }
}

/* === Command Parsing === */

/** Does this character break a command? */
static bool is_cmdsep(char ch)
{
    switch (ch) {
    case '\0': // End of string
    case '\n': // End of line
    case ';':  // Sequence separator
    case '&':  // Background separator
    case '|':  // Pipe separator
        return true;
    default: return false;
    }
}

/** Djes this character break word? */
static bool is_wordsep(char ch) { return isspace(ch) || is_cmdsep(ch); }

/** Parse a string of whitespace characters */
static ssize_t sh_parse_space(char **bufpos)
{
    const char *startpos = *bufpos;
    while (isspace(**bufpos)) ++*bufpos;
    return *bufpos - startpos; // Return length of whitespace.
}

/** Parse a word (non-whitespace characters */
static ssize_t sh_parse_word(char **bufpos)
{
    const char *startpos = *bufpos;
    while (!is_wordsep(**bufpos)) ++*bufpos;
    return *bufpos - startpos; // Return length of word.
}

/** Parse a single shell argument */
static ssize_t sh_parse_arg(char **bufpos)
{
    /* For now, an argument is just a word.
     * If we wanted to handle quoted arguments,
     * this is where we would do that. */
    return sh_parse_word(bufpos);
}

/** Parse a single command (destructive) */
static ssize_t sh_parse_cmd(char **bufpos, struct parsed_cmd *cmd)
{
    *cmd = (struct parsed_cmd){}; // Zero-out command struct.

    /* Read arguments. */
    for (;;) {
        /* Skip whitespace. */
        sh_parse_space(bufpos);

        /* Read next argument. */
        char   *arg    = *bufpos;
        ssize_t arglen = sh_parse_arg(bufpos);

        /* Save next character in case it is a command separator. */
        char nextchar = **bufpos;

        /* Terminate argument string (if not already terminated). */
        if (**bufpos) {
            **bufpos = '\0'; // Write null to end arg string.
            ++*bufpos;       // Consume written null char.
        }

        /* Save the parsed argument (if it is non-zero length). */
        if (arglen > 0) {
            /* Do not exceed the limit for argument count,
             * including an extra slot for a terminating null. */
            if (cmd->argc >= SH_ARGVSZ - 1) return -E2BIG;

            cmd->argv[cmd->argc] = arg; // Save arg pointer.
            cmd->argc++;                // Advance to next argument.
        }

        /* If we encountered a command separotor (or end of string),
         * terminate command and return. */
        if (is_cmdsep(nextchar)) {
            cmd->separator = nextchar;

            if (SH_DEBUG_PARSE) {
                fprintf(stderr, "parsed cmd: ");
                for (size_t j = 0; j < cmd->argc; j++) {
                    if (j) fprintf(stderr, " ");
                    fprintf(stderr, "%s", cmd->argv[j]);
                }
                fprintf(stderr, "\n");
            }

            return cmd->argc;
        }
    }
}

/** Parse multiple commands (destructive) */
static ssize_t
sh_parse_cmds(char **bufpos, struct parsed_cmd cmdv[], size_t cmdvsz)
{
    size_t i = 0;
    while (**bufpos) {
        if (i >= cmdvsz) return -E2BIG;
        ssize_t argc = sh_parse_cmd(bufpos, &cmdv[i]);
        if (argc < 0) return argc;
        i++;
    }
    return i;
}

/* === Shell Builtin Commands === */

static int print_cmds(FILE *f, const struct shcmd cmds[])
{
    int   res;
    char  buf[SH_LINEBUFSZ];
    char *pos = buf, *end = buf + SH_LINEBUFSZ;
    for (const struct shcmd *cmd = cmds; cmd->name && pos < end; cmd++) {
        if (pos != buf) pos += snprintf(pos, BUFREM(pos, end), ", ");
        pos += snprintf(pos, BUFREM(pos, end), "%s", cmd->name);
    }

    res = fprintf(f, SH_PREFIX "built-in commands: %s\n", buf);
    if (res < 0) return res;
    return 0;
}

static int cmd_pwd(int argc, char *argv[])
{
    UNUSED(argc);
    UNUSED(argv);
    printf("%s\n", cwd);
    return 0;
}

#define GREY_ON_BLACK 0x07
#define ED_SCREEN     2

/**
 * Clear screen and reset TTY color via ANSI escape codes
 *
 * @see
 * - <https://en.wikipedia.org/wiki/ANSI_escape_code>
 */
static int cmd_reset(int argc, char *argv[])
{
    UNUSED(argc);
    UNUSED(argv);
    printf("\033[38;5;%dm", GREY_ON_BLACK);
    printf("\033[%dJ", ED_SCREEN);
    return 0;
}

static int cmd_help(int argc, char *argv[])
{
    UNUSED(argc);
    UNUSED(argv);
    return print_cmds(stdout, SH_BUILTINS);
}

static const struct shcmd SH_BUILTINS[] = {
        {"help", cmd_help},
        {"pwd", cmd_pwd},
        {"reset", cmd_reset},
        {},
};

static shcmd_fn *sh_search_builtins(const struct shcmd cmds[], char *arg0)
{
    for (const struct shcmd *cmd = cmds; cmd->fn; cmd++)
        if (!cmd->name || strcmp(arg0, cmd->name) == 0) return cmd->fn;
    return NULL;
}

/* === Command Execution === */

/** Spawn a child process via fork/exec */
__attribute__((unused)) // Not all configurations use this function.
static pid_t
sh_fork_exec(struct parsed_cmd *cmd)
{
    /* Fork child. */
    pid_t child = fork();

    /* Parent: fork returned an error. */
    if (child < 0) {
        reporterr(child, "could not fork\n");
        return child;
    }

    /* Parent: fork returned child PID. */
    if (child > 0) return child;

    /* Child: If we reach this point, thien this is the child. */

    /* Add bin path. */
    char pathbuf[PATH_MAX];
    snprintf(pathbuf, PATH_MAX, "/bin/%s", cmd->argv[0]);

    /* Exec child. */
    int res = execv(pathbuf, (char *const *) cmd->argv);

    /* Exec only returns if there was an error. */
    reporterr(res, "could not exec %s\n", cmd->argv[0]);
    _exit(res);
}

#if __munix__
static pid_t sh_spawn_munix(struct parsed_cmd *cmd)
{
    /* Add bin path. */
    char pathbuf[PATH_MAX];
    snprintf(pathbuf, PATH_MAX, "/bin/%s", cmd->argv[0]);

    return proc_spawn_munix(pathbuf, (char *const *) cmd->argv);
}
#endif

static ssize_t sh_exec_cmds(size_t cmdc, struct parsed_cmd cmdv[])
{
    for (size_t i = 0; i < cmdc; i++) {
        struct parsed_cmd *cmd = cmdv + i;
        if (cmd->argc == 0) continue; // Skip empty commands

        /* Check builtins. */
        shcmd_fn *builtin = sh_search_builtins(SH_BUILTINS, cmd->argv[0]);
        if (builtin) {
            builtin(cmd->argc, cmd->argv);
            continue;
        }

#if __munix__
        /* In Munix, use the custom spawn syscall. */
        pid_t child = sh_spawn_munix(cmd);
#else
        /* For Linux, use fork/exec. */
        pid_t child = sh_fork_exec(cmd);
#endif /* Munix vs Linux */

        switch (cmd->separator) {
        case '|':
        case '&':
            /* Middle of pipeline or background process. Don't wait. */
            break;
        default:
            /* Normal. Wait for child. */
            waitpid(child, NULL, 0);
            break;
        }
    }
    return 0;
}

/* === Main Shell Operation === */

int read_exec_loop(void)
{
    for (;;) {
        /* Read. */
        char    linebuf[SH_LINEBUFSZ];
        ssize_t readct = sh_line_read(SH_PROMPT, linebuf, SH_LINEBUFSZ);

        if (readct < 0) { // Error
            reporterr(readct, "error reading command line\n");
            return readct;
        }
        if (readct == 0) { // End of file
            fprintf(stderr, "(eof)\n");
            return 0;
        }

        /* Parse. */
        char             *bufpos = linebuf;
        struct parsed_cmd cmdv[SH_CMDVSZ];

        ssize_t cmdc = sh_parse_cmds(&bufpos, cmdv, SH_CMDVSZ);
        if (cmdc == 0) continue; // No command
        if (cmdc < 0) {
            reporterr(cmdc, "error parsing command line\n");
            continue;
        }

        /* Exec. */
        sh_exec_cmds(cmdc, cmdv);
    }
}

int main(int argc, char *argv[])
{
    (void) argc, (void) argv; // Unused for now
    if (SH_NAME) printf("%s\n", SH_NAME);
    return read_exec_loop();
}

