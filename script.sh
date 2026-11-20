#!/bin/bash

# Nazivi ulaznog i izlaznog fajla
IMAGE_IN="data/big1.bmp"
IMAGE_OUT="output.bmp"
CSV_FILE="rezultati.csv"
=
# Provjera da li postoji ulazna slika
if [ ! -f "$IMAGE_IN" ]; then
    echo "Greska: Fajl $IMAGE_IN ne postoji u trenutnom direktorijumu!"
    exit 1
fi

# Priprema CSV fajla sa zaglavljem
# echo "Optimizacija,Niti,Prosjecno_Vrijeme_s,Varijansa" > $CSV_FILE

# Definisemo nizove nivoa optimizacije i flegova
OPT_NAMES=("O0" "O1" "O2" "O3" "O3_SIMD")
OPT_FLAGS=("-O0" "-O1" "-O2" "-O3" "-O3 -march=native")

echo "=================================================="
echo "    POCETAK AUTOMATSKOG TESTINGA I MJERENJA"
echo "=================================================="

# Prolazimo kroz sve nivoe optimizacije
for i in "${!OPT_NAMES[@]}"; do
    NAME="${OPT_NAMES[$i]}"
    FLAGS="${OPT_FLAGS[$i]}"
    EXE="main_${NAME}.exe"

    echo ""
    echo "--> Kompajliranje: gcc -fopenmp $FLAGS main.c -o $EXE"
    gcc -fopenmp $FLAGS main.c -o $EXE -lm

    if [ $? -ne 0 ]; then
        echo "Greska pri kompajliranju za $NAME!"
        continue
    fi

    # Mjerenje za 1 i 2 niti (threads)
    for THREADS in 1 2; do
        echo "    Exec: $NAME sa OMP_NUM_THREADS=$THREADS..."
        
        # Postavljanje broja OpenMP niti
        export OMP_NUM_THREADS=$THREADS

        # Pokretanje programa i hvatanje ispisa
        OUTPUT=$(./$EXE $IMAGE_IN $IMAGE_OUT 3 0 -1 0 -1 5 -1 0 -1 0)

        # Izvlacenje vremena i varijanse iz ispisa programa
        AVG=$(echo "$OUTPUT" | grep "Prosjecno vrijeme" | awk '{print $4}')
        VAR=$(echo "$OUTPUT" | grep "Varijansa" | awk '{print $2}')

        # Upisivanje u CSV fajl
        echo "$NAME,$THREADS,$AVG,$VAR" >> $CSV_FILE
        echo "    -> Prosjek: $AVG s | Varijansa: $VAR"
    done

    # Ciscenje binarnog fajla
    rm -f $EXE
done

echo ""
echo "=================================================="
echo " MJERENJE ZAVRSENO! Rezultati uspesno sacuvani u $CSV_FILE"
echo "=================================================="