#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// node for linked list
typedef struct Node Node;
typedef struct Node {
    char *word;
    Node *next;
} Node;

// linked list
typedef struct Dictionary {
    Node *firstNode;
    int numWords;
} Dictionary;

// letter to represent a letter on the board - must track if it has been used and what char it is
typedef struct BoardLetter {
    char character;
    char used;
} BoardLetter;

// represents a side to one of the boards - an array of that sides characters
typedef struct BoardSide {
    BoardLetter *letters;
    int numLetters;
    int sideNum;
} BoardSide;

// structure for the board
typedef struct Board {
    BoardSide *sides;
    int numSides;
} Board;

/**
 * Load and fill the linked list used for the dictionary
 */
Dictionary readDict(char *fileName) {
    
    // char path[100] = "../";
    // strcat(path, fileName);

    FILE *file = fopen(fileName, "r");
    if (file == NULL) {
        printf("Could not open file for reading\n");
        exit(1);
    }

    Dictionary dict;

    char currentWord[100];
    Node *previous = NULL;
    int numWords = 0;

    while(fgets(currentWord, sizeof(currentWord), file)) {

        numWords++;
        int length = strlen(currentWord);
        char *p = malloc(sizeof(char) * length);
        if (p == NULL) {
            printf("Failed to allocated heap\n");
            exit(1);
        }

        currentWord[length - 1] = '\0';
        strcpy(p, currentWord);

        Node *node = malloc(sizeof(Node));
        if (node == NULL) {
            printf("Failed to allocated heap\n");
            exit(1);
        }

        node->word = p;

        if (previous == NULL) {
            dict.firstNode = node;
        } else {
            previous->next = node;
        }

        previous = node;
        
    }

    fclose(file);

    previous->next = NULL;
    dict.numWords = numWords;
    return dict;

}

/**
 * Load the board into memory
 */
void readBoard(Board *board, char *fileName) {
    // char path[100] = "../tests/tests/";
    // strcat(path, fileName);

    FILE *file = fopen(fileName, "r");
    if (file == NULL) {
        printf("Could not open file for reading\n");
        exit(1);
    }

    char currRow[100];
    int numRows = 0;

    while(fgets(currRow, sizeof(currRow), file)) {
        numRows++;
    }

    if (numRows < 3) {
        printf("Invalid board\n");
        exit(1);
    }

    rewind(file);

    int usedLetters[26];
    for (int i = 0; i < 26; i++) usedLetters[i] = 0;

    BoardSide *sides = malloc(sizeof(BoardSide) * numRows);
    if (sides == NULL) {
        printf("Failed to allocate heap\n");
        exit(1);
    }

    int ind = 0;
    while(fgets(currRow, sizeof(currRow), file)) {
        int rowLength = strlen(currRow) - 1;

        BoardLetter *rowLetters = malloc(rowLength * sizeof(BoardLetter));
        if (rowLetters == NULL) {
            printf("Failed to allocate heap\n");
            exit(1);
        }

        for (int i = 0; i < rowLength; i++) {

            if (usedLetters[currRow[i] % 26] > 0) {
                printf("Invalid board\n");
                exit(1);
            } else {
                usedLetters[currRow[i] % 26] += 1;
            }

            BoardLetter letter;
            letter.character = currRow[i];
            letter.used = '0';
            rowLetters[i] = letter;
        }

        BoardSide side;
        side.letters = rowLetters;
        side.numLetters = rowLength;
        side.sideNum = ind;
        sides[ind] = side;

        ind++;
  
    }
    board->sides = sides;
    board->numSides = numRows;

    fclose(file);

}

/**
 * Check if the the given dictionary contains a word
 */
int containsWord(Dictionary *dictionary, char *word) {
    Node *currentWord = dictionary->firstNode;
    while (currentWord != NULL) {
        if (strcmp(currentWord->word, word) == 0) {
            return 1;
        }
        currentWord = currentWord->next;
    }

    return 0;
}

/**
 * Check if the given board contains a given letter
 */
int boardContainsLetter(Board *board, char letter) {

    for (int s = 0; s < board->numSides; s++) {
        for (int l = 0; l < board->sides[s].numLetters; l++) {
            if (board->sides[s].letters[l].character == letter) {
                board->sides[s].letters[l].used = '1';
                return s;
            }
        }
    }

    return -1;
}

/**
 * get and validate inputs
 */
int validateSolution(Dictionary *dictionary, Board *board) {

    char currentWord[100];
    int prevSide = -1;
    char lastCharOfLastWord = '\0';

    while (fgets(currentWord, sizeof(currentWord), stdin)) {

        int length = strlen(currentWord);
        currentWord[length - 1] = '\0';
        char word[length];
        strcpy(word, currentWord);

        if (!containsWord(dictionary, word)) {
            printf("Word not found in dictionary\n");
            exit(0);
        }

        prevSide = boardContainsLetter(board, word[0]);
        if (prevSide == -1) {
            printf("Used a letter not present on the board\n");
            exit(0);
        }

        for (int w = 0; w < length - 1; w++) {
            
            int currSide = boardContainsLetter(board, word[w]);
            if (currSide == -1) {
                printf("Used a letter not present on the board\n");
                exit(0);
            }

            if (w == 0) {
                if (lastCharOfLastWord != '\0' && word[0] != lastCharOfLastWord) {
                    printf("First letter of word does not match last letter of previous word\n");
                    exit(0);
                }
            } else {
                if (currSide == prevSide) {
                    printf("Same-side letter used consecutively\n");
                    exit(0);
                }
            }

            prevSide = currSide;

        }

        lastCharOfLastWord = word[length - 2];
            
            for (int s = 0; s < board->numSides; s++) {
                for (int l = 0; l < board->sides[s].numLetters; l++) {
                    if (board->sides[s].letters[l].used == '0') {
                        goto continue_while; // skip to next iteration of while loop
                    }
                }
            }
            
            // all letters used
            return 1;

            continue_while:
                continue;
        
    }

    return 0;

}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        printf("Wrong number of args\n");
        exit(1);
    }

    char *fileBoard = argv[1];
    char *fileDict = argv[2];

    Dictionary dict = readDict(fileDict);
    Board board;
    readBoard(&board, fileBoard);

    if (validateSolution(&dict, &board)) {
        printf("Correct\n");
    } else {
        printf("Not all letters used\n");
        exit(0);
    }
    

    return 0;
}
