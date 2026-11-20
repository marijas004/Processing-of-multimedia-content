#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <omp.h>
#include <mpi.h>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

#pragma pack(push, 1)
typedef struct {
    uint16_t bfType; // tip fajla, mora biti 0x4D42 za BMP
    uint32_t bfSize; // ukupna velicina fajla u bajtovima
    uint16_t bfReserved1; // rezervisano, mora biti 0
    uint16_t bfReserved2; // rezervisano, mora biti 0
    uint32_t bfOffBits; // offset podataka o slici
} BITMAPFILEHEADER;

typedef struct {
    uint32_t biSize; // velicina same strukture
    int32_t biWidth; // širina slike u pikselima
    int32_t biHeight; // visina slike u pikselima
    uint16_t biPlanes; // broj ravnina
    uint16_t biBitCount; // broj bitova po pikselu (24 za RGB, po 8 bitova za svaku boju)
    uint32_t biCompression; // tip kompresije
    uint32_t biSizeImage; // velicina slike u bajtovima
    int32_t biXPelsPerMeter; // broj piksela po metru u x-pravcu
    int32_t biYPelsPerMeter; // broj piksela po metru u y-pravcu
    uint32_t biClrUsed; // broj boja u paleti boja (0 za RGB)
    uint32_t biClrImportant; // broj važnih boja  (0 znači sve boje su važne)
} BITMAPINFOHEADER;
#pragma pack(pop)

unsigned char *load_bmp(const char *filename, int *outWidth, int *outHeight)
{
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("fopen");
        return NULL;
    }

    BITMAPFILEHEADER bfh;
    BITMAPINFOHEADER bih;
    if (fread(&bfh, sizeof bfh, 1, f) != 1 || fread(&bih, sizeof bih, 1, f) != 1) {
        fprintf(stderr, "Greska pri citanju BMP zaglavlja\n");
        fclose(f);
        return NULL;
    }

    if (bfh.bfType != 0x4D42 || bih.biBitCount != 24 || bih.biCompression != 0) {
        fprintf(stderr, "Podrzan je samo nekompresovani 24-bitni BMP format!\n");
        fclose(f);
        return NULL;
    }

    int w = bih.biWidth;
    int h = abs(bih.biHeight);
    int row_bytes = ((w * 3 + 3) / 4) * 4; // pravilo BMP formata nalaže da svaki red u datoteci mora po veličini u bajtovima biti djeljiv s 4

    unsigned char *data = malloc(w * h * 3);
    unsigned char *scan = malloc(row_bytes); // privremeni bafer za čitanje jednog reda piksela

    if (!data || !scan) {
        perror("malloc");
        free(data);
        free(scan);
        fclose(f);
        return NULL;
    }

    fseek(f, bfh.bfOffBits, SEEK_SET); // pomeramo se na početak podataka o slici (54. bajt)
    for (int y = 0; y < h; y++) {
        int row = (bih.biHeight > 0) ? (h - 1 - y) : y; // BMP format čuva redove odozdo prema gore ako je biHeight pozitivan, a odozgo prema dolje ako je negativan
        unsigned char *ptr = data + (row * w * 3); // pokazivač na početak trenutnog reda u izlaznom baferu-data

        if (fread(scan, 1, row_bytes, f) != (size_t)row_bytes) {
            fprintf(stderr, "Greska pri citanju reda piksela\n");
            free(scan);
            free(data);
            fclose(f);
            return NULL;
        }

        // BGR -> RGB
        for (int x = 0; x < w; x++) { 
            ptr[3 * x + 0] = scan[3 * x + 2];
            ptr[3 * x + 1] = scan[3 * x + 1];
            ptr[3 * x + 2] = scan[3 * x + 0];
        }
    }

    free(scan); // Oslobađamo privremeni bafer
    fclose(f);
    *outWidth = w;
    *outHeight = h;
    return data;
}

int write_bmp(const char *filename, const unsigned char *data, int w, int h) // funkcija za cuvanje slike u BMP formatu
{
    int row_bytes = ((w * 3 + 3) / 4) * 4;
    uint32_t imageSize = row_bytes * h;

    BITMAPFILEHEADER bfh = {
        .bfType = 0x4D42,
        .bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + imageSize,
        .bfReserved1 = 0,
        .bfReserved2 = 0,
        .bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)
    };

    BITMAPINFOHEADER bih = {
        .biSize = sizeof(BITMAPINFOHEADER),
        .biWidth = w,
        .biHeight = h,
        .biPlanes = 1,
        .biBitCount = 24,
        .biCompression = 0,
        .biSizeImage = imageSize,
        .biXPelsPerMeter = 2835,
        .biYPelsPerMeter = 2835,
        .biClrUsed = 0,
        .biClrImportant = 0
    };

    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("fopen");
        return -1;
    }

    if (fwrite(&bfh, sizeof bfh, 1, f) != 1 || fwrite(&bih, sizeof bih, 1, f) != 1) {
        fprintf(stderr, "Greska pri pisanju zaglavlja\n");
        fclose(f);
        return -1;
    }

    unsigned char *scan = malloc(row_bytes);
    if (!scan) {
        perror("malloc");
        fclose(f);
        return -1;
    }

    for (int y = 0; y < h; y++) {
        int row = h - 1 - y;
        const unsigned char *ptr = data + (row * w * 3);

        for (int x = 0; x < w; x++) {
            scan[3 * x + 0] = ptr[3 * x + 2]; // RGB -> BGR
            scan[3 * x + 1] = ptr[3 * x + 1];
            scan[3 * x + 2] = ptr[3 * x + 0];
        }
        for (int p = 3 * w; p < row_bytes; p++) {
            scan[p] = 0; // Padding
        }
        fwrite(scan, 1, row_bytes, f);
    }

    free(scan);
    fclose(f);
    return 0;
}

void apply_convolution(const unsigned char *input, unsigned char *output, int w, int h, const float *kernel, int k_size)
{
    int offset = k_size / 2; //govori nam koliko se susjednih piksela uzima sa svake strane centralnog piksela (1 lijevo, 1 desno, 1 gore, 1 dole)

    #pragma omp parallel for schedule(dynamic) // dijeli posao između više niti, svaka nit obrađuje različite redove slike
    for (int y = offset; y < h - offset; y++) {
        for (int x = offset; x < w - offset; x++) {
            float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f;

            for (int ky = -offset; ky <= offset; ky++) {
                for (int kx = -offset; kx <= offset; kx++) {
                    int pixel_idx = ((y + ky) * w + (x + kx)) * 3; // pretvara 2D koordinatu u redni broj piksela u nizu *3
                    float k_val = kernel[(ky + offset) * k_size + (kx + offset)]; // uzima odgovarajuću vrijednost kernela (koja je u istom položaju kao i susjedni piksel)

                    sum_r += input[pixel_idx + 0] * k_val;
                    sum_g += input[pixel_idx + 1] * k_val;
                    sum_b += input[pixel_idx + 2] * k_val;
                }
            }

            int dest_idx = (y * w + x) * 3;
            output[dest_idx + 0] = (unsigned char)(sum_r > 255.0f ? 255 : (sum_r < 0.0f ? 0 : sum_r));
            output[dest_idx + 1] = (unsigned char)(sum_g > 255.0f ? 255 : (sum_g < 0.0f ? 0 : sum_g));
            output[dest_idx + 2] = (unsigned char)(sum_b > 255.0f ? 255 : (sum_b < 0.0f ? 0 : sum_b));
        }
    }
}


/* ====================================================================
 *  HIBRIDNI MPI + OpenMP DIO
 *
 *  Nivo 1 (distribuirano):  MPI procesi dijele SKUP FAJLOVA medju sobom
 *                           (svaki proces ima svoju memoriju).
 *  Nivo 2 (visejezgarno):   unutar procesa, OpenMP dijeli REDOVE JEDNE
 *                           slike medju nitima (apply_convolution).
 * ==================================================================== */

typedef struct {
    char name[256];
    long size;      // velicina fajla u bajtovima (mjera "tezine" posla)
    int owner;      // MPI rank koji obradjuje ovaj fajl
} FileInfo;

static int has_bmp_ext(const char *n)
{
    size_t l = strlen(n);
    if (l < 5) return 0;
    const char *e = n + l - 4;
    return (e[0] == '.') &&
           (e[1] == 'b' || e[1] == 'B') &&
           (e[2] == 'm' || e[2] == 'M') &&
           (e[3] == 'p' || e[3] == 'P');
}

// sortiranje: najveci fajl prvi; kod istih velicina po imenu (deterministicki na svim rankovima)
static int cmp_files(const void *a, const void *b)
{
    const FileInfo *x = a, *y = b;
    if (x->size != y->size) return (y->size > x->size) - (y->size < x->size);
    return strcmp(x->name, y->name);
}

static int list_bmp_files(const char *dir, FileInfo **out)
{
    DIR *d = opendir(dir);
    if (!d) { perror("opendir"); return -1; }

    int cap = 64, n = 0;
    FileInfo *v = malloc(cap * sizeof(FileInfo));
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!has_bmp_ext(e->d_name) || strlen(e->d_name) >= sizeof(v[0].name)) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (n == cap) { cap *= 2; v = realloc(v, cap * sizeof(FileInfo)); }
        strcpy(v[n].name, e->d_name);
        v[n].size = (long)st.st_size;
        v[n].owner = -1;
        n++;
    }
    closedir(d);
    qsort(v, n, sizeof(FileInfo), cmp_files);
    *out = v;
    return n;
}

/*
 * Raspodjela fajlova po procesima: "Longest Processing Time first" (LPT).
 * Fajlovi su vec sortirani od najveceg; svaki ide procesu koji trenutno ima
 * najmanje posla. Svi rankovi racunaju ISTO (isti ulaz, isti algoritam),
 * pa nikakva MPI komunikacija za raspodjelu nije potrebna.
 */
static void assign_files(FileInfo *v, int n, int nranks)
{
    long *load = calloc(nranks, sizeof(long));
    for (int i = 0; i < n; i++) {
        int best = 0;
        for (int r = 1; r < nranks; r++)
            if (load[r] < load[best]) best = r;
        v[i].owner = best;
        load[best] += v[i].size;
    }
    free(load);
}

// ucitaj -> konvolucija -> upisi; vraca 0 ako je OK. Vremena I/O i racunanja se mjere odvojeno.
static int process_file(const char *in_path, const char *out_path,
                        const float *kernel, int k_size,
                        double *t_io, double *t_comp)
{
    int w, h;
    double t0 = MPI_Wtime();
    unsigned char *in = load_bmp(in_path, &w, &h);
    if (!in) return -1;
    unsigned char *out = malloc((size_t)w * h * 3);
    if (!out) { free(in); return -1; }
    memcpy(out, in, (size_t)w * h * 3);          // rubni pikseli ostaju iz ulaza
    double t1 = MPI_Wtime();

    apply_convolution(in, out, w, h, kernel, k_size);   // OpenMP unutar procesa
    double t2 = MPI_Wtime();

    int rc = write_bmp(out_path, out, w, h);
    double t3 = MPI_Wtime();

    free(in);
    free(out);
    *t_io   += (t1 - t0) + (t3 - t2);
    *t_comp += (t2 - t1);
    return rc;
}

int main(int argc, char *argv[])
{
    // OpenMP se poziva samo iz glavne niti procesa -> dovoljan je FUNNELED nivo (pokrece se iz glavne niti)
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    int rank, nranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nranks);

    if (argc < 3) {
        if (rank == 0)
            fprintf(stderr, "Upotreba: %s <ulazni_dir> <izlazni_dir> [reps] [k_size k_0 ... k_(k*k-1)]\n", argv[0]);
        MPI_Finalize();
        return 1;
    }
    const char *in_dir  = argv[1];
    const char *out_dir = argv[2];
    int reps = (argc >= 4) ? atoi(argv[3]) : 5;
    if (reps < 1) reps = 1;

    // kernel: podrazumijevano box blur 3x3; inace iz komandne linije (isto kao u main.c)
    int k_size = 3;
    float default_kernel[9];
    for (int i = 0; i < 9; i++) default_kernel[i] = 1.0f / 9.0f;
    float *kernel = default_kernel;
    if (argc >= 5) {
        int ks = atoi(argv[4]);
        if (ks >= 1 && (ks % 2) == 1 && argc >= 5 + ks * ks) {
            k_size = ks;
            kernel = malloc(ks * ks * sizeof(float));
            for (int i = 0; i < ks * ks; i++) kernel[i] = (float)atof(argv[5 + i]);
        } else if (rank == 0) {
            fprintf(stderr, "Neispravan kernel (k_size mora biti neparan i navedeno k*k vrijednosti). Koristim box blur 3x3.\n");
        }
    }

    if (rank == 0) MKDIR(out_dir);
    MPI_Barrier(MPI_COMM_WORLD); // nijedan proces ne smije krenuti dok direktorij ne bude kreiran

    FileInfo *files = NULL;
    int nfiles = list_bmp_files(in_dir, &files);
    if (nfiles <= 0) {
        if (rank == 0) fprintf(stderr, "Nema .bmp fajlova u '%s'\n", in_dir);
        MPI_Finalize();
        return 1;
    }
    assign_files(files, nfiles, nranks);

    int my_count = 0;
    long my_bytes = 0;
    for (int i = 0; i < nfiles; i++)
        if (files[i].owner == rank) { my_count++; my_bytes += files[i].size; }

    int nthreads = 2; // koliko niti ce OpenMP koristiti unutar procesa
    if (rank == 0) {
        printf("MPI procesa: %d | OpenMP niti po procesu: %d | fajlova: %d | kernel %dx%d | mjerenja: %d\n",
               nranks, nthreads, nfiles, k_size, k_size, reps);
    }

    double *t_total = malloc(reps * sizeof(double));
    double *t_compute = malloc(reps * sizeof(double));
    double last_local_total = 0.0;

    // rep = -1 je zagrijavanje (ne mjeri se), zatim reps mjerenja
    for (int rep = -1; rep < reps; rep++) {
        double io = 0.0, comp = 0.0;
        MPI_Barrier(MPI_COMM_WORLD);                 // svi krecu istovremeno
        double t0 = MPI_Wtime();

        for (int i = 0; i < nfiles; i++) {
            if (files[i].owner != rank) continue;
            char in_path[1024], out_path[1024];
            snprintf(in_path,  sizeof in_path,  "%s/%s", in_dir,  files[i].name);
            snprintf(out_path, sizeof out_path, "%s/%s", out_dir, files[i].name);
            if (process_file(in_path, out_path, kernel, k_size, &io, &comp) != 0)
                fprintf(stderr, "[rank %d] greska pri obradi %s\n", rank, files[i].name);
        }

        double local = MPI_Wtime() - t0;
        double max_total = 0.0, max_comp = 0.0;
        // ukupno vrijeme serije = vrijeme NAJSPORIJEG procesa
        MPI_Reduce(&local, &max_total, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        MPI_Reduce(&comp,  &max_comp,  1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

        if (rep >= 0) {
            t_total[rep] = max_total;                // vazi samo na rank 0
            t_compute[rep] = max_comp;
        }
        last_local_total = local;
    }

    // statistika (prosjek + varijansa, kao u main.c)
    double mean = 0.0, mean_c = 0.0, var = 0.0;
    if (rank == 0) {
        for (int i = 0; i < reps; i++) { mean += t_total[i]; mean_c += t_compute[i]; }
        mean /= reps;
        mean_c /= reps;
        for (int i = 0; i < reps; i++) var += (t_total[i] - mean) * (t_total[i] - mean);
        var /= reps;
    }

    // po-procesni pregled (balans opterecenja)
    double *all_t = NULL;
    int *all_n = NULL;
    long *all_b = NULL;
    if (rank == 0) {
        all_t = malloc(nranks * sizeof(double));
        all_n = malloc(nranks * sizeof(int));
        all_b = malloc(nranks * sizeof(long));
    }
    MPI_Gather(&last_local_total, 1, MPI_DOUBLE, all_t, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Gather(&my_count, 1, MPI_INT,    all_n, 1, MPI_INT,    0, MPI_COMM_WORLD);
    MPI_Gather(&my_bytes, 1, MPI_LONG,   all_b, 1, MPI_LONG,   0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("\nRank | fajlova | MB obradjeno | vrijeme zadnje serije [s]\n");
        double tmax = 0.0, tsum = 0.0;
        for (int r = 0; r < nranks; r++) {
            printf("%4d | %7d | %12.1f | %.4f\n", r, all_n[r], all_b[r] / 1048576.0, all_t[r]);
            if (all_t[r] > tmax) tmax = all_t[r];
            tsum += all_t[r];
        }
        printf("\nProsjecno ukupno vrijeme serije:   %.6f s\n", mean);
        printf("  od cega racunanje (najsporiji):  %.6f s\n", mean_c);
        printf("Varijansa:                         %.3e\n", var);
        printf("Neuravnotezenost (max/prosjek):    %.3f\n", tmax / (tsum / nranks));
        // linija za automatsku obradu u skripti
        printf("CSV,%d,%d,%d,%.6f,%.6e,%.6f\n", nranks, nthreads, nfiles, mean, var, mean_c);
        free(all_t); free(all_n); free(all_b);
    }

    free(t_total);
    free(t_compute);
    free(files);
    if (kernel != default_kernel) free(kernel);
    MPI_Finalize();
    return 0;
}