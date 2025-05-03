#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <magic.h>
#include <openssl/evp.h>

#define MAX_FILES 10000
#define MAX_PATH 4096
#define MD5_DIGEST_LENGTH 16  // Для совместимости со старым кодом

typedef struct {
    char path[MAX_PATH];
    unsigned char md5[MD5_DIGEST_LENGTH];
    off_t size;
} FileInfo;

FileInfo files[MAX_FILES];
int file_count = 0;
const char **allowed_mime_types = NULL;
int mime_type_count = 0;

// Проверяем, разрешён ли MIME-тип
int is_mime_allowed(const char *mime_type) {
    if (mime_type_count == 0) return 1;
    
    for (int i = 0; i < mime_type_count; i++) {
        if (strcmp(mime_type, allowed_mime_types[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// Получаем MIME-тип файла
char* get_mime_type(magic_t magic_cookie, const char *path) {
    const char *mime_type = magic_file(magic_cookie, path);
    return mime_type ? strdup(mime_type) : NULL;
}

// Вычисляем MD5 хеш файла (современная версия)
void calculate_md5(const char *path, unsigned char *md5_result) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        perror("fopen");
        memset(md5_result, 0, MD5_DIGEST_LENGTH);
        return;
    }

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    const EVP_MD *md = EVP_md5();
    unsigned int md_len;

    if (!EVP_DigestInit_ex(mdctx, md, NULL)) {
        fclose(file);
        EVP_MD_CTX_free(mdctx);
        memset(md5_result, 0, MD5_DIGEST_LENGTH);
        return;
    }

    unsigned char buffer[4096];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) != 0) {
        if (!EVP_DigestUpdate(mdctx, buffer, bytes_read)) {
            fclose(file);
            EVP_MD_CTX_free(mdctx);
            memset(md5_result, 0, MD5_DIGEST_LENGTH);
            return;
        }
    }

    if (!EVP_DigestFinal_ex(mdctx, md5_result, &md_len)) {
        memset(md5_result, 0, MD5_DIGEST_LENGTH);
    }

    EVP_MD_CTX_free(mdctx);
    fclose(file);
}

// Рекурсивно сканируем директорию
void scan_directory(magic_t magic_cookie, const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        perror("opendir");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat stat_buf;
        if (lstat(full_path, &stat_buf) == -1) {
            perror("lstat");
            continue;
        }

        if (S_ISDIR(stat_buf.st_mode)) {
            scan_directory(magic_cookie, full_path);
        } else if (S_ISREG(stat_buf.st_mode)) {
            char *mime_type = get_mime_type(magic_cookie, full_path);
            if (mime_type && is_mime_allowed(mime_type)) {
                if (file_count < MAX_FILES) {
                    strncpy(files[file_count].path, full_path, MAX_PATH);
                    files[file_count].size = stat_buf.st_size;
                    calculate_md5(full_path, files[file_count].md5);
                    file_count++;
                } else {
                    fprintf(stderr, "Достигнуто максимальное количество файлов\n");
                }
            }
            free(mime_type);
        }
    }
    closedir(dir);
}

// Сравниваем MD5 хеши
int compare_md5(const unsigned char *md1, const unsigned char *md2) {
    return memcmp(md1, md2, MD5_DIGEST_LENGTH) == 0;
}

// Находим и выводим дубликаты
void find_and_print_duplicates() {
    int *printed = calloc(file_count, sizeof(int));
    if (!printed) {
        perror("calloc");
        return;
    }

    for (int i = 0; i < file_count; i++) {
        if (printed[i]) continue;

        int first_in_group = 1;
        for (int j = i + 1; j < file_count; j++) {
            if (!printed[j] && 
                files[i].size == files[j].size && 
                compare_md5(files[i].md5, files[j].md5)) {
                
                if (first_in_group) {
                    printf("\nДубликаты (%ld байт):\n", files[i].size);
                    printf("%s\n", files[i].path);
                    first_in_group = 0;
                    printed[i] = 1;
                }
                printf("%s\n", files[j].path);
                printed[j] = 1;
            }
        }
    }
    free(printed);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Использование: %s <директория> [MIME-тип1 MIME-тип2 ...]\n", argv[0]);
        return 1;
    }

    // Инициализация libmagic
    magic_t magic_cookie = magic_open(MAGIC_MIME_TYPE);
    if (!magic_cookie) {
        fprintf(stderr, "Не удалось инициализировать magic\n");
        return 1;
    }
    if (magic_load(magic_cookie, NULL) != 0) {
        fprintf(stderr, "Не удалось загрузить magic базу: %s\n", magic_error(magic_cookie));
        magic_close(magic_cookie);
        return 1;
    }

    // Парсинг MIME-типов
    if (argc > 2) {
        allowed_mime_types = (const char **)&argv[2];
        mime_type_count = argc - 2;
    }

    // Сканирование директории
    scan_directory(magic_cookie, argv[1]);

    // Поиск дубликатов
    find_and_print_duplicates();

    // Очистка
    magic_close(magic_cookie);

    return 0;
}