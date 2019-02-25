#include <stdlib.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define READ_CHUNK_SIZE (1<<20)

extern int  init_randombytes();
extern void cleanup_randombytes();
extern void randombytes(unsigned char* dest, unsigned long long int length);

#ifdef __unix__

static FILE* dev_urandom_fh;

#elif _WIN32

#include <windows.h>
#include <wincrypt.h>

static HCRYPTPROV crypt_provider;

#else

    #error Random number generators available only for Unix and Windows.

#endif

int init_randombytes()
{
#ifdef __unix__
  
    dev_urandom_fh = fopen("/dev/urandom", "r");
    if(0 == dev_urandom_fh)
    {
	return 1;
    }
    
    return 0;
#endif
    
#ifdef _WIN32

    return 0 == CryptAcquireContext(&crypt_provider, NULL, NULL, PROV_RSA_FULL, 0);
    
#endif
}

void cleanup_randombytes()
{
#ifdef __unix__
	if(0 != dev_urandom_fh)
	{
		fclose(dev_urandom_fh);
		dev_urandom_fh = 0;
	}
#endif
}

void randombytes(unsigned char* dest, unsigned long long int length)
{

#ifdef __unix__
	size_t bytes_read;
	unsigned char* write_to;

	if(0 == dev_urandom_fh)
	{
		fprintf(stderr, "randombytes called without initialisation.");
		exit(1);
	}

	write_to = dest;

	while(length > READ_CHUNK_SIZE)
	{
		bytes_read = fread(write_to, 1, READ_CHUNK_SIZE, dev_urandom_fh);
		write_to += bytes_read;
		length   -= bytes_read;
	}

	while(length > 0)
	{
		bytes_read = fread(write_to, 1, length, dev_urandom_fh);
		length -= bytes_read;
	}

#endif

#ifdef _WIN32
	CryptGenRandom(crypt_provider, length, dest);
#endif
	
}

#ifdef __cplusplus
}
#endif
