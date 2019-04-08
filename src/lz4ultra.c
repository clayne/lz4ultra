/*
 * main.c - command line optimal compression utility for the lz4 format
 *
 * Copyright (C) 2019 Emmanuel Marty
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <sys/timeb.h>
#else
#include <sys/time.h>
#endif
#include "format.h"
#include "shrink.h"
#include "expand.h"

#define BLOCK_SIZE 65536
#define OPT_VERBOSE 1
#define OPT_RAW     2

/*---------------------------------------------------------------------------*/

static long long lz4ultra_get_time() {
   long long nTime;

#ifdef _WIN32
   struct _timeb tb;
   _ftime(&tb);

   nTime = ((long long)tb.time * 1000LL + (long long)tb.millitm) * 1000LL;
#else
   struct timeval tm;
   gettimeofday(&tm, NULL);

   nTime = (long long)tm.tv_sec * 1000000LL + (long long)tm.tv_usec;
#endif
   return nTime;
}

/*---------------------------------------------------------------------------*/

static int lz4ultra_compress(const char *pszInFilename, const char *pszOutFilename, const unsigned int nOptions) {
   FILE *f_in, *f_out;
   unsigned char *pInData, *pOutData;
   lsza_compressor compressor;
   long long nStartTime = 0LL, nEndTime = 0LL;
   long long nOriginalSize = 0LL, nCompressedSize = 0LL;
   int nResult;
   bool bError = false;

   f_in = fopen(pszInFilename, "rb");
   if (!f_in) {
      fprintf(stderr, "error opening '%s' for reading\n", pszInFilename);
      return 100;
   }

   f_out = fopen(pszOutFilename, "wb");
   if (!f_out) {
      fprintf(stderr, "error opening '%s' for writing\n", pszOutFilename);
      return 100;
   }

   pInData = (unsigned char*)malloc(BLOCK_SIZE * 2);
   if (!pInData) {
      fclose(f_out);
      f_out = NULL;

      fclose(f_in);
      f_in = NULL;

      fprintf(stderr, "out of memory\n");
      return 100;
   }
   memset(pInData, 0, BLOCK_SIZE * 2);

   pOutData = (unsigned char*)malloc(BLOCK_SIZE);
   if (!pOutData) {
      free(pInData);
      pInData = NULL;

      fclose(f_out);
      f_out = NULL;

      fclose(f_in);
      f_in = NULL;

      fprintf(stderr, "out of memory\n");
      return 100;
   }
   memset(pInData, 0, BLOCK_SIZE);

   nResult = lz4ultra_compressor_init(&compressor, BLOCK_SIZE * 2);
   if (nResult != 0) {
      free(pOutData);
      pOutData = NULL;

      free(pInData);
      pInData = NULL;

      fclose(f_out);
      f_out = NULL;

      fclose(f_in);
      f_in = NULL;

      fprintf(stderr, "error initializing compressor\n");
      return 100;
   }

   if ((nOptions & OPT_RAW) == 0) {
      unsigned char cHeader[7];

      cHeader[0] = 0x04;                              /* Magic number: 0x184D2204 */
      cHeader[1] = 0x22;
      cHeader[2] = 0x4D;
      cHeader[3] = 0x18;

      cHeader[4] = 0b01000000;                        /* Version.Hi Version.Lo !B.Indep B.Checksum Content.Size Content.Checksum Reserved.Hi Reserved.Lo */
      cHeader[5] = 4 << 4;                            /* Block MaxSize */
      cHeader[6] = 0xc0;                              /* Header checksum for xxHash32 */

      bError = fwrite(cHeader, 1, 7, f_out) != 7;
      nCompressedSize += 7LL;
   }

   if (nOptions & OPT_VERBOSE) {
      nStartTime = lz4ultra_get_time();
   }

   int nPreviousBlockSize = 0;

   while (!feof(f_in) && !bError) {
      int nInDataSize;

      if (nPreviousBlockSize) {
         memcpy(pInData, pInData + BLOCK_SIZE, nPreviousBlockSize);
      }

      nInDataSize = (int)fread(pInData + BLOCK_SIZE, 1, BLOCK_SIZE, f_in);
      if (nInDataSize > 0) {
         if (nPreviousBlockSize && (nOptions & OPT_RAW) != 0) {
            fprintf(stderr, "error: raw blocks can only be used with files <= 64 Kb\n");
            bError = true;
            break;
         }

         int nOutDataSize;

         nOutDataSize = lz4ultra_shrink_block(&compressor, pInData + BLOCK_SIZE - nPreviousBlockSize, nPreviousBlockSize, nInDataSize, pOutData, (nInDataSize >= BLOCK_SIZE) ? BLOCK_SIZE : nInDataSize);
         if (nOutDataSize >= 0) {
            /* Write compressed block */

            if ((nOptions & OPT_RAW) == 0) {
               unsigned char cBlockSize[4];

               cBlockSize[0] = nOutDataSize & 0xff;
               cBlockSize[1] = (nOutDataSize >> 8) & 0xff;
               cBlockSize[2] = (nOutDataSize >> 16) & 0xff;
               cBlockSize[3] = (nOutDataSize >> 24) & 0x7f;

               if (fwrite(cBlockSize, 1, 4, f_out) != (size_t)4) {
                  bError = true;
               }
            }

            if (!bError) {
               if (fwrite(pOutData, 1, (size_t)nOutDataSize, f_out) != (size_t)nOutDataSize) {
                  bError = true;
               }
               else {
                  nOriginalSize += (long long)nInDataSize;
                  nCompressedSize += 3LL + (long long)nOutDataSize;
               }
            }
         }
         else {
            /* Write uncompressible, literal block */

            if ((nOptions & OPT_RAW) != 0) {
               fprintf(stderr, "error: data is incompressible, raw blocks only support compressed data\n");
               bError = true;
               break;
            }

            unsigned char cBlockSize[4];

            cBlockSize[0] = nInDataSize & 0xff;
            cBlockSize[1] = (nInDataSize >> 8) & 0xff;
            cBlockSize[2] = (nInDataSize >> 16) & 0xff;
            cBlockSize[3] = ((nInDataSize >> 24) & 0x7f) | 0x80;   /* Uncompressed block */

            if (fwrite(cBlockSize, 1, 4, f_out) != (size_t)4) {
               bError = true;
            }
            else {
               if (fwrite(pInData + BLOCK_SIZE, 1, (size_t)nInDataSize, f_out) != (size_t)nInDataSize) {
                  bError = true;
               }
               else {
                  nOriginalSize += (long long)nInDataSize;
                  nCompressedSize += 3LL + (long long)nInDataSize;
               }
            }
         }

         nPreviousBlockSize = nInDataSize;
      }

      if (!bError && !feof(f_in) && nOriginalSize >= 1024 * 1024) {
         fprintf(stdout, "\r%lld => %lld (%g %%)", nOriginalSize, nCompressedSize, (double)(nCompressedSize * 100.0 / nOriginalSize));
      }
   }

   unsigned char cFooter[4];
   int nFooterSize;

   if ((nOptions & OPT_RAW) != 0) {
      cFooter[0] = 0x00;         /* EOD marker for raw block */
      cFooter[1] = 0x00;         
      nFooterSize = 2;
   }
   else {
      cFooter[0] = 0x00;         /* EOD frame */
      cFooter[1] = 0x00;
      cFooter[2] = 0x00;
      cFooter[3] = 0x00;
      nFooterSize = 4;
   }

   if (!bError)
      bError = fwrite(cFooter, 1, nFooterSize, f_out) != nFooterSize;
   nCompressedSize += (long long)nFooterSize;

   if (!bError && (nOptions & OPT_VERBOSE)) {
      nEndTime = lz4ultra_get_time();

      double fDelta = ((double)(nEndTime - nStartTime)) / 1000000.0;
      double fSpeed = ((double)nOriginalSize / 1048576.0) / fDelta;
      int nCommands = lz4ultra_compressor_get_command_count(&compressor);
      fprintf(stdout, "\rCompressed '%s' in %g seconds, %.02g Mb/s, %d tokens (%lld bytes/token), %lld into %lld bytes ==> %g %%\n",
         pszInFilename, fDelta, fSpeed, nCommands, nOriginalSize / ((long long)nCommands),
         nOriginalSize, nCompressedSize, (double)(nCompressedSize * 100.0 / nOriginalSize));
   }

   lz4ultra_compressor_destroy(&compressor);

   free(pOutData);
   pOutData = NULL;

   free(pInData);
   pInData = NULL;

   fclose(f_out);
   f_out = NULL;

   fclose(f_in);
   f_in = NULL;

   if (bError) {
      fprintf(stderr, "\rcompression error for '%s'\n", pszInFilename);
      return 100;
   }
   else {
      return 0;
   }
}

/*---------------------------------------------------------------------------*/

static int lz4ultra_decompress(const char *pszInFilename, const char *pszOutFilename, const unsigned int nOptions) {
   long long nStartTime = 0LL, nEndTime = 0LL;
   long long nOriginalSize = 0LL;
   unsigned int nFileSize = 0;

   FILE *pInFile = fopen(pszInFilename, "rb");
   if (!pInFile) {
      fprintf(stderr, "error opening input file\n");
      return 100;
   }

   if ((nOptions & OPT_RAW) == 0) {
      unsigned char cHeader[7];

      memset(cHeader, 0, 7);

      if (fread(cHeader, 1, 7, pInFile) != 7) {
         fclose(pInFile);
         pInFile = NULL;
         fprintf(stderr, "error reading header in input file\n");
         return 100;
      }

      if (cHeader[0] != 0x04 ||
         cHeader[1] != 0x22 ||
         cHeader[2] != 0x4D ||
         cHeader[3] != 0x18 ||
         cHeader[4] != 0b01000000 ||
         cHeader[5] != (4 << 4) ||
         cHeader[6] != 0xc0) {
         fclose(pInFile);
         pInFile = NULL;
         fprintf(stderr, "invalid magic number, version, flags, block size or checksum in input file\n");
         return 100;
      }
   }
   else {
      fseek(pInFile, 0, SEEK_END);
      nFileSize = (unsigned int)ftell(pInFile);
      fseek(pInFile, 0, SEEK_SET);

      if (nFileSize < 2) {
         fclose(pInFile);
         pInFile = NULL;
         fprintf(stderr, "invalid file size for raw block mode\n");
         return 100;
      }
   }

   FILE *pOutFile = fopen(pszOutFilename, "wb");
   if (!pOutFile) {
      fclose(pInFile);
      pInFile = NULL;
      fprintf(stderr, "error opening output file\n");
      return 100;
   }

   unsigned char *pInBlock;
   unsigned char *pOutData;

   pInBlock = (unsigned char*)malloc(BLOCK_SIZE);
   if (!pInBlock) {
      fclose(pOutFile);
      pOutFile = NULL;

      fclose(pInFile);
      pInFile = NULL;
      fprintf(stderr, "error opening output file\n");
      return 100;
   }

   pOutData = (unsigned char*)malloc(BLOCK_SIZE * 2);
   if (!pOutData) {
      free(pInBlock);
      pInBlock = NULL;

      fclose(pOutFile);
      pOutFile = NULL;

      fclose(pInFile);
      pInFile = NULL;
      fprintf(stderr, "error opening output file\n");
      return 100;
   }

   if (nOptions & OPT_VERBOSE) {
      nStartTime = lz4ultra_get_time();
   }

   int nDecompressionError = 0;
   int nPrevDecompressedSize = 0;

   while (!feof(pInFile) && !nDecompressionError) {
      unsigned char cBlockSize[4];
      unsigned int nBlockSize = 0;

      if (nPrevDecompressedSize != 0) {
         memcpy(pOutData + BLOCK_SIZE - nPrevDecompressedSize, pOutData + BLOCK_SIZE, nPrevDecompressedSize);
      }

      if ((nOptions & OPT_RAW) == 0) {
         if (fread(cBlockSize, 1, 4, pInFile) == 4) {
            nBlockSize = ((unsigned int)cBlockSize[0]) |
               (((unsigned int)cBlockSize[1]) << 8) |
               (((unsigned int)cBlockSize[2]) << 16) |
               (((unsigned int)cBlockSize[3]) << 24);
         }
         else {
            nBlockSize = 0;
         }
      }
      else {
         nBlockSize = nFileSize - 2;
         nFileSize = 0;
      }

      if (nBlockSize != 0) {
         bool bIsUncompressed = (nBlockSize & 0x80000000) != 0;
         int nDecompressedSize = 0;

         nBlockSize &= 0x7fffffff;
         if (fread(pInBlock, 1, nBlockSize, pInFile) == nBlockSize) {
            if (bIsUncompressed) {
               memcpy(pOutData + BLOCK_SIZE, pInBlock, nBlockSize);
               nDecompressedSize = nBlockSize;
            }
            else {
               unsigned int nBlockOffs = 0;

               nDecompressedSize = lz4ultra_expand_block(pInBlock, nBlockSize, pOutData, BLOCK_SIZE, BLOCK_SIZE);
               if (nDecompressedSize < 0) {
                  nDecompressionError = nDecompressedSize;
                  break;
               }
            }

            if (nDecompressedSize != 0) {
               nOriginalSize += (long long)nDecompressedSize;

               fwrite(pOutData + BLOCK_SIZE, 1, nDecompressedSize, pOutFile);
               nPrevDecompressedSize = nDecompressedSize;
               nDecompressedSize = 0;
            }
         }
         else {
            break;
         }
      }
      else {
         break;
      }
   }

   free(pOutData);
   pOutData = NULL;

   free(pInBlock);
   pInBlock = NULL;

   fclose(pOutFile);
   pOutFile = NULL;

   fclose(pInFile);
   pInFile = NULL;

   if (nDecompressionError) {
      fprintf(stderr, "decompression error for '%s'\n", pszInFilename);
      return 100;
   }
   else {
      if (nOptions & OPT_VERBOSE) {
         nEndTime = lz4ultra_get_time();
         double fDelta = ((double)(nEndTime - nStartTime)) / 1000000.0;
         double fSpeed = ((double)nOriginalSize / 1048576.0) / fDelta;
         fprintf(stdout, "Decompressed '%s' in %g seconds, %g Mb/s\n",
            pszInFilename, fDelta, fSpeed);
      }

      return 0;
   }
}

static int lz4ultra_compare(const char *pszInFilename, const char *pszOutFilename, const unsigned int nOptions) {
   long long nStartTime = 0LL, nEndTime = 0LL;
   long long nOriginalSize = 0LL;
   long long nKnownGoodSize = 0LL;
   unsigned int nFileSize = 0;

   FILE *pInFile = fopen(pszInFilename, "rb");
   if (!pInFile) {
      fprintf(stderr, "error opening compressed input file\n");
      return 100;
   }

   if ((nOptions & OPT_RAW) == 0) {
      unsigned char cHeader[7];

      memset(cHeader, 0, 7);

      if (fread(cHeader, 1, 7, pInFile) != 7) {
         fclose(pInFile);
         pInFile = NULL;
         fprintf(stderr, "error reading header in compressed input file\n");
         return 100;
      }

      if (cHeader[0] != 0x04 ||
         cHeader[1] != 0x22 ||
         cHeader[2] != 0x4D ||
         cHeader[3] != 0x18 ||
         cHeader[4] != 0b01000000 ||
         cHeader[5] != (4 << 4) ||
         cHeader[6] != 0xc0) {
         fclose(pInFile);
         pInFile = NULL;
         fprintf(stderr, "invalid magic number, version, flags, block size or checksum in input file\n");
         return 100;
      }
   }
   else {
      fseek(pInFile, 0, SEEK_END);
      nFileSize = (unsigned int)ftell(pInFile);
      fseek(pInFile, 0, SEEK_SET);

      if (nFileSize < 2) {
         fclose(pInFile);
         pInFile = NULL;
         fprintf(stderr, "invalid file size for raw block mode\n");
         return 100;
      }
   }

   FILE *pOutFile = fopen(pszOutFilename, "rb");
   if (!pOutFile) {
      fclose(pInFile);
      pInFile = NULL;
      fprintf(stderr, "error opening original uncompressed file\n");
      return 100;
   }

   unsigned char *pInBlock;
   unsigned char *pOutData;
   unsigned char *pCompareData;

   pInBlock = (unsigned char*)malloc(BLOCK_SIZE);
   if (!pInBlock) {
      fclose(pOutFile);
      pOutFile = NULL;

      fclose(pInFile);
      pInFile = NULL;
      fprintf(stderr, "error opening output file\n");
      return 100;
   }

   pOutData = (unsigned char*)malloc(BLOCK_SIZE * 2);
   if (!pOutData) {
      free(pInBlock);
      pInBlock = NULL;

      fclose(pOutFile);
      pOutFile = NULL;

      fclose(pInFile);
      pInFile = NULL;
      fprintf(stderr, "error opening output file\n");
      return 100;
   }

   pCompareData = (unsigned char*)malloc(BLOCK_SIZE);
   if (!pCompareData) {
      free(pOutData);
      pOutData = NULL;

      free(pInBlock);
      pInBlock = NULL;

      fclose(pOutFile);
      pOutFile = NULL;

      fclose(pInFile);
      pInFile = NULL;
      fprintf(stderr, "error opening output file\n");
      return 100;
   }

   if (nOptions & OPT_VERBOSE) {
      nStartTime = lz4ultra_get_time();
   }

   int nDecompressionError = 0;
   bool bComparisonError = false;
   int nPrevDecompressedSize = 0;

   while (!feof(pInFile) && !nDecompressionError && !bComparisonError) {
      unsigned int nBlockSize = 0;

      if (nPrevDecompressedSize != 0) {
         memcpy(pOutData + BLOCK_SIZE - nPrevDecompressedSize, pOutData + BLOCK_SIZE, nPrevDecompressedSize);
      }

      int nBytesToCompare = (int)fread(pCompareData, 1, BLOCK_SIZE, pOutFile);

      if ((nOptions & OPT_RAW) == 0) {
         unsigned char cBlockSize[4];

         if (fread(cBlockSize, 1, 4, pInFile) == 4) {
            nBlockSize = ((unsigned int)cBlockSize[0]) |
               (((unsigned int)cBlockSize[1]) << 8) |
               (((unsigned int)cBlockSize[2]) << 16) |
               (((unsigned int)cBlockSize[3]) << 24);
         }
         else {
            nBlockSize = 0;
         }
      }
      else {
         nBlockSize = nFileSize - 2;
         nFileSize = 0;
      }

      if (nBlockSize != 0) {
         bool bIsUncompressed = (nBlockSize & 0x80000000) != 0;
         int nDecompressedSize = 0;

         nBlockSize &= 0x7fffffff;
         if (fread(pInBlock, 1, nBlockSize, pInFile) == nBlockSize) {
            if (bIsUncompressed) {
               memcpy(pOutData + BLOCK_SIZE, pInBlock, nBlockSize);
               nDecompressedSize = nBlockSize;
            }
            else {
               unsigned int nBlockOffs = 0;

               nDecompressedSize = lz4ultra_expand_block(pInBlock, nBlockSize, pOutData, BLOCK_SIZE, BLOCK_SIZE);
               if (nDecompressedSize < 0) {
                  nDecompressionError = nDecompressedSize;
                  break;
               }
            }

            if (nDecompressedSize == nBytesToCompare) {
               nKnownGoodSize = nOriginalSize;

               nOriginalSize += (long long)nDecompressedSize;

               if (memcmp(pOutData + BLOCK_SIZE, pCompareData, nBytesToCompare))
                  bComparisonError = true;
               nPrevDecompressedSize = nDecompressedSize;
               nDecompressedSize = 0;
            }
            else {
               bComparisonError = true;
               break;
            }
         }
         else {
            break;
         }
      }
      else {
         break;
      }
   }

   free(pCompareData);
   pCompareData = NULL;

   free(pOutData);
   pOutData = NULL;

   free(pInBlock);
   pInBlock = NULL;

   fclose(pOutFile);
   pOutFile = NULL;

   fclose(pInFile);
   pInFile = NULL;

   if (nDecompressionError) {
      fprintf(stderr, "decompression error for '%s'\n", pszInFilename);
      return 100;
   }
   else if (bComparisonError) {
      fprintf(stderr, "error comparing compressed file '%s' with original '%s' starting at %lld\n", pszInFilename, pszOutFilename, nKnownGoodSize);
      return 100;
   }
   else {
      if (nOptions & OPT_VERBOSE) {
         nEndTime = lz4ultra_get_time();
         double fDelta = ((double)(nEndTime - nStartTime)) / 1000000.0;
         double fSpeed = ((double)nOriginalSize / 1048576.0) / fDelta;
         fprintf(stdout, "Compared '%s' in %g seconds, %g Mb/s\n",
            pszInFilename, fDelta, fSpeed);
      }

      return 0;
   }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv) {
   int i;
   const char *pszInFilename = NULL;
   const char *pszOutFilename = NULL;
   bool bArgsError = false;
   bool bCommandDefined = false;
   bool bVerifyCompression = false;
   char cCommand = 'z';
   unsigned int nOptions = 0;

   for (i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "-d")) {
         if (!bCommandDefined) {
            bCommandDefined = true;
            cCommand = 'd';
         }
         else
            bArgsError = true;
      }
      else if (!strcmp(argv[i], "-z")) {
         if (!bCommandDefined) {
            bCommandDefined = true;
            cCommand = 'z';
         }
         else
            bArgsError = true;
      }
      else if (!strcmp(argv[i], "-c")) {
         if (!bVerifyCompression) {
            bVerifyCompression = true;
         }
         else
            bArgsError = true;
      }
      else if (!strcmp(argv[i], "-v")) {
         if ((nOptions & OPT_VERBOSE) == 0) {
            nOptions |= OPT_VERBOSE;
         }
         else
            bArgsError = true;
      }
      else if (!strcmp(argv[i], "-r")) {
         if ((nOptions & OPT_RAW) == 0) {
            nOptions |= OPT_RAW;
         }
         else
            bArgsError = true;
      }
      else {
         if (!pszInFilename)
            pszInFilename = argv[i];
         else {
            if (!pszOutFilename)
               pszOutFilename = argv[i];
            else
               bArgsError = true;
         }
      }
   }

   if (bArgsError || !pszInFilename || !pszOutFilename) {
      fprintf(stderr, "usage: %s [-c] [-d] [-v] [-r] <infile> <outfile>\n", argv[0]);
      fprintf(stderr, "       -c: check resulting stream after compressing\n");
      fprintf(stderr, "       -d: decompress (default: compress)\n");
      fprintf(stderr, "       -v: be verbose\n");
      fprintf(stderr, "       -r: raw block format (max. 64 Kb files)\n");
      return 100;
   }

   if (cCommand == 'z') {
      int nResult = lz4ultra_compress(pszInFilename, pszOutFilename, nOptions);
      if (nResult == 0 && bVerifyCompression) {
         nResult = lz4ultra_compare(pszOutFilename, pszInFilename, nOptions);
      }
   }
   else if (cCommand == 'd') {
      return lz4ultra_decompress(pszInFilename, pszOutFilename, nOptions);
   }
   else {
      return 100;
   }
}
