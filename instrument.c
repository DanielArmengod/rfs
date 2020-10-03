#include <stdio.h>
#include <stdlib.h>


void __cyg_profile_func_enter( void *func_address, void *call_site )
                                __attribute__ ((no_instrument_function));

void __cyg_profile_func_exit ( void *func_address, void *call_site )
                                __attribute__ ((no_instrument_function));

static FILE *fp;

void __cyg_profile_func_enter( void *this, void *callsite )
{
  /* Function Entry Address */
  fprintf(fp, "E%p\n", (int *)this);
}


void __cyg_profile_func_exit( void *this, void *callsite )
{
  /* Function Exit Address */
  fprintf(fp, "X%p\n", (int *)this);
}



/* Constructor and Destructor Prototypes */

void main_constructor( void )
	__attribute__ ((no_instrument_function, constructor));

void main_destructor( void )
	__attribute__ ((no_instrument_function, destructor));


/* Output trace file pointer */

void main_constructor( void )
{
  fp = fopen( "trace.txt", "w" );
  if (fp == NULL) exit(-1);
}


void main_deconstructor( void )
{
  fclose( fp );
}
