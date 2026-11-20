#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <omp.h>

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
    // NAIVNA VERZIJA: 1 nit, petlje po kolonama (x vanjska, y unutrasnja),
    // provjera granica slike unutar najdublje petlje.
    int offset = k_size / 2;

    for (int x = 0; x < w; x++) {          // vanjska petlja po kolonama
        for (int y = 0; y < h; y++) {      // unutrasnja po redovima -> skokovi od w*3 bajta
            float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f;

            for (int ky = -offset; ky <= offset; ky++) {
                for (int kx = -offset; kx <= offset; kx++) {
                    int ny = y + ky;
                    int nx = x + kx;

                    // provjera granica u najdubljoj petlji
                    if (ny >= 0 && ny < h && nx >= 0 && nx < w) {
                        int pixel_idx = (ny * w + nx) * 3;
                        float k_val = kernel[(ky + offset) * k_size + (kx + offset)];

                        sum_r += input[pixel_idx + 0] * k_val;
                        sum_g += input[pixel_idx + 1] * k_val;
                        sum_b += input[pixel_idx + 2] * k_val;
                    }
                }
            }

            int dest_idx = (y * w + x) * 3;
            output[dest_idx + 0] = (unsigned char)(sum_r > 255.0f ? 255 : (sum_r < 0.0f ? 0 : sum_r));
            output[dest_idx + 1] = (unsigned char)(sum_g > 255.0f ? 255 : (sum_g < 0.0f ? 0 : sum_g));
            output[dest_idx + 2] = (unsigned char)(sum_b > 255.0f ? 255 : (sum_b < 0.0f ? 0 : sum_b));
        }
    }
}

int main(int argc, char *argv[])
{
    const char *ulazni_fajl = "data/image.bmp";
    const char *izlazni_fajl = "output.bmp";
    int k_size = 3;

    // dinamicki alociran kernel
    float default_kernel[9] = {
        0, 0, 0,
        0, 1, 0,
        0, 0, 0
    };
    float *kernel = default_kernel;

    if (argc >= 3) {
        ulazni_fajl = argv[1];
        izlazni_fajl = argv[2];
    }

    // parsiranje dinamickog kernela iz komandne linije
    if (argc >= 4) {
        k_size = atoi(argv[3]);
        if (argc >= 4 + (k_size * k_size)) {
            kernel = malloc(k_size * k_size * sizeof(float));
            for (int i = 0; i < k_size * k_size; i++) {
                kernel[i] = (float)atof(argv[4 + i]);
            }
        }
    }

    int w, h;
    printf("Ucitavam sliku: %s\n", ulazni_fajl);
    unsigned char *input_data = load_bmp(ulazni_fajl, &w, &h);
    if (!input_data) return 1;

    unsigned char *output_data = malloc(w * h * 3);
    if (!output_data) {
        free(input_data);
        if (kernel != default_kernel) free(kernel);
        return 1;
    }

    memcpy(output_data, input_data, w * h * 3);

    // zagrijavanje koda
    printf("Vrsim zagrijavanje (warm-up)...\n");
    for (int i = 0; i < 3; i++) {
        apply_convolution(input_data, output_data, w, h, kernel, k_size);
    }

    // mjerenje vremena i varijanse
    int num_measurements = 5;
    double total_time = 0.0;
    double times[5];

    printf("Pocetak mjerenja (%d iteracija)...\n", num_measurements);
    for (int i = 0; i < num_measurements; i++) {
        double start_time = omp_get_wtime();
        apply_convolution(input_data, output_data, w, h, kernel, k_size);
        double end_time = omp_get_wtime();

        times[i] = end_time - start_time;
        total_time += times[i];
    }

    double prosjek = total_time / num_measurements;

    // Izracunavanje varijanse
    double sum_sq_diff = 0.0;
    for (int i = 0; i < num_measurements; i++) {
        sum_sq_diff += (times[i] - prosjek) * (times[i] - prosjek);
    }
    double varijansa = sum_sq_diff / num_measurements;

    printf("Prosjecno vrijeme izvrsavanja: %f sekundi\n", prosjek);
    printf("Varijansa: %e\n", varijansa);

    // cuvanje rezultata u izlazni fajl
    if (write_bmp(izlazni_fajl, output_data, w, h) != 0) {
        fprintf(stderr, "Greska pri snimanju!\n");
    } else {
        printf("Slika uspesno sacuvana na: %s\n", izlazni_fajl);
    }

    if (kernel != default_kernel) free(kernel);
    free(input_data);
    free(output_data);
    return 0;
}