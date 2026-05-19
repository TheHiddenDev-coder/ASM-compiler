#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    uint16_t x = 0;
    printf("%s", "Enter x: ");
    scanf("%hu", &x);
    printf("%s", "You entered ");
    printf("%hu\n", x);
    return 0;
}
