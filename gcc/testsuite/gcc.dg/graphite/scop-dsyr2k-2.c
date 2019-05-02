/* { dg-require-effective-target size32plus } */
#define NMAX 3000

static double a[NMAX][NMAX], b[NMAX][NMAX], c[NMAX][NMAX];

typedef __INT32_TYPE__ int32_t;
typedef __INT64_TYPE__ int64_t;

void dsyr2k(int64_t N) {
	int32_t i,j,k;

#pragma scop
	for (i=0; i<N; i++) {
		for (j=0; j<N; j++) {
			for (k=j; k<N; k++) {
				c[j][k] += a[i][j] * b[i][k] + b[i][j] * a[i][k];
			}
		}
	}
#pragma endscop
}

/* { dg-final { scan-tree-dump-times "number of SCoPs: 1" 1 "graphite"} } */
