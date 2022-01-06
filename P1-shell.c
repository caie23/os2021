#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

void handle_sigstp(int signal) {} // Ctrl+Z -> ignore
void handle_sigint(int signal)
{
    kill(0, SIGCHLD);
}

int getcmd(char *prompt, char *args[], int *background)
{
    int length, i = 0;
    char *token, *loc;
    char *line = NULL;
    size_t linecap = 0;

    signal(SIGTSTP, handle_sigstp); // Ctrl+Z
    signal(SIGINT, handle_sigint);  // Ctrl+C

    printf("%s", prompt);
    length = getline(&line, &linecap, stdin);

    if (length <= 0)
    {
        exit(-1);
    }

    // Check if background is specified..
    if ((loc = index(line, '&')) != NULL)
    {
        *background = 1; // &
        *loc = ' ';
    }
    else
        *background = 0; // no &

    while ((token = strsep(&line, " \t\n")) != NULL)
    {
        for (int j = 0; j < strlen(token); j++)
            if (token[j] <= 32)
                token[j] = '\0';
        if (strlen(token) > 0)
            args[i++] = token;
    }

    return i;
}

int main(void)
{
    char buf[BUFSIZ];
    char *args[20] = {NULL};
    int bg;

    int *jobs = NULL;
    jobs = (int *)malloc(sizeof(int));

    while (1)
    {
        bg = 0;
        int cnt = getcmd("\n>> ", args, &bg);
        args[cnt] = NULL;

        int fd[2];
        pipe(fd);

        int pid = fork();
        if (pid == 0)
        { // child

            if (strcmp(args[0], "exit") == 0)
            {
                kill(0, SIGTERM);
            }

            char *cmd1[10] = {NULL};
            char *cmd2[10] = {NULL};
            int i = 0;
            for (i; i < cnt; i++)
            {
                if (strcmp(args[i], "|") == 0 || strcmp(args[i], ">") == 0 || args[i] == NULL)
                    break;
                else
                    cmd1[i] = args[i];
            }
            char *op = args[i];
            cmd1[i + 1] = NULL;

            if (strcmp(args[0], "cd") == 0)
            {
                chdir(args[1]);
                continue;
            }
            if (strcmp(args[0], "jobs") == 0)
            {
                printf("background processes:\n");
                for (int i = 0; jobs[i] != 0; i++)
                    printf("[%d]%d\n", i + 1, jobs[i]);
                continue;
            }

            if (op == NULL)
            {
                execvp(args[0], args);
                continue;
            }
            else if (strcmp(op, ">") == 0) // output redirection
            {
                printf("cmd1: %s\n", cmd1[0]);
                printf("cmd2: %s\n", args[i + 1]);

                dup2(fd[1], 1);
                close(fd[1]);
                close(fd[0]);
                open(args[i + 1], O_CREAT, 0644);
                execvp(cmd1[0], cmd1);
                close(fd[0]);
                close(fd[1]);

                continue;
            }
            else if (strcmp(op, "|") == 0) // piping
            {
                int k = 0;
                for (i = i + 1; i < cnt; i++)
                {
                    if (args[i] == NULL)
                        break;
                    else
                    {
                        cmd2[k] = args[i];
                        k++;
                    }
                }
                cmd2[k] = NULL;

                int fd[2];
                pipe(fd);
                if (fork() == 0)
                {
                    // child - reading end
                    close(fd[1]);
                    close(0);
                    dup(fd[0]);
                    execvp(cmd2[0], cmd2);
                    close(fd[0]);
                    continue;
                }
                else
                {
                    // parent - writing end
                    close(fd[0]);
                    close(1);
                    dup(fd[1]);
                    execvp(cmd1[0], cmd1);
                    close(fd[1]);
                    continue;
                }
            }
        }
        else
        {
            // parent
            // if (strcmp(args[0], "fg") == 0)
            // {
            //     if (args[1] == NULL)
            //     {
            //         args[1] = "0";
            //         args[2] = NULL;
            //     }

            //     int i = atoi(args[1]);
            //     printf("foreground process [%d]:\n", i);
            //     if (jobs[1] == 0)
            //     {
            //         printf("wrong index\n");
            //         continue;
            //     }

            //     pid_t wid = jobs[i];
            //     waitpid(wid, NULL, 0);

            //     // shift left
            //     while (jobs[i] != 0)
            //     {
            //         jobs[i] = jobs[i + 1];
            //     }
            //     continue;
            // }

            if (bg == 0) // no &
            {
                waitpid(pid, NULL, 0);
                continue;
            }
            else // &
            {
                int i = 0;
                while (jobs[i] != 0)
                    i++;
                jobs[i] = pid;
                continue;
            }
        }
    }
    free(jobs);
}