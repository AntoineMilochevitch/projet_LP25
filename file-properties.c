#include "file-properties.h"

#include <sys/stat.h>
#include <dirent.h>
#include <openssl/evp.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include "defines.h"
#include <fcntl.h>
#include <stdio.h>
#include "utility.h"

/*!
 * @brief get_file_stats gets all of the required information for a file (inc. directories)
 * @param the files list entry
 * You must get:
 * - for files:
 *   - mode (permissions)
 *   - mtime (in nanoseconds)
 *   - size
 *   - entry type (FICHIER)
 *   - MD5 sum
 * - for directories:
 *   - mode
 *   - entry type (DOSSIER)
 * @return -1 in case of error, 0 else
 */
int get_file_stats(files_list_entry_t *entry) {
    struct stat sb;

    if (lstat(path, &sb) == -1) {
        return -1;
    }

    entry->mtime = sb.st_mtim;
    entry->size = sb.st_size;
    entry->mode = sb.st_mode;

    if (S_ISDIR(sb.st_mode)) {
        entry->entry_type = DOSSIER;
    } else if (S_ISREG(sb.st_mode)) {
        entry->entry_type = FICHIER;

        if (compute_file_md5(entry) == -1) {
            return -1;
        }
    } else {
        return -1;
    }

    return 0;
}

/*!
 * @brief compute_file_md5 computes a file's MD5 sum
 * @param the pointer to the files list entry
 * @return -1 in case of error, 0 else
 * Use libcrypto functions from openssl/evp.h
 */
int compute_file_md5(files_list_entry_t *entry) {

    //Ouvre et vérifie si le fichier à été correctement ouvert.
    FILE *file = fopen(entry->path_and_name, "rb");
    if (!file) {
        perror("Impossible d'ouvrir le fichier");
        return -1;
    }

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    const EVP_MD *md = EVP_md5(); // Algorithme MD5 de evp.h

    //Vérifie si les deux lignes du dessus ont réussi.
    if ((!mdctx || !md)||(1 != EVP_DigestInit_ex(mdctx, md, NULL))) {
        fclose(file);
        EVP_MD_CTX_free(mdctx);
        perror("Erreur dans l'initialisation de la somme MD5");
        return -1;
    }

    unsigned char buffer[1024];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), file)) != 0) {
        if (1 != EVP_DigestUpdate(mdctx, buffer, bytes)) {
            printf("%s\n", mdctx);
            fclose(file);
            EVP_MD_CTX_free(mdctx);
            perror("Erreur dans la mise à jour de la somme MD5");
            return -1;
        }
    }
    unsigned int md_len; 
    if (1 != EVP_DigestFinal_ex(mdctx, entry->md5sum, &md_len)) {
        fclose(file);
        EVP_MD_CTX_free(mdctx);
        perror("Erreur dans la finalisation de la somme MD5");
        return -1;
    }

    EVP_MD_CTX_free(mdctx);
    fclose(file);

    return 0;
}

/*!
 * @brief directory_exists tests the existence of a directory
 * @path_to_dir a string with the path to the directory
 * @return true if directory exists, false else
 */
bool directory_exists(char *path_to_dir) {
    struct stat sb;
    if (stat(path_to_dir, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        return true;
    } else {
        return false;
    }
}

/*!
 * @brief is_directory_writable tests if a directory is writable
 * @param path_to_dir the path to the directory to test
 * @return true if dir is writable, false else
 * Hint: try to open a file in write mode in the target directory.
 */
bool is_directory_writable(char *path_to_dir) {
    char test_file[PATH_SIZE];
    if (concat_path(test_file, path_to_dir, "testfile.tmp") == NULL) {
        return false;
    }

    FILE *file = fopen(test_file, "w");
    if (file != NULL) {
        fclose(file);
        remove(test_file);
        return true;
    } else {
        return false;
    }    
}
