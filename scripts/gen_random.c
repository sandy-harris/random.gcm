/*
 * Program to select random numbers for initialising things
 * in the random(4) driver.
 *
 * A different implementation of basically the same idea is
 * one of several kernel security enhancements at
 * https://grsecurity.net/
 *
 * This program:
 *
 *    limits the range of Hamming weights
 *    every byte has at least one bit 1, one 0
 *    different every time it runs
 *
 * data from /dev/urandom
 * results suitable for inclusion by random.c
 * writes to stdout, expecting makefile to redirect
 *
 * makefile should also delete the output file after it is
 * used in compilation of random.c. This is more secure; it
 * forces the file to be rebuilt and a new version used in
 * every compile. It also prevents an enemy just reading an
 * output file in the build directory and getting the data
 * that is in use in the current kernel. This is not full
 * protection since they might look in the kernel image,
 * but it seems to be the best we can do.
 *
 * This falls well short of the ideal initialisation solution,
 * which would give every installation (rather than every
 * compiled kernel) a different seed. For that, see John
 * Denker's suggestions at:
 * http://www.av8n.com/computer/htm/secure-random.htm#sec-boot-image
 *
 * On the other hand, neither sort of seed is necessary if
 *    either  you have a trustworthy hardware RNG
 *    or      you have secure stored data
 * In those cases, the device can easily be initialised well; the
 * only difficulty is to ensure this is done early enough.
 *
 * Inserting random data at compile time can do no harm and may
 * sometimes make attacks harder. It is not an ideal solution, and
 * not always necessary, but cheap and probably the best we can do
 * during the build (rather than install) process.
 *
 * This is certainly done early enough and the data is random
 * enough, but it is not necessarily secret enough.
 *
 * In some cases -- for example, a firewall machine that compiles
 * its own kernel -- this alone might be enough to ensure secure
 * initialisation, since only an enemy who already has root could
 * discover this data. Of course even in those cases it should not
 * be used alone, only as one layer of a defense in depth.
 *
 * In other cases -- a kernel that is compiled once then used in
 * a Linux distro or installed on many devices -- this is likely
 * of very little value. It complicates an attack somewhat, but
 * it clearly will not stop a serious attacker and may not even
 * slow them down much.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <ctype.h>

/*
 * Configuration information
 * moved from random.c
 */
#define INPUT_POOL_SHIFT	12
#define INPUT_POOL_WORDS	(1 << (INPUT_POOL_SHIFT-5))
#define OUTPUT_POOL_SHIFT	10
#define OUTPUT_POOL_WORDS	(1 << (OUTPUT_POOL_SHIFT-5))

#define TOTAL_POOL_WORDS  (INPUT_POOL_WORDS + 2*OUTPUT_POOL_WORDS)

typedef uint32_t u32 ;

int accept(u32) ;
int hamming(u32);
void do_block( int, char * ) ;
void usage(void) ;

int urandom ;

int main(int argc, char **argv)
{
	if( (urandom = open("/dev/urandom", O_RDONLY)) == -1 )  {
		fprintf(stderr, "gen_random_init: no /dev/urandom, cannot continue\n") ;
		exit(1) ;
	}
	printf("/* File generated by gen_random_init.c */\n\n") ;
	/*
	 * print our constants into output file
	 * ensuring random.c has the same values
	 */
	printf("#define INPUT_POOL_WORDS %d\n", INPUT_POOL_WORDS) ; 
	printf("#define OUTPUT_POOL_WORDS %d\n", OUTPUT_POOL_WORDS) ;
	printf("#define INPUT_POOL_SHIFT %d\n\n", INPUT_POOL_SHIFT) ; 
 
	/*
	 * Initialise the pools with random data
	 * This is done unconditionally
	 */
	do_block( TOTAL_POOL_WORDS, "pools" ) ;

#ifdef CONFIG_RANDOM_GCM

#define ARRAY_ROWS	 8			/* 4 pools get 2 constants each    */
#define ARRAY_WORDS	(4 * ARRAY_ROWS)	/* 32-bit words, 128-bit constants */

/*
 * If we are using the GCM hash, set up an array of random
 * constants for it.
 *
 * The choice of 32 words (eight 128-bit rows, 1024 bits) for
 * this is partly arbitrary, partly reasoned. 256 bits would
 * almost certainly be enough, but 1024 is convenient.
 *
 * The AES-GCM hash initialises its accumulator all-zero and uses
 * a 128-bit multiplier, H. I chose instead to use two constants,
 * one to initialise the accumulator and one in the role of H.
 *
 * This requires that a pair of 128-bit constants be used in each
 * output operation. I have four pools and chose to give each pool
 * its own pair instead of using one pair for all pools. I then
 * chose to initialise all eight with random data.
 *
 * Any of those choices might be changed, but all seem reasonable.
 *
 * Add an extra 8 words for a counter used in the hashing
 * 128-bit counter with some extra data for mixing
 */
	printf("#define ARRAY_WORDS %d\n\n", ARRAY_WORDS) ;

	do_block( (ARRAY_WORDS + 8), "constants" ) ;
	printf("static u32 *counter = constants + ARRAY_WORDS ;\n") ;

#endif /* CONFIG_RANDOM_GCM */

	exit(0) ;
}

/*
 * each call outputs one array of nwords 32-bit words
 * with the given array name
 */

#define PER_LINE 8

void do_block( int nwords, char *name )
{
	int nbytes, i, x ;
	u32 *p, *data ;

	nbytes = 4 * nwords ;

	if( (data = calloc( (size_t) nwords, 4)) == NULL )  {
		fprintf(stderr, "gen_random_init: calloc() failed, cannot continue\n") ;
		exit(1) ;
	}

	/* normal case: we have memory */
	x = read( urandom, data, nbytes ) ;
	if( x != nbytes )	{
		fprintf(stderr,"gen_random_int: read() failed, cannot contiue\n") ;
                exit(1) ;
        }

	/*
	 * Replace any array entries that fail criteria
	 *
	 * In theory, the while loop here could run for some
	 * ridiculously long time, or even go infinite
	 * In practice, this is astronomically unlikely
	 * given any sensible definition of accept() and
	 * input that is anywhere near random
	 */ 
	for( i = 0, x = 1, p = data ; x && (i < nwords) ; i++, p++ )	{
		while( !accept(*p) )
			x = read( urandom, (char *) p, 4) ;
        }

        /* output an array of random data */
        printf("static u32 %s[] = {\n", name ) ;
	for( i = 0 ; i < nwords ; i ++ )	{
		printf("0x%08x", data[i]) ;
		if( i == (nwords-1) )
			printf( " } ;\n" ) ;
		else if( (i%PER_LINE) == (PER_LINE-1) )
			printf( ",\n" ) ;
		else	printf( ", " ) ;
	}
	printf("\n") ;
	free( data ) ;
}

void usage()
{
	fprintf(stderr, "usage: gen_random_init\n") ;
	exit(1) ;
}

/*
 * These tests are not strictly necessary
 *
 * We could just use the /dev/urandom output & take what comes
 * Arguably, that would be the best course;
 * "If it ain't broke, don't fix it."
 *
 * Applying any bias here makes output somewhat less random,
 * so easier for an enemy to guess.
 *
 * However, a Hamming weight near 16 gives a chance close
 * to 50/50 that using one of these numbers in arithmetic
 * (+, xor or various forms of multiplication) will change
 * any given bit. This is a highly desirable effect.
 *
 * Compromise: apply some bias, but not a very strong one
 */

#define MIN  8
#define MAX (32-MIN)

int accept( u32 u )
{
        int h, i ;
        char *p ;

        /* reject low or high Hamming weights */
        h = hamming(u) ;
        if( ( h < MIN ) || ( h > MAX ) )
                return(0) ;

        /* at least one 1 and at least one 0 in each byte */
        for( i = 0, p = (char *) &u ; i < 4 ; i++, p++ )        {
                switch(*p)      {
                        case '\0':
                        case '\255':
                                return(0) ;
                        default:
                                break ;
                }
        }
        return(1) ;
}

/*
 * Kernighan's method
 * http://graphics.stanford.edu/~seander/bithacks.html
 */
int hamming( u32 x )
{
        int h ;
        for (h = 0 ; x ; h++)
          x &= (x-1) ; /* clear the least significant bit set */
        return(h) ;
}
