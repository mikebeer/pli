#include <stdio.h>
extern void plstart(int c, char **v);
extern void plstop(int rc);
extern void lib_i(void);
extern int add3(int a, int b, const char *tag);
extern double hypot2(double x, double y);
extern void fillsq(double *v, int n);
int main(int argc, char **argv)
{
    double v[8];
    int i, r;
    plstart(argc, argv);
    lib_i();
    r = add3(40, 2, "from C");
    printf("\nadd3 = %d\n", r);
    printf("hypot2 = %.3f\n", hypot2(3.0, 4.0));
    fillsq(v, 5);
    for (i = 0; i < 5; i++) printf("%.0f ", v[i]);
    printf("\n");
    plstop(0);
    return 0;
}
