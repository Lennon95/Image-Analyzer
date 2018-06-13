#include <stdio.h>
#include <stdlib.h>
#include <string.h>		// Para usar strings
#include <math.h>       // Algumas operacoes matematicas serao necessarias
#include <dirent.h>

// SOIL é a biblioteca para leitura das imagens
#ifndef __APPLE__
#include "SOIL.h"
#endif


#ifdef __APPLE__
#include <GLUT/glut.h>
#include "SOIL/SOIL.h"
#endif


#define USE_QUANTIZATION_32

#ifdef USE_QUANTIZATION_64
#define QUANTIZATION_SIZE   0x40
#endif

#ifdef USE_QUANTIZATION_32
#define QUANTIZATION_SIZE   0x20
#endif

#ifdef USE_QUANTIZATION_16
#define QUANTIZATION_SIZE   0x10
#endif

// Uma imagem em tons de cinza
typedef struct Img
{
    int width, height;
    unsigned char* img;
    char name_id[30];
    char img_class[30];

    unsigned int histogram[16]; // Histograma divido em 16 regiões
    float glcm[QUANTIZATION_SIZE][QUANTIZATION_SIZE]; // MCO de QUANTIZATION_SIZE² elementos

    float mean, median, std_deviation, kurtosis; // Descritores do histograma
    float energy, entropy, contrast, variance, homogeneity; // Descritores da MCO
} Img;

const char* TEST_FNAME = "Teste.csv";
const char* TRAINING_FNAME = "Treino.csv";
const char* IMAGE_DIR = "imagens_teste_treino";

// Protótipos
int load(Img** pic);
void unload(size_t length, Img** pic);
void printImgBytes (Img* pic, int n_bytes);
void printGLCMStats(Img* pic);
void printHistStats(Img* pic);
void buildGLHistogram(Img* pic);
void buildGLCM(Img* pic, int offsetX, int offsetY);
void calcHistStatistics(Img* pic);
void calcGLCMStatistics(Img* pic);
void generateCSVFiles(Img** pic, size_t length);

// Carrega uma imagem para a struct Img
int load(Img** pic)
{
    int chan;
    int offsetX = 1;
    int offsetY = -1;

    int i = 0;

    size_t length = 0;

    DIR* pDir = opendir(IMAGE_DIR);
    struct dirent *pDirent;
    struct dirent *pSubdirent;

    if (!pDir)
    {
        printf("**** Diretorio %s nao encontrado.\n", IMAGE_DIR);
        exit(0);
    }

    while((pDirent = readdir(pDir)) != NULL)
    {
        if (strcmp(pDirent->d_name, "..") == 0 || strcmp(pDirent->d_name, ".") == 0)
            continue;

        char subdir[100];
        memset(subdir, '\0', sizeof(subdir));

        strcpy(subdir, IMAGE_DIR);
        strcat(subdir, "/");
        strcat(subdir, pDirent->d_name);

        DIR* pSubdir = opendir(subdir);
        if (!pSubdir)
        {
            printf("**** Diretorio %s nao encontrado.\n", subdir);
            exit(0);
        }

        for (; (pSubdirent = readdir(pSubdir)) != NULL; i++)
        {
            if (strcmp(pSubdirent->d_name, "..") == 0 || strcmp(pSubdirent->d_name, ".") == 0)
            {
                i--;
                continue;
            }

            char imgname[100];
            memset(imgname, '\0', sizeof(imgname));
            strcpy(imgname, subdir);
            strcat(imgname, "/");
            strcat(imgname, pSubdirent->d_name);

            pic[i] = malloc(sizeof(Img));
            pic[i]->img = SOIL_load_image(imgname, &pic[i]->width, &pic[i]->height, &chan, SOIL_LOAD_AUTO);
            if (!pic[i]->img)
            {
                printf("** SOIL loading error: '%s' \n** Filename: %s\n\n", SOIL_last_result(), imgname);
            }
            else
            {
                memset(pic[i]->name_id, '\0', 30);
                memset(pic[i]->img_class, '\0', 30);
                strcpy(pic[i]->name_id, pSubdirent->d_name);
                strcpy(pic[i]->img_class, pDirent->d_name);

                printf("\n\n*** IMAGEM CARREGADA: %s ***\n", pic[i]->name_id);
                printf("Dimensoes: %d x %d x %d\n\n", pic[i]->width, pic[i]->height, chan);

                printf("Iniciando construcao de histogramas de cinza..\n");
                buildGLHistogram(pic[i]);

                printf("\nIniciando geracao de MCO com vizinhanca (%d, %d)...\n", offsetX, offsetY);
                buildGLCM(pic[i], offsetX, offsetY);
            }
            length = i + 1;
        }
    }
    generateCSVFiles(pic, length);
    return length;
}

void unload(size_t length, Img** pic)
{
    for (int i = 0; i < length; i++)
    {
        free(pic[i]->img);
        free(pic[i]);
    }
}

// Imprimir bytes da imagem
void printImgBytes(Img* pic, int n_bytes)
{
    printf("\nPrimeiros %d bytes da imagem:\n", n_bytes);
    for (int i = 0; i < n_bytes; i++)
    {
        if (i % 16 == 0) printf("\n");
        printf("%02X ", pic->img[i]);
    }
    printf("\n");
}

void printGLCMStats(Img* pic)
{
    printf("\n*** Descritores da MCO ***\n");
    printf("Energia: %f \n", pic->energy);
    printf("Entropia: %f \n", pic->entropy);
    printf("Contraste: %f \n", pic->contrast);
    printf("Variancia: %f \n", pic->variance);
    printf("Homogeneidade: %f \n", pic->homogeneity);
}

void printHistStats(Img* pic)
{
    printf("\n*** Descritores estatisticos do histograma ***\n");
    printf("Media: %0.2f\n", pic->mean);
    printf("Mediana: %0.2f\n", pic->median);
    printf("Curtose: %0.6f\n", pic->kurtosis);
    printf("Desvio padrao: %0.2f\n", pic->std_deviation);
}

// Construir um histograma de niveis de cinza
void buildGLHistogram(Img* pic)
{
    unsigned int imgWidth = pic->width;
    unsigned int imgHeight = pic->height;

    for (int i = 0; i < 16; i ++)
        pic->histogram[i] = 0;

    for (int i = 0; i < imgWidth * imgHeight; i++)
    {
        int index = (int ) floor((float )pic->img[i] / 16);
        pic->histogram[index] ++;
    }

    printf("Calculando descritores estatisticos do histograma..:\n");
    calcHistStatistics(pic);
}

// Construir uma MCO de cinza
void buildGLCM(Img* pic, int offsetX, int offsetY)
{
    unsigned int imgWidth = pic->width;
    unsigned int imgHeight = pic->height;
    unsigned char n_img[imgHeight * imgWidth];

    int i;

    memcpy(n_img, pic->img, imgHeight * imgWidth);

    // Reduzir o numero de tons de cinza
    for (i = 0; i < imgWidth * imgHeight; i++)
        n_img[i] = (n_img[i] / (0x100 / QUANTIZATION_SIZE));

    // Inicializando matriz
    for (i = 0; i < QUANTIZATION_SIZE; i++)
        for (int j = 0; j < QUANTIZATION_SIZE; j++)
            pic->glcm[i][j] = 0.0;

    // Se o offsetY for negativo, o pixel vizinho
    // do pixel de referência esta offsetY-linhas acimas
    i = (offsetY < 0) ? imgWidth * (-1 * offsetY) : 0;

    // Construindo MCO
    for ( ; i < imgWidth * imgHeight; i++)
    {
        if (offsetX < 0 && i % imgWidth == 0)
            i += (-1 * offsetX);
        if (offsetX > 0 && (i + offsetX) % imgWidth == 0)
            i += offsetX;

        int index[2] = {
            (int )n_img[i],
            i + offsetX + (imgWidth * offsetY)
            };

        if (index[1] >= imgWidth * imgHeight)
            break;

        index[1] = n_img[index[1]];
        pic->glcm[index[0]][index[1]] += 1.0;
    }

    printf("MCO gerada.\nNormalizando e gerando descritores estatisticos...\n");
    calcGLCMStatistics(pic);

    printf("MCO normalizada e descritores estatisticos calculados com sucesso.\n");
  //  printGLCMStats(pic);
}

void calcHistStatistics(Img* pic)
{
    int totalFreq = 0;
    int m_accFreq = 0;
    int medianIndex = -1;
    int medianPos = (pic->width * pic->height) / 2;
    int q1_accFreq = 0;
    int q3_accFreq = 0;
    int p10_accFreq = 0;
    int p90_accFreq = 0;
    int q1_index = -1;
    int q3_index = -1;
    int p10_index = -1;
    int p90_index = -1;
    int qPos = (pic->width * pic->height) / 4;
    int p10_Pos = (10 * (pic->width * pic->height)) / 100;
    int p90_Pos = (90 * (pic->width * pic->height)) / 100;

    float sampleVariance = 0.0;
    float Q1 = 0.0;
    float Q3 = 0.0;
    float P10 = 0.0;
    float P90 = 0.0;

    pic->mean = 0.0;
    pic->median = 0.0;
    pic->kurtosis = 0.0;
    pic->std_deviation = 0.0;

    for (int i = 0; i < 16; i++)
    {
        int begin = i * 16;
        int end = ((i + 1) * 16);

        pic->mean += pic->histogram[i] * ((begin + end) / 2);
        sampleVariance += pic->histogram[i] * pow(((begin + end) / 2), 2);
        totalFreq += pic->histogram[i];

        if (totalFreq > medianPos && medianIndex == -1)
        {
            medianIndex = i;
            m_accFreq = totalFreq - pic->histogram[i];
        }

        if (totalFreq > qPos && q1_index == -1)
        {
            q1_index = i;
            q1_accFreq = totalFreq - pic->histogram[i];
        }

        if (totalFreq > (3 * qPos) && q3_index == -1)
        {
            q3_index = i;
            q3_accFreq = totalFreq - pic->histogram[i];
        }

        if (totalFreq > p10_Pos && p10_index == -1)
        {
            p10_index = i;
            p10_accFreq = totalFreq - pic->histogram[i];
        }

        if (totalFreq > p90_Pos && p90_index == -1)
        {
            p90_index = i;
            p90_accFreq = totalFreq - pic->histogram[i];
        }

      //  printf("Index>%02d. Intervalo de classe: %03d |-- %03d: %06d\t   Ponto medio: %d\n",
        //       i, begin, end, pic->histogram[i], (begin + end) /2);
    }

    pic->mean /= totalFreq;
    pic->median = (medianIndex * 16) + ((float )((medianPos - m_accFreq) * 16) / pic->histogram[medianIndex]);
    Q1 = (q1_index * 16) + ((float )((qPos - q1_accFreq) * 16) / pic->histogram[q1_index]);
    Q3 = (q3_index * 16) + ((float )(((3 * qPos) - q3_accFreq) * 16) / pic->histogram[q3_index]);
    P10 = (p10_index * 16) + ((float )((p10_Pos - p10_accFreq) * 16) / pic->histogram[p10_index]);
    P90 = (p90_index * 16) + ((float )((p90_Pos - p90_accFreq) * 16) / pic->histogram[p90_index]);

    // sampleVariance /= totalFreq;
    // sampleVariance -=  pow(pic->mean, 2);
    sampleVariance -=  (totalFreq * pow(pic->mean, 2));
    sampleVariance /= totalFreq - 1;
    pic->std_deviation = sqrt(sampleVariance);
    pic->kurtosis = (Q3 - Q1) / (2 * (P90 - P10));

   // printHistStats(pic);
}

void calcGLCMStatistics(Img* pic)
{
    int biggestVal = 0;

    // Calcular o maior valor da MCO para normalizar
    for (int i = 0; i < QUANTIZATION_SIZE; i++)
        for (int j = 0; j < QUANTIZATION_SIZE; j++)
            biggestVal = (pic->glcm[i][j] > biggestVal) ? pic->glcm[i][j] : biggestVal;

    pic->energy = 0.0;
    pic->entropy = 0.0;
    pic->contrast = 0.0;
    pic->variance = 0.0;
    pic->homogeneity = 0.0;
    for (int i = 0; i < QUANTIZATION_SIZE; i++)
        for (int j = 0; j < QUANTIZATION_SIZE; j++)
        {
            pic->glcm[i][j]     /= biggestVal;
            pic->energy         += pow(pic->glcm[i][j], 2);
            pic->contrast       += (pic->glcm[i][j] * pow((i - j), 2));
            pic->variance       += (pic->glcm[i][j] * (i - j));
            pic->homogeneity    += (pic->glcm[i][j] * (pic->glcm[i][j] / (1 + pow((i - j), 2))));
        }
    pic->entropy = sqrt(pic->energy);
}

void generateCSVFiles(Img** pic, size_t length)
{
    FILE* fTest = fopen(TEST_FNAME, "w+");;
    FILE* fTraining = fopen(TRAINING_FNAME, "w+");

    const char* line = "%s,%f,%f,%f,%f,%f,%f,%f,%f,%f,%s\n";

    int aux = 0;

    fputs("nome,media,mediana,desvio_padrao,curtose,energia,entropia,contraste,variancia,homogeneidade,classe\n", fTest);
    fputs("nome,media,mediana,desvio_padrao,curtose,energia,entropia,contraste,variancia,homogeneidade,classe\n", fTraining);

    printf("**** TOTAL DE IMAGENS: %d\n", length);

    for (int i = 0; i < length; i++)
    {
        if (!pic[i]->img) continue;

        if (aux == 0)
        {
            fprintf(fTest, line, pic[i]->name_id, pic[i]->mean, pic[i]->median, pic[i]->std_deviation, pic[i]->kurtosis,
                pic[i]->energy, pic[i]->entropy, pic[i]->contrast, pic[i]->variance, pic[i]->homogeneity, pic[i]->img_class);
            aux = 1;
        }
        else
        {
            fprintf(fTraining, line, pic[i]->name_id, pic[i]->mean, pic[i]->median, pic[i]->std_deviation, pic[i]->kurtosis,
                pic[i]->energy, pic[i]->entropy, pic[i]->contrast, pic[i]->variance, pic[i]->homogeneity, pic[i]->img_class);
            aux = 0;
        }
    }
    fclose(fTest);
    fclose(fTraining);
}

int main(int argc, char** argv)
{
    Img** pic = malloc(1000 *(sizeof(Img)));
    size_t length;

    if (argc < 1)
    {
        printf("loader [img]\n");
        exit(1);
    }

    length = load(pic);
    unload(length, pic);

    free(pic);
    return 0;
}
