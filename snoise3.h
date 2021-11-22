/*
 * A speed-improved simplex noise algorithm for 2D, 3D and 4D in C.
 * Ported to C by Bram Stolk.
 *
 * Based on SimplexNoise.java from Stefan Guvstavson, which was:
 * Based on example code by Stefan Gustavson (stegu@itn.liu.se).
 *   Optimisations by Peter Eastman (peastman@drizzle.stanford.edu).
 *   Better rank ordering method by Stefan Gustavson in 2012.
 *
 * This code was placed in the public domain by its original author,
 * Stefan Gustavson. You may use it as you see fit, but
 * attribution is appreciated.
 */

#if defined(_MSC_VER) && !defined(__clang__)
#	define __inline__ __inline
#endif

#define sn3_scalar float

#include <math.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct
{
	sn3_scalar x,y,z,w;
} sn3_Grad;

static sn3_Grad sn3_grad3[ 12 ] =
{
	1,1,0,0,	-1,1,0,0,	1,-1,0,0,	-1,-1,0,0,
	1,0,1,0,	-1,0,1,0,	1,0,-1,0,	-1,0,-1,0,
	0,1,1,0,	0,-1,1,0,	0,1,-1,0,	0,-1,-1,0,
};

static int sn3_singletable[] = 
{
  151,160,137,91,90,15,
  131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
  190, 6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,
  88,237,149,56,87,174,20,125,136,171,168, 68,175,74,165,71,134,139,48,27,166,
  77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,
  102,143,54, 65,25,63,161, 1,216,80,73,209,76,132,187,208, 89,18,169,200,196,
  135,130,116,188,159,86,164,100,109,198,173,186, 3,64,52,217,226,250,124,123,
  5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,
  223,183,170,213,119,248,152, 2,44,154,163, 70,221,153,101,155,167, 43,172,9,
  129,22,39,253, 19,98,108,110,79,113,224,232,178,185, 112,104,218,246,97,228,
  251,34,242,193,238,210,144,12,191,179,162,241, 81,51,145,235,249,14,239,107,
  49,192,214, 31,181,199,106,157,184, 84,204,176,115,121,50,45,127, 4,150,254,
  138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

static int* sn3_perm;
static int* sn3_permMod12;


void sn3_sino_init( void )
{
	// To remove the need for index wrapping, double the permutation table length
	sn3_perm      = (int*) malloc( sizeof(int) * 512 );
	sn3_permMod12 = (int*) malloc( sizeof(int) * 512 );
	for( int i=0; i<512; i++ )
	{
		sn3_perm[i] = sn3_singletable[ i & 255 ];
		sn3_permMod12[ i ] = (int) ( sn3_perm[ i ] % 12 );
	}
	// fprintf( stderr, "permutation tables have been set up.\n" );
}

void sn3_sino_exit( void )
{
	free( sn3_perm );
	free( sn3_permMod12 );
	sn3_perm = 0;
	sn3_permMod12 = 0;
}

// Skewing and unskewing factors for 2, 3, and 4 dimensions
#if defined( USEDOUBLES )
#define F2	0.3660254037844386	// 0.5*(Math.sqrt(3.0)-1.0);
#define G2	0.21132486540518713	// (3.0-Math.sqrt(3.0))/6.0;
#define F3	0.3333333333333333	// 1.0/3.0;
#define G3	0.16666666666666666	// 1.0/6.0;
#define F4	0.30901699437494745	// (Math.sqrt(5.0)-1.0)/4.0;
#define G4	0.1381966011250105	// (5.0-Math.sqrt(5.0))/20.0;
#else
#define F2	0.3660254037844386f	// 0.5*(Math.sqrt(3.0)-1.0);
#define G2	0.21132486540518713f	// (3.0-Math.sqrt(3.0))/6.0;
#define F3	0.3333333333333333f	// 1.0/3.0;
#define G3	0.16666666666666666f	// 1.0/6.0;
#define F4	0.30901699437494745f	// (Math.sqrt(5.0)-1.0)/4.0;
#define G4	0.1381966011250105f	// (5.0-Math.sqrt(5.0))/20.0;
#endif


static __inline__ sn3_scalar sn3_dot3( sn3_Grad g, sn3_scalar x, sn3_scalar y, sn3_scalar z )
{
	return g.x*x + g.y*y + g.z*z; 
}

static __inline__ int sn3_fastfloor( sn3_scalar x )
{
	int xi = (int) x;
	return x<xi ? xi-1 : xi;
}

sn3_scalar sn3_sample( sn3_scalar xin, sn3_scalar yin, sn3_scalar zin )
{
    // Skew the input space to determine which simplex cell we're in
    sn3_scalar s = ( xin+yin+zin )*F3; // Very nice and simple skew factor for 3D
    int i = sn3_fastfloor( xin+s );
    int j = sn3_fastfloor( yin+s );
    int k = sn3_fastfloor( zin+s );
    sn3_scalar t = ( i+j+k )*G3;
    sn3_scalar X0 = i-t; // Unskew the cell origin back to (x,y,z) space
    sn3_scalar Y0 = j-t;
    sn3_scalar Z0 = k-t;
    sn3_scalar x0 = xin-X0; // The x,y,z distances from the cell origin
    sn3_scalar y0 = yin-Y0;
    sn3_scalar z0 = zin-Z0;
    // For the 3D case, the simplex shape is a slightly irregular tetrahedron.
    // Determine which simplex we are in.
    int i1, j1, k1; // Offsets for second corner of simplex in (i,j,k) coords
    int i2, j2, k2; // Offsets for third corner of simplex in (i,j,k) coords
    if (x0>=y0)
    {
      if (y0>=z0)
      {
        i1=1; j1=0; k1=0; i2=1; j2=1; k2=0;   // X Y Z order
      }
      else
        if (x0>=z0)
        {
          i1=1; j1=0; k1=0; i2=1; j2=0; k2=1; // X Z Y order
        }
        else
        {
          i1=0; j1=0; k1=1; i2=1; j2=0; k2=1; // Z X Y order
        }
    }
    else
    { // x0<y0
      if (y0<z0)
      {
        i1=0; j1=0; k1=1; i2=0; j2=1; k2=1;    // Z Y X order
      }
      else
        if (x0<z0)
        {
          i1=0; j1=1; k1=0; i2=0; j2=1; k2=1;  // Y Z X order
        }
        else
        {
           i1=0; j1=1; k1=0; i2=1; j2=1; k2=0; // Y X Z order
        }
    }
    // A step of (1,0,0) in (i,j,k) means a step of (1-c,-c,-c) in (x,y,z),
    // a step of (0,1,0) in (i,j,k) means a step of (-c,1-c,-c) in (x,y,z), and
    // a step of (0,0,1) in (i,j,k) means a step of (-c,-c,1-c) in (x,y,z), where
    // c = 1/6.
    sn3_scalar x1 = x0 - i1 + G3; // Offsets for second corner in (x,y,z) coords
    sn3_scalar y1 = y0 - j1 + G3;
    sn3_scalar z1 = z0 - k1 + G3;
    sn3_scalar x2 = x0 - i2 + 2.0f*G3; // Offsets for third corner in (x,y,z) coords
    sn3_scalar y2 = y0 - j2 + 2.0f*G3;
    sn3_scalar z2 = z0 - k2 + 2.0f*G3;
    sn3_scalar x3 = x0 - 1.0f + 3.0f*G3; // Offsets for last corner in (x,y,z) coords
    sn3_scalar y3 = y0 - 1.0f + 3.0f*G3;
    sn3_scalar z3 = z0 - 1.0f + 3.0f*G3;
    // Work out the hashed gradient indices of the four simplex corners
    int ii = i & 255;
    int jj = j & 255;
    int kk = k & 255;
    int gi0 = sn3_permMod12[ii+   sn3_perm[jj+   sn3_perm[kk   ]]];
    int gi1 = sn3_permMod12[ii+i1+sn3_perm[jj+j1+sn3_perm[kk+k1]]];
    int gi2 = sn3_permMod12[ii+i2+sn3_perm[jj+j2+sn3_perm[kk+k2]]];
    int gi3 = sn3_permMod12[ii+1+ sn3_perm[jj+1+ sn3_perm[kk+1 ]]];
    // Calculate the contribution from the four corners
    const sn3_scalar t0 = 0.6f - x0*x0 - y0*y0 - z0*z0;
    const sn3_scalar t1 = 0.6f - x1*x1 - y1*y1 - z1*z1;
    const sn3_scalar t2 = 0.6f - x2*x2 - y2*y2 - z2*z2;
    const sn3_scalar t3 = 0.6f - x3*x3 - y3*y3 - z3*z3;
    const sn3_scalar n0 = t0 < 0 ? 0 : t0*t0*t0*t0 * sn3_dot3(sn3_grad3[gi0], x0, y0, z0);
    const sn3_scalar n1 = t1 < 0 ? 0 : t1*t1*t1*t1 * sn3_dot3(sn3_grad3[gi1], x1, y1, z1);
    const sn3_scalar n2 = t2 < 0 ? 0 : t2*t2*t2*t2 * sn3_dot3(sn3_grad3[gi2], x2, y2, z2);
    const sn3_scalar n3 = t3 < 0 ? 0 : t3*t3*t3*t3 * sn3_dot3(sn3_grad3[gi3], x3, y3, z3);
    // Add contributions from each corner to get the final noise value.
    // The result is scaled to stay just inside [-1,1]
    return 32.0f * ( n0 + n1 + n2 + n3 );
}
