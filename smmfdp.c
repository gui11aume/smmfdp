#define _GNU_SOURCE

#include <math.h>

#include "bwt.h"
#include "map.h"
#include "divsufsort.h"
#include "mem_seed_prob.h"

#define GAMMA 17

// ------- Machine-specific code ------- //
// The 'mmap()' option 'MAP_POPULATE' is available
// only on Linux and from kern version 2.6.23.
#if __linux__
  #include <linux/version.h>
  #if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22)
    #define _MAP_POPULATE_AVAILABLE
  #endif
#endif

#ifdef _MAP_POPULATE_AVAILABLE
  #define MMAP_FLAGS (MAP_PRIVATE | MAP_POPULATE)
#else
  #define MMAP_FLAGS MAP_PRIVATE
#endif


// ------- Machine-specific code ------- //
// The 'mmap()' option 'MAP_POPULATE' is available
// only on Linux and from kern version 2.6.23.
#if __linux__
  #include <linux/version.h>
  #if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22)
    #define _MAP_POPULATE_AVAILABLE
  #endif
#endif

#ifdef _MAP_POPULATE_AVAILABLE
  #define MMAP_FLAGS (MAP_PRIVATE | MAP_POPULATE)
#else
  #define MMAP_FLAGS MAP_PRIVATE
#endif


// Error-handling macro.
#define exit_cannot_open(x) \
   do { fprintf(stderr, "cannot open file '%s' %s:%d:%s()\n", (x), \
         __FILE__, __LINE__, __func__); exit(EXIT_FAILURE); } while(0)

#define exit_error(x) \
   do { if (x) { fprintf(stderr, "error: %s %s:%d:%s()\n", #x, \
         __FILE__, __LINE__, __func__); exit(EXIT_FAILURE); }} while(0)


typedef struct uN0_t uN0_t;
struct uN0_t {
   double u;
   size_t N0;
};


void
build_index
(
   const char * fname
)
{

   // Open fasta file.
   FILE * fasta = fopen(fname, "r");
   if (fasta == NULL) exit_cannot_open(fname);

   char buff[256];

   // Read and normalize genome
   sprintf(buff, "%s.chr", fname);
   fprintf(stderr, "reading genome... ");
   char * genome = normalize_genome(fasta, buff);
   fprintf(stderr, "done.\n");

   fprintf(stderr, "creating suffix array... ");
   int64_t * sa = compute_sa(genome);
   fprintf(stderr, "done.\n");

   fprintf(stderr, "creating bwt... ");
   bwt_t * bwt = create_bwt(genome, sa);
   fprintf(stderr, "done.\n");

   fprintf(stderr, "creating Occ table... ");
   occ_t * occ = create_occ(bwt);
   fprintf(stderr, "done.\n");

   fprintf(stderr, "filling lookup table... ");
   lut_t * lut = malloc(sizeof(lut_t));
   fill_lut(lut, occ, (range_t) {.bot=1, .top=strlen(genome)}, 0, 0);
   fprintf(stderr, "done.\n");

   fprintf(stderr, "compressing suffix array... ");
   csa_t * csa = compress_sa(sa);
   fprintf(stderr, "done.\n");

   // Write files
   char * data;
   ssize_t ws;
   size_t sz;


   // Write the compressed suffix array file.
   sprintf(buff, "%s.sa", fname);
   int fsar = creat(buff, 0644);
   if (fsar < 0) exit_cannot_open(buff);
   
   ws = 0;
   sz = sizeof(csa_t) + csa->nint64 * sizeof(int64_t);
   data = (char *) csa;
   while (ws < sz) ws += write(fsar, data + ws, sz - ws);
   close(fsar);


   // Write the Burrows-Wheeler transform.
   sprintf(buff, "%s.bwt", fname);
   int fbwt = creat(buff, 0644);
   if (fbwt < 0) exit_cannot_open(buff);

   ws = 0;
   sz = sizeof(bwt_t) + bwt->nslots * sizeof(uint8_t);
   data = (char *) bwt;
   while (ws < sz) ws += write(fbwt, data + ws, sz - ws);
   close(fbwt);


   // Write the Occ table.
   sprintf(buff, "%s.occ", fname);
   int focc = creat(buff, 0644);
   if (focc < 0) exit_cannot_open(buff);

   ws = 0;
   sz = sizeof(occ_t) + occ->nrows * SIGMA * sizeof(blocc_t);
   data = (char *) occ;
   while (ws < sz) ws += write(focc, data + ws, sz - ws);
   close(focc);


   // Write the lookup table
   sprintf(buff, "%s.lut", fname);
   int flut = creat(buff, 0644);
   if (flut < 0) exit_cannot_open(buff);

   ws = 0;
   sz = sizeof(lut_t);
   data = (char *) lut;
   while (ws < sz) ws += write(flut, data + ws, sz - ws);
   close(flut);

   // Clean up.
   free(csa);
   free(bwt);
   free(occ);
   free(lut);

}


index_t
load_index
(
   const char  * fname
)
{

   size_t mmsz;
   char buff[256];

   chr_t * chr = index_load_chr(fname);
   exit_error(chr == NULL);

   
   sprintf(buff, "%s.sa", fname);
   int fsar = open(buff, O_RDONLY);
   if (fsar < 0) exit_cannot_open(buff);

   mmsz = lseek(fsar, 0, SEEK_END);
   csa_t *csa = (csa_t *) mmap(NULL, mmsz, PROT_READ, MMAP_FLAGS, fsar, 0);
   exit_error(csa == NULL);
   close(fsar);


   sprintf(buff, "%s.bwt", fname);
   int fbwt = open(buff, O_RDONLY);
   if (fbwt < 0) exit_cannot_open(buff);

   mmsz = lseek(fbwt, 0, SEEK_END);
   bwt_t *bwt = (bwt_t *) mmap(NULL, mmsz, PROT_READ, MMAP_FLAGS, fbwt, 0);
   exit_error(bwt == NULL);
   close(fbwt);


   sprintf(buff, "%s.occ", fname);
   int focc = open(buff, O_RDONLY);
   if (focc < 0) exit_cannot_open(buff);

   mmsz = lseek(focc, 0, SEEK_END);
   occ_t *occ = (occ_t *) mmap(NULL, mmsz, PROT_READ, MMAP_FLAGS, focc, 0);
   exit_error(occ == NULL);
   close(focc);


   sprintf(buff, "%s.lut", fname);
   int flut = open(buff, O_RDONLY);
   if (flut < 0) exit_cannot_open(buff);

   mmsz = lseek(flut, 0, SEEK_END);
   lut_t *lut = (lut_t *) mmap(NULL, mmsz, PROT_READ, MMAP_FLAGS, focc, 0);
   exit_error(lut == NULL);
   close(flut);

   return (index_t) {.chr = chr, .csa = csa, .bwt = bwt,
      .occ = occ, .lut = lut };

}


uN0_t
estimate_uN0
(
   const size_t * l_cascade,
   const size_t * r_cascade,
   const size_t   slen
)
{

   // Here we need to pay attention to the fact that C is
   // 0-based, which creates some confusion for the value
   // of 'n'. For clarity, we say that 'n' is the first
   // number in the 1-based convension, and we shift it
   // by 1 when accessing C arrays.
   
   // FIXME: another weak assert.
   assert (GAMMA + 3 < slen);

   const int n = GAMMA + 4; // Skip the first GAMMA + 3.
   const double MU[3] = {.06, .04, .02};

   const double L1 = l_cascade[n-1] - 1;
   const double R1 = r_cascade[n-1] - 1;
         double L2 = n * (l_cascade[n-1] - 1);
         double R2 = n * (r_cascade[n-1] - 1);

   for (int i = n+1 ; i < slen+1 ; i++) {
      L2 += l_cascade[i-1] - 1;
      R2 += r_cascade[i-1] - 1;
   }

   double loglik = -INFINITY;
   size_t best_N0 = 0.0;
   double best_mu = 0.0;

   for (int iter = 0 ; iter < 3 ; iter++) {

      double mu = MU[iter];

      double L3 = L2 / (1-mu) - L1 / mu;
      double R3 = R2 / (1-mu) - R1 / mu;

      double C = (1 - pow(1-mu,n)) / (n * pow(1-mu,n-1));

      // Compute the number of duplicates.
      double N0 = (L1+R1 + C*(L3+R3)) / 2.0;
//      double N0 = L1 + C*L3;

      if (N0 < 1.0) N0 = 1.0;

      double tmp = 2*lgamma(N0+1) + (L1+R1) * log(mu)
         + (L2+R2) * log(1-mu)
         + (2*N0 - (L1+L2)) * log(1-pow(1-mu,n))
         - lgamma(N0-L1+1)  - lgamma(N0-R1+1);
//      double tmp = lgamma(N0+1) + L1 * log(mu) + L2 * log(1-mu)
//         + (N0-L1) * log(1-pow(1-mu,n)) - lgamma(N0-L1+1);

      if (tmp < loglik) {
         break;
      }

      loglik = tmp;
      best_N0 = round(N0);
      best_mu = mu;

   }

   return (uN0_t) { best_mu, best_N0 };

}


double
quality
(
         aln_t   aln,
   const char * seq,
         index_t idx
)
{

   size_t slen = strlen(seq);

   assert(slen < 250);
   size_t l_cascade[250] = {0};
   size_t r_cascade[250] = {0};

   size_t merid;
   int mlen;

   // Bacwkard search on the hit.
   range_t range;

   // Look up the beginning (reverse) of the query in lookup table.
   merid = 0;
   for (mlen = 0 ; mlen < LUTK ; mlen++) {
      uint8_t c = ENCODE[(uint8_t) aln.refseq[slen-1-mlen]];
      merid = c + (merid << 2);
   }
   range = idx.lut->kmer[merid];

   for ( ; mlen < slen ; mlen++) {
      if (NONALPHABET[(uint8_t) aln.refseq[slen-mlen-1]])
         return 0.0/0.0;
      int c = ENCODE[(uint8_t) aln.refseq[slen-mlen-1]];
      range.bot = get_rank(idx.occ, c, range.bot - 1);
      range.top = get_rank(idx.occ, c, range.top) - 1;
      l_cascade[mlen] = range.top - range.bot + 1;
   }

   // Look up the beginning (forward) of the query in lookup table.
   merid = 0;
   for (mlen = 0 ; mlen < LUTK ; mlen++) {
      uint8_t c = REVCMP[(uint8_t) aln.refseq[mlen]];
      merid = c + (merid << 2);
   }
   range = idx.lut->kmer[merid];

   for ( ; mlen < slen ; mlen++) {
      if (NONALPHABET[(uint8_t) aln.refseq[mlen]])
         return 0.0/0.0;
      int c = REVCMP[(uint8_t) aln.refseq[mlen]];
      range.bot = get_rank(idx.occ, c, range.bot - 1);
      range.top = get_rank(idx.occ, c, range.top) - 1;
      r_cascade[mlen] = range.top - range.bot + 1;
   }

#if 0
   // Here we need to pay attention to the fact that C is
   // 0-based, which creates some confusion for the value
   // of 'n'. For clarity, we say that 'n' is the first
   // number in the 1-based convension, and we shift it
   // by 1 when accessing C arrays.
   
   // FIXME: another weak assert.
   assert (GAMMA + 3 < slen);

   const int n = GAMMA + 4; // Skip the first GAMMA + 3.
   const double MU[3] = {.06, .04, .02};

   const double L1 = l_cascade[n-1] - 1;
   const double R1 = r_cascade[n-1] - 1;
         double L2 = n * (l_cascade[n-1] - 1);
         double R2 = n * (r_cascade[n-1] - 1);

   for (int i = n+1 ; i < slen+1 ; i++) {
      L2 += l_cascade[i-1] - 1;
      R2 += r_cascade[i-1] - 1;
   }

   double loglik = -INFINITY;
   size_t best_N0 = 0.0;
   double best_mu = 0.0;

   for (int iter = 0 ; iter < 3 ; iter++) {

      double mu = MU[iter];

      double L3 = L2 / (1-mu) - L1 / mu;
      double R3 = R2 / (1-mu) - R1 / mu;

      double C = (1 - pow(1-mu,n)) / (n * pow(1-mu,n-1));

      // Compute the number of duplicates.
      double N0 = (L1+R1 + C*(L3+R3)) / 2.0;

      if (N0 < 1.0) N0 = 1.0;

      double tmp = 2*lgamma(N0+1) + (L1+R1) * log(mu)
         + (L2+R2) * log(1-mu)
         + (2*N0 - (L1+L2)) * log(1-pow(1-mu,n))
         - lgamma(N0-L1+1)  - lgamma(N0-L2+1);

      if (tmp < loglik) {
         break;
      }

      loglik = tmp;
      best_N0 = round(N0);
      best_mu = mu;

   }
#endif

   uN0_t uN0 = estimate_uN0(l_cascade, r_cascade, slen);

   double best_mu = uN0.u;
   double best_N0 = uN0.N0;

   double typeI = prob_typeI_MEM_failure(slen, best_mu, best_N0) / 5;
   double typeII = prob_typeII_MEM_failure(slen, best_mu, best_N0);

   if (best_N0 == 1 && best_mu == 0.06)
      typeII /= 5;

   // Binomial terms (type I).
   double A = lgamma(slen+1) - lgamma(slen-aln.score+1) -
      lgamma(aln.score+1) + aln.score * log(0.01) +
      (slen-aln.score) * log(0.99);
   double B = lgamma(slen-GAMMA+1) - lgamma(slen-GAMMA-aln.score+1) -
      lgamma(aln.score+1) + aln.score * log(0.75) +
      (slen-GAMMA-aln.score) * log(0.25);
   double prob_typeI_given_data =
      1.0 / ( 1.0 + exp(A + log(1-typeI) - B - log(typeI)) );

   return prob_typeI_given_data + typeII >= 1.0 ? 1.0 :
      prob_typeI_given_data + typeII;

}


void
batchmap
(
   const char * indexfname,
   const char * readsfname
)
{

   fprintf(stderr, "loading index... ");
   // Load index files.
   index_t idx = load_index(indexfname);

   // Load the genome.
   FILE * fasta = fopen(indexfname, "r");
   if (fasta == NULL) exit_cannot_open(indexfname);

   char * genome = normalize_genome(fasta, NULL);

   fprintf(stderr, "done.\n");
   FILE * inputf = fopen(readsfname, "r");
   if (inputf == NULL) exit_cannot_open(readsfname);


   size_t sz = 64;
   ssize_t rlen;
   char * seq = malloc(64);
   exit_error(seq == NULL);

   size_t counter = 0; // Used for randomizing.
   size_t maxlen = 0; // Max 'k' value for seeding probabilities.

   // Read sequence file line by line.
   while ((rlen = getline(&seq, &sz, inputf)) != -1) {
      
      // Remove newline character if present.
      if (seq[rlen-1] == '\n') seq[rlen-1] = '\0';

      if (rlen > maxlen) {
         maxlen = rlen;
         // Initialize library with read length.
         set_params_mem_prob(GAMMA, rlen, .01);
      }

      alnstack_t * alnstack = mapread(seq, idx, genome, GAMMA);

      if (!alnstack) exit(EXIT_FAILURE);

      // Take one of the best alignments at "random".
      if (alnstack->pos == 0) {
         fprintf(stdout, "%s\tNA\tNA\n", seq);
         free(alnstack);
         continue;
      }

      aln_t aln = alnstack->aln[counter++ % alnstack->pos];

      char * apos = chr_string(aln.refpos, idx.chr);
      double prob = alnstack->pos > 1 ?
                        1.0 - 1.0 / alnstack->pos :
                        quality(aln, seq, idx);
      fprintf(stdout, "%s\t%s\t%f\n", seq, apos, prob);

      free(apos);
      free(alnstack);

   }

   free(seq);
   clean_mem_prob();

   // FIXME: Would be nice to do this, but the size is unknown.
   // FIXME: Either forget it, or store the size somewhere.
   //munmap(idx.csa);
   //munmap(idx.bwt);
   //munmap(idx.occ);
   //munmap(idx.lut);

}


int main(int argc, char ** argv) {

   // Sanity checks.
   if (argc < 2) {
      fprintf(stderr, "First argument must be \"index\" or \"mem\".\n");
      exit(EXIT_FAILURE);
   }

   if (strcmp(argv[1], "index") == 0) {
      if (argc < 3) {
         fprintf(stderr, "Specify file to index.\n");
         exit(EXIT_FAILURE);
      }
      build_index(argv[2]);
   }
   else if (strcmp(argv[1], "mem") == 0) {
      if (argc < 4) {
         fprintf(stderr, "Specify index and read file.\n");
         exit(EXIT_FAILURE);
      }
      batchmap(argv[2], argv[3]);
   }
   else {
      fprintf(stderr, "First argument must be \"index\" or \"mem\".\n");
      exit(EXIT_FAILURE);
   }

}
