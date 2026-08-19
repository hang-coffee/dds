/* t23.c - include guard：同一头文件 #include 两次 */
/* expect A = 0x64 */
#include <grd.h>
#include <grd.h>

int main(void) {
    int r;
    r = GRD_VAL;           /* 99 */
    if (GRD_FLAG) r = r + 1;   /* 100 */
    return r;              /* 100 = 0x64 */
}
