//#include <stdio.h>
#include "types.h"
#include "user.h"



char *str = "You can't change a character!";

int main() {
    printf(1, "addr: %x\n", &str[1]);

    str[1] = 'O';

    printf(1, "%s\n", str);
   
    exit();
    return 0;
}
