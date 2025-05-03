#define MAXLINE 1024
#define MAXARGS 128

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <locale.h>

#include <sys/wait.h>
#include <fcntl.h>


typedef struct Node Node;
typedef struct Node {
    char *name;
    char *value;
    Node *next;
} Node;


void loop(char *fileName);
int processLine(char *line);
int subVar(char **arg);
int initRedirection(char *arg);
int processCommand(int argc, char **argv);
int runExit(int argc, char **argv);
void runCd(int argc, char **argv);
void runLs(int argc, char **argv);
int filter(const struct dirent *entry);
int compare(const struct dirent **a, const struct dirent **b);
void runHistory(int argc, char **argv);
void runShellVariables(int argc, char **argv);
void runEnvVariables(int argc, char **argv);