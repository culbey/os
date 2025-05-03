#include "wsh.h"


int err = 0;

// will handle all environment variable tings
void runEnvVariables(int argc, char **argv) {

    if (strcmp(argv[0], "export") == 0) { // set the environment variable

        if (argc != 2) {
            fprintf(stderr, "export: invalid number of args\n");
            err = 1;
            return;
        }

        char *assignment = strdup(argv[1]);
        if (assignment == NULL) {
            err = 1;
            return;
        }

        char *equals = strstr(assignment, "=");
        if (equals == NULL) {
            fprintf(stderr, "export: error in strstr\n");
            err = 1;
            free(assignment);
            return;
        }
        
        // break up assignment into assignment: name | equals + 1 = value
        *equals = '\0';

        if (setenv(assignment, equals + 1, 1) != 0) {
            fprintf(stderr, "export: setting env variable failed\n");
            err = 1;
        }

        free(assignment);

    } else if (strcmp(argv[0], "search") == 0) {
        
        char *value = getenv(argv[1]);
        if (value == NULL) return;
        // this malloc/free will be handled with the other args
        argv[1] = strdup(value);
        if (argv[1] == NULL) {
            err = 1;
        }

    } 
}

// handles all local/shell variables
void runShellVariables(int argc, char **argv) {


    // head node for all shell variables
    static Node shellVariables;

    if (strcmp(argv[0], "local") == 0) { // handle variable assignment and reassignment

        
        if (argc != 2) {
            fprintf(stderr, "local: invalid number of args\n");
            err = 1;
            return;
        }


        // duplicate token, so we do not modify original argument
        char *assignment = strdup(argv[1]);
        if (assignment == NULL) {
            err = 1;
            return;
        }

        // check for =
        char *equals = strstr(assignment, "=");
        if (equals == NULL) {
            err = 1;
            free(assignment);
            return;
        }
        *equals = '\0'; // set assignment to variable name and equals + 1


        // assignment/name is alloced as name\0value
        char *value = equals + 1;
        char *name = assignment;

        Node *currNode = &shellVariables;
        while (currNode->next != NULL) {
            currNode = currNode->next;

            if (strcmp(currNode->name, name) == 0) {
                free(currNode->name); // frees both currNode's name and value
                
                currNode->name = name;
                currNode->value = value;

                return;
            
            }
        }

        Node *newNode = malloc(sizeof(Node));
        if (newNode == NULL) {
            err = 1;
            free(assignment);
            return;
        }
        newNode->name = name;
        newNode->value = value;
        newNode->next = NULL;

        currNode->next = newNode;
     

    } else if (strcmp(argv[0], "vars") == 0) { // print out all variables

        if (argc > 1) {
            fprintf(stderr, "vars: invalid number of args\n");
            err = 1;
            return;
        }
        
        Node *currNode = shellVariables.next;
        while (currNode != NULL) {
            printf("%s=%s\n", currNode->name, currNode->value);
            currNode = currNode->next;
        }

    } else if (strcmp(argv[0], "search") == 0) {

        Node *currNode = shellVariables.next;
        while (currNode != NULL) {
            if (strcmp(currNode->name, argv[1]) == 0) {
                argv[1] = currNode->value;
                return;
            }
            currNode = currNode->next;
        }

        
    } else if (strcmp(argv[1], "clean") == 0){ // free memory in heap
        Node *currNode = shellVariables.next;
        Node *next;
        while (currNode != NULL) {
            next = currNode->next;
            free(currNode->name);
            free(currNode);
            currNode = next;
        }
    }
    
     
}

void runHistory(int argc, char **argv) {

    static int listSize = 5;
    static Node historyList;
    

    if (argc == 1) { // history

        
        int ind = 1;
        Node *currNode = historyList.next;
        while (currNode != NULL && ind <= listSize) {
            printf("%d) %s\n", ind, currNode->value);
            currNode = currNode->next;
            ind++;
        }

    } else if (strcmp(argv[1], "set") == 0) {

        if (argc != 3) {
            fprintf(stderr, "history set: wrong number of arguments\n");
            err = 1;
            return;
        }

        int val;
        if ((val = atoi(argv[2])) == 0) {
            fprintf(stderr, "history set: integer conversion failed or set to 0\n");
            err = 1;
            return;
        }
        if (val < 0) {
            fprintf(stderr, "history set: negative value detected\n");
            err = 1;
            return;
        }

    
        if (val < listSize) {
            Node *currNode = historyList.next;
            int ind = 1;
            
            // get the val'th node
            while (currNode != NULL && ind < val) {
                ind++;
                currNode = currNode->next;
            }

            if (currNode == NULL || currNode->next == NULL) return;
            
            // free and delete all extra nodes
            Node *next = currNode->next;
            currNode->next = NULL;
            currNode = next;
            while (currNode != NULL) {
                next = currNode->next;
                free(currNode->value);
                free(currNode);
                currNode = next;
            }
            
        }
        
        listSize = val;

    } else if (strcmp(argv[1], "add") == 0) {

        
        if (argc != 3) return;

        // check to see if last command is the same as the current one
        if(historyList.next != NULL) {
            if (strcmp(argv[2], historyList.next->value) == 0) return;
        }


        char *newCommand = strdup(argv[2]);
        if (newCommand == NULL) {
            err = 1;
            return; 
        }

        Node *newNode = malloc(sizeof(Node));
        if (newNode == NULL) {
            err = 1;
            free(newCommand);
            return;   
        }
        newNode->value = newCommand;


        // put new node in first spot in the linked list
        Node *currNode = historyList.next;
        historyList.next = newNode;
        newNode->next = currNode;
        currNode = newNode;
        int ind = 1;
        while (currNode != NULL && ind < listSize) { // if there are nodes we must delete, we will get the last node
            ind++;
            currNode = currNode->next;
        }

        // if the list is not overflowed
        if (currNode == NULL || currNode->next == NULL) return;

        // delete all extra nodes - must free the string and the node
        Node *next = currNode->next;
        currNode->next = NULL;
        currNode = next;
        if (currNode != NULL) {
            next = currNode->next;
            free(currNode->value);
            free(currNode);
            currNode = next;
        }

        
        
    } else if (strcmp(argv[1], "clean") == 0) {
        Node *currNode = historyList.next;
        Node *next;
        while (currNode != NULL) {
            next = currNode->next;
            free(currNode->value);
            free(currNode);
            currNode = next;
        }
    } else {

        if (argc != 2) {
            fprintf(stderr, "history n: invalid number of args\n");
            return;
        }

        int val;
        if ((val = atoi(argv[1])) == 0) {
            fprintf(stderr, "history n: invalid number or 0 detected\n");
            return;
        }

        
        if (val < 0 || val > listSize) {
            fprintf(stderr, "history n: %d outside range of history\n", val);
            return;
        }

        Node *currNode = historyList.next;
        int ind = 1;
        while (currNode != NULL && ind < val) {
            ind++;
            currNode = currNode->next;
        }
        
        if (ind != val || currNode == NULL) return;

        char *line = strdup(currNode->value);
        if (line == NULL) exit(-1);
        processLine(line);
        free(line);


    }
        

}

int compare(const struct dirent **a, const struct dirent **b) {
        return strcmp((*a)->d_name, (*b)->d_name);
}

int filter(const struct dirent *entry) {
    return entry->d_name[0] != '.';
}

void runLs(int argc, char **argv) {

    if (argc > 1 || argv[1] != NULL) {
        fprintf(stderr, "ls: Invalid number of args\n");
        err = 1;
        return;
    }
    // pointer to array of pointers to directory entries
    struct dirent **nameList;

    int numFiles;

    // sets the value at &nameList to a pointer that is stored in the heap. That pointer is an array of pointers to malloced directory entries.
    // Must free each entry and the pointer to the array
    if ((numFiles = scandir(".", &nameList, filter, compare)) < 0) {
        fprintf(stderr, "ls: scandir failed\n");
        err = 1;
        return;
    }

    for (int i = 0; i < numFiles; i++) {
        printf("%s\n", nameList[i]->d_name);
        free(nameList[i]);  // free each entry
    }

    free(nameList); // free heap pointer to array of pointers

}

void runCd(int argc, char **argv) {
    if (argc != 2 || argv[1] == NULL) {
        fprintf(stderr, "cd: invalid number of args\n");
        err = 1;
        return;
    }

    if (chdir(argv[1]) != 0) {
        fprintf(stderr, "cd: failed for %s\n", argv[1]);
        err = 1;
        return;
    }
}

int runExit(int argc, char **argv) {
    if (argc > 1 || argv[1] != NULL) {
        fprintf(stderr, "exit: invalid number of args\n");
        err = 1;
        return 1;
    }
    return -1;
}


// will return 1 if not a built in command
int processCommand(int argc, char **argv) {
    // argv is argument array malloced in process line ! CANNOT CHANGE POINTER !

    if (strcmp(argv[0], "exit") == 0) {
        return runExit(argc, argv);
    } else if (strcmp(argv[0], "cd") == 0) {
       runCd(argc, argv);
    } else if (strcmp(argv[0], "ls") == 0) {
       runLs(argc, argv);
    } else if (strcmp(argv[0], "local") == 0 || strcmp(argv[0], "vars") == 0) {
       runShellVariables(argc, argv);
    } else if (strcmp(argv[0], "export") == 0) {
       runEnvVariables(argc, argv);
    } else if (strcmp(argv[0], "history") == 0) {
       runHistory(argc, argv);
    } else {
        
        pid_t pid = fork();
        if (pid < 0) {

            fprintf(stderr, "Fork failed\n");
            err = 1;
            return 0;


        } else if (pid == 0) { // child

            // arrange arguments like {arg1, arg2, NULL}
            char *newargs[argc + 1];
            for (int i = 0; i < argc; i++) {
                newargs[i] = argv[i];
            }
            newargs[argc] = NULL;


            // check if there is an executable with the given path
            if (access(newargs[0], X_OK) == 0) {

                execv(newargs[0], newargs); // should not return
                fprintf(stderr, "error excecing\n");
                _exit(-1);
    
            } else { // if the path was not given, we should look in the PATH env variable

                char *value = getenv("PATH");
                if (value == NULL) _exit(-1);

                char *paths = strdup(value);
                if (paths == NULL) _exit(-1);

                // get 1 path at a time where paths are separated like path1:path2
                char *token = strtok(paths, ":");
                while (token != NULL) {

                    // each path should be the path(token)/newargs[0]\n - so we need 2 extra char for / and \0
                    char *path = calloc(strlen(token) + strlen(newargs[0]) + 2, sizeof(char));
                    if (path == NULL) {
                        fprintf(stderr, "malloc fail\n");
                        _exit(-1);
                    }

                    strcat(path, token);
                    strcat(path, "/");
                    strcat(path, newargs[0]);

                
                    if (access(path, X_OK) == 0) { // check if we have an executable with the given path
                        execv(path, newargs);
                        fprintf(stderr, "execv of %s failed\n", path);
                        _exit(-1);
                    } else {
                        token = strtok(NULL, ":");
                    }

                    free(path);
                }

            }


            //fprintf(stderr, "wsh: %s: command not found\n", newargs[0]);

            _exit(-1);


        } else { // in parent

            int status;
            waitpid(pid, &status, 0);
            
            if (WIFEXITED(status)) { 
                if (WEXITSTATUS(status) == 255) { // check if we exited with -1
                    err = -1;
                    return 0;
                } else {
                    return 1;
                }
            }

        }
    }

    // built-in command
    return 0;
}

// will open input, output, and error to specified file
// returns 1 if redirection occured and 0 if no redirection
int initRedirection(char *arg) {
    if (arg == NULL) return 0;

    char *firstOccurance;
    int fdInput = -1;
    int fdOutput = -1;
    int fdError = -1;

    int fd = -1;

    if ((firstOccurance = strstr(arg, "&>>"))) { // append stdout & stderr to blank

        fdOutput = open(firstOccurance + 3, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fdOutput < 0) {
            err = 1;
            return 0;
        }
        fdError = dup(fdOutput);

    } else if ((firstOccurance = strstr(arg, "&>"))) { // overwrite stdout and stderr to blank

        fdOutput = open(firstOccurance + 2, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fdOutput < 0) {
            err = 1;
            return 0;
        }
        fdError = dup(fdOutput);

    } else if ((firstOccurance = strstr(arg, ">>"))) { // append stdout to blank

        fdOutput = open(firstOccurance + 2, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fdOutput < 0) {
            err = 1;
            return 0;
        }

        // check for fd before and ensure it is a valid number
        if (firstOccurance != arg) {
            char num[strlen(arg) + 1];
            int diff = strlen(arg) - strlen(firstOccurance);
            strncpy(num, arg, diff);
            fd = atoi(num);
            if (fd == 0 && (*(firstOccurance - 1) != '0' || diff > 1)) {
                err = 1;
                return 0;
            }
        }

    } else if ((firstOccurance = strstr(arg, ">"))) { // overwrite stdout to blank
        
        fdOutput = open(firstOccurance + 1, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fdOutput < 0) {
            err = 1;
            return 0;
        }

        // check for fd before and ensure it is a valid number
        if (firstOccurance != arg) {
            char num[strlen(arg) + 1];
            int diff = strlen(arg) - strlen(firstOccurance);
            strncpy(num, arg, diff);
            fd = atoi(num);
            if (fd == 0 && (*(firstOccurance - 1) != '0' || diff > 1)) {
                err = 1;
                return 0;
            }
        }

    } else if ((firstOccurance = strstr(arg, "<"))) { // redirect blank to stdin

        fdInput = open(firstOccurance + 1, O_RDONLY);
        if (fdInput < 0) {
            err = 1;
            return 0;
        }

        // check for fd before and ensure it is a valid number
        if (firstOccurance != arg) {
            char num[strlen(arg) + 1];
            int diff = strlen(arg) - strlen(firstOccurance);
            strncpy(num, arg, diff);
            fd = atoi(num);
            if (fd == 0 && (*(firstOccurance - 1) != '0' || diff > 1)) {
                err = 1;
                return 0;
            }
        }

    }

    int redirection = 0;
    if (fdInput != -1) {
        dup2(fdInput, ((fd == -1) ? STDIN_FILENO : fd));
        close(fdInput);
        redirection = 1;
    }

    if (fdOutput != -1) {
        dup2(fdOutput, ((fd == -1) ? STDOUT_FILENO : fd));
        close(fdOutput);
        redirection = 1;
    }

    if (fdError != -1) {
        dup2(fdError, STDERR_FILENO);
        close(fdError);
        redirection = 1;
    }

    return redirection;

}



// takes in command as an input
// outputs 1 if command is not a built-in and should be saved to history
int processLine(char *line) {


    char *token = strtok(line, " \n");
    if (token == NULL) return 0;    // if the command is empty
    if (token[0] == '#') return 0;  // the line is a comment

    // create the argument array
    char **args = calloc(MAXARGS, sizeof(char*));
    if (args == NULL) exit(-1);

    // init number of args to 0
    int argInd = 0;


    while (token != NULL && argInd < MAXARGS) {

        // check each token for variables to substitute
        char *dollarSign = strstr(token, "$"); // line: \0token\0
        if (dollarSign != NULL) { 
            // variable to substitute was found
            
            // X$Z -> X\0Z
            *dollarSign = '\0'; // now token holds before $
        
            char *varName = dollarSign + 1;
            char *searchArgs[2] = {"search", varName};

            
            runEnvVariables(2, searchArgs);

            // check if env variables found the value, if not check shell variables
            if (searchArgs[1] == varName) {
                runShellVariables(2, searchArgs);
            }
            

            // variable was not found, so we will not add this token
            if (searchArgs[1] == dollarSign + 1) continue;       
            
        
            int length = strlen(token) + strlen(searchArgs[1]) + 1;
            char *subbedToken = calloc(length, sizeof(char)); // allocate for before, after, and null terminator
            if (subbedToken == NULL) return 0;

            // concat before, to after, and ensure null terminator
            strcpy(subbedToken, token);
            strcat(subbedToken, searchArgs[1]);
            subbedToken[length - 1] = '\0';

            args[argInd] = subbedToken;

        } else { // No substitution needed

            // duplicate the token and put it in the args
            args[argInd] = strdup(token);
            if (args[argInd] == NULL) return 0;
        }


        argInd++;
        token = strtok(NULL, " \n");
    }

    // check if only argument was invalid variable
    if (argInd == 0) return 0;

    // check if final argument was redirection
    if (initRedirection(args[argInd - 1])) {
        // if redirection was found, remove this argument
        argInd--;
        free(args[argInd]);
        args[argInd] = NULL;
    } 


    // handle the command with given argc and argv, returns 0 if a built in command
    int r = processCommand(argInd, args);

    for (int i = 0; i < MAXARGS; i++) {
        if (args[i] != NULL) {
            free(args[i]);
            args[i] = NULL;
        }
    }
    free(args);
    args = NULL;
    return r;

   
}

void loop(char *fileName) {

    // save copies of the default stdin, out, err
    int std_in = dup(fileno(stdin));
    int std_out = dup(fileno(stdout));
    int std_err = dup(fileno(stderr));


    char *line = calloc(MAXLINE, sizeof(char));
    if(line == NULL) exit(-1);

    // used to store full line after line is modified with strtok
    char *fullLine = calloc(MAXLINE, sizeof(char));
    if (fullLine == NULL) exit(-1);

    // if we are running in batch mode, open the file
    FILE *file = NULL;
    if (fileName != NULL) {
        file = fopen(fileName, "r");
        if (file == NULL) exit(-1);
    }

    // init the PATH env variable
    if (setenv("PATH", "/bin", 1) != 0) {
        fprintf(stderr, "failed initializing path env variable\n");
        err = 1;
    }

    
    while(1) {


        if (file == NULL) {
            printf("wsh> ");
            fflush(stdout);
        }
        
        // get the next command, from file or stdin. If end of file, break the loop
        if (fgets(line, MAXLINE, ((file == NULL) ? stdin : file)) == NULL) break;
        
        
        line[strcspn(line, "\n")] = '\0'; // replace newline character with null terminator
        if (strlen(line) == 0) continue;

        // store copy of line to save to history because strtok will add \0 to line
        strcpy(fullLine, line);

        int r = processLine(line);
        if (r == 1) { // if the command is not a built in
            char *args[] = {"history", "add", fullLine};
            runHistory(3, args);
        }

        // ensure all text written to the current stdout and stderr are flushed before restore
        fflush(stdout);
        fflush(stderr);

        // restore all file descriptors back to defaults
        dup2(std_in, STDIN_FILENO);
        dup2(std_out, STDOUT_FILENO);
        dup2(std_err, STDERR_FILENO);
        if (r == -1) break;
    }
    
    // close duplicate file descriptors on close
    close(std_in);
    close(std_out);
    close(std_err);

    char *cleanArgs[2] = {"exit", "clean"};
    runHistory(0, cleanArgs);
    runShellVariables(0, cleanArgs);
    
    if (file != NULL) fclose(file);
    free(line);
    free(fullLine);
    exit(err);
}


int main(int argc, char **argv) {

    if (argc > 1) {
        if (argc > 2) exit(-1);
        loop(argv[1]);
    } else {
        loop(NULL);
    }

   
}
