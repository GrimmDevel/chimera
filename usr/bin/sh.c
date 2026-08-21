#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

extern i64 xiu_fork(void);
extern i64 xiu_chdir(const char *path);
extern pid_t waitpid(pid_t pid, int *status, int options);
extern i64 open(const char *path, int flags, int mode);

static void print_prompt(void) {
  char cwd[256];
  const char *user = getenv("USER");
  if (!user || user[0] == '\0') {
    user = (getuid() == 0) ? "root" : "user";
  }

  if (getcwd(cwd, sizeof(cwd)) && cwd[0] != '\0') {
    printf("%s@xiu:%s# ", user, cwd);
  } else {
    printf("%s@xiu:/# ", user);
  }
}

static void sigint_handler(int sig) {
  (void)sig;
  printf("\n");
  print_prompt();
}

static void print_help(void) {
  printf("\nXIU OS Console Shell (xsh)\n");
  printf("Built-in commands:\n");
  printf("  help            Show this help message\n");
  printf("  clear           Clear the screen\n");
  printf("  cd <dir>        Change directory\n");
  printf("  pwd             Print working directory\n");
  printf("  echo <msg>      Print text (supports '> file' redirection)\n");
  printf("  export <K=V>    Set environment variable (e.g. export PATH=...)\n");
  printf("  exit            Exit the shell\n\n");
  printf("Executables are resolved dynamically from $PATH (%s).\n\n",
         getenv("PATH") ? getenv("PATH") : "/bin:/usr/bin");
}

static void execute_single_command(char *cmd) {
  if (!cmd) return;
  while (*cmd == ' ' || *cmd == '\t') cmd++;
  if (*cmd == '\0') return;

  int is_background = 0;
  size_t len = strlen(cmd);
  while (len > 0 && (cmd[len - 1] == ' ' || cmd[len - 1] == '\t')) cmd[--len] = '\0';
  if (len > 0 && cmd[len - 1] == '&') {
    is_background = 1;
    cmd[--len] = '\0';
    while (len > 0 && (cmd[len - 1] == ' ' || cmd[len - 1] == '\t')) cmd[--len] = '\0';
  }
  if (len == 0) return;

  // check for output redirection '>'
  char *redirect_file = NULL;
  char *gt = strchr(cmd, '>');
  if (gt) {
    *gt = '\0';
    redirect_file = gt + 1;
    while (*redirect_file == ' ' || *redirect_file == '\t') redirect_file++;
    char *end = redirect_file + strlen(redirect_file) - 1;
    while (end >= redirect_file && (*end == ' ' || *end == '\t')) *end-- = '\0';
    if (strlen(redirect_file) == 0) redirect_file = NULL;
  }

  // basic command parsing
  char *argv[16];
  int argc = 0;
  char *p = cmd;

  while (*p && argc < 15) {
    while (*p == ' ' || *p == '\t')
      *p++ = '\0';
    if (*p == '\0')
      break;

    argv[argc++] = p;

    while (*p && *p != ' ' && *p != '\t')
      p++;
  }
  argv[argc] = NULL;

  if (argc == 0)
    return;

  // built-ins
  if (strcmp(argv[0], "help") == 0) {
    print_help();
    return;
  } else if (strcmp(argv[0], "clear") == 0) {
    printf("\033[2J\033[H");
    return;
  } else if (strcmp(argv[0], "exit") == 0) {
    exit(0);
  } else if (strcmp(argv[0], "echo") == 0) {
    if (redirect_file) {
      int out_fd = (int)open(redirect_file, 0x0200 | 1, 0644);
      if (out_fd >= 0) {
        for (int i = 1; i < argc; i++) {
          write(out_fd, argv[i], strlen(argv[i]));
          if (i < argc - 1) write(out_fd, " ", 1);
        }
        write(out_fd, "\n", 1);
        close(out_fd);
      } else {
        printf("echo: cannot open '%s' for writing\n", redirect_file);
      }
    } else {
      for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1)
          printf(" ");
      }
      printf("\n");
    }
    return;
  } else if (strcmp(argv[0], "pwd") == 0) {
    char cwd[256];
    if (getcwd(cwd, sizeof(cwd))) {
      printf("%s\n", cwd);
    } else {
      printf("/\n");
    }
  } else if (strcmp(argv[0], "cd") == 0) {
    if (argc > 1) {
      if (xiu_chdir(argv[1]) < 0) {
        printf("cd: no such file or directory: %s\n", argv[1]);
      }
    } else {
      xiu_chdir("/");
    }
    return;
  } else if (strcmp(argv[0], "export") == 0) {
    if (argc > 1) {
      char *eq = strchr(argv[1], '=');
      if (eq) {
        *eq = '\0';
        setenv(argv[1], eq + 1, 1);
      } else {
        const char *val = getenv(argv[1]);
        if (val) setenv(argv[1], val, 1);
      }
    } else {
      extern char **environ;
      if (environ) {
        for (char **e = environ; *e; e++) {
          printf("%s\n", *e);
        }
      }
    }
    return;
  }

  // fork and Exec
  i64 pid = xiu_fork();
  if (pid == 0) {
    signal(SIGINT, SIG_DFL);
    if (redirect_file) {
      int out_fd = (int)open(redirect_file, 0x0200 | 1, 0644);
      if (out_fd >= 0) {
        dup2(out_fd, 1);
        close(out_fd);
      }
    }

    execvp(argv[0], argv);

    printf("xsh: command not found: %s\n", argv[0]);
    exit(1);
  } else if (pid > 0) {
    if (!is_background) {
      int status = 0;
      waitpid((pid_t)pid, &status, 0);
    } else {
      printf("[%lld]\n", (long long)pid);
    }
  } else {
    printf("xsh: fork failed\n");
  }
}

static void handle_command(char *cmd) {
  if (!cmd || strlen(cmd) == 0)
    return;

  char *p = cmd;
  char *start = cmd;
  while (*p) {
    if (*p == ';' || *p == '&') {
      char sep = *p;
      *p = '\0';
      char subcmd[256];
      strncpy(subcmd, start, sizeof(subcmd) - 4);
      subcmd[sizeof(subcmd) - 4] = '\0';
      if (sep == '&') {
        strncat(subcmd, " &", 3);
      }
      execute_single_command(subcmd);
      start = p + 1;
    }
    p++;
  }
  if (*start) {
    execute_single_command(start);
  }
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  char buf[1024];

  signal(SIGINT, sigint_handler);

  printf("XIU Console Shell (xsh)\n");
  printf("Type 'help' for built-in commands.\n\n");

  while (1) {
    print_prompt();

    i64 n = read(0, buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      // strip trailing newlines / carriage returns
      while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = '\0';
      }
      handle_command(buf);
    }
  }

  return 0;
}
