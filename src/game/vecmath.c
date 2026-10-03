#include <math.h>
#include <stdlib.h>
#include "vecmath.h"

int Math_SquareRoot12(int x)
{
    if (x <= 0)
        return 0;
    return (int)sqrt((double)x * 4096.0);
}

int Vec_ApproxLen2(const int* v)
{
    int a = abs(v[0]), b = abs(v[1]);
    int big = b, small = b;
    if (a < b)
        small = a;
    else
        big = a;
    if (big >> 1 < small)
    {
        int t = small * 0x6a;
        small = big - small;
        big = (big * 0x4b + t) >> 7;
    }
    return big + (small >> 2) - (small >> 6);
}

int Vec_ApproxLen3(const int* v)
{
    int t[2] = { Vec_ApproxLen2(v), v[2] };
    return Vec_ApproxLen2(t);
}

int Vec_LenSq16(const int* v)
{
    return (int)(((int64_t)v[0] * v[0]) >> 16) + (int)(((int64_t)v[1] * v[1]) >> 16) +
           (int)(((int64_t)v[2] * v[2]) >> 16);
}

int Vec_NormalizeLen(int* v)
{
    int x = v[0], y = v[1], z = v[2];
    int m = abs(x);
    if (abs(y) > m)
        m = abs(y);
    if (abs(z) > m)
        m = abs(z);
    if (m < 0x200)
    {
        x <<= 10;
        y <<= 10;
        z <<= 10;
    }
    else if (m > 0x800000)
    {
        x >>= 10;
        y >>= 10;
        z >>= 10;
    }
    int r = Math_SquareRoot12(Fix_Mul16(x, x) + Fix_Mul16(y, y) + Fix_Mul16(z, z));
    int len = r << 2;
    if (len > 1)
    {
        unsigned inv = 0x80000000u / (unsigned)(len >> 1);
        v[0] = (int)(((int64_t)x * (int)inv) >> 16);
        v[1] = (int)(((int64_t)y * (int)inv) >> 16);
        v[2] = (int)(((int64_t)z * (int)inv) >> 16);
    }
    if (m < 0x200)
        len >>= 10;
    else if (m > 0x800000)
        len = r << 12;
    return len;
}

void Vec_Normalize(int* v)
{
    double x = v[0], y = v[1], z = v[2];
    double l = sqrt(x * x + y * y + z * z);
    if (l <= 0)
        return;
    v[0] = (int)lround(x / l * 65536.0);
    v[1] = (int)lround(y / l * 65536.0);
    v[2] = (int)lround(z / l * 65536.0);
}

void Vec_Scale(int k, int* v)
{
    v[0] = Fix_Mul16(v[0], k);
    v[1] = Fix_Mul16(v[1], k);
    v[2] = Fix_Mul16(v[2], k);
}

void Vec_Cross(const int* a, const int* b, int* out)
{
    int x = Fix_Mul16(b[2], a[1]) - Fix_Mul16(b[1], a[2]);
    int y = Fix_Mul16(b[0], a[2]) - Fix_Mul16(b[2], a[0]);
    int z = Fix_Mul16(b[1], a[0]) - Fix_Mul16(b[0], a[1]);
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

int Vec_Dot16(const int* a, const int* b)
{
    return Fix_Mul16(a[0], b[0]) + Fix_Mul16(a[1], b[1]) + Fix_Mul16(a[2], b[2]);
}

int Math_Atan2(int y, int x)
{
    if (x == 0 && y == 0)
        return 0;
    return (int)lround(atan2((double)y, (double)x) * 2048.0 / 3.14159265358979323846);
}
