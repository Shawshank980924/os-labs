#include <stdio.h>

int main(){
    unsigned int i = 0x00646c72;
    char *p = (char *)&i;
	printf("H%x Wo%s", 57616, p);
    return 0;
}