#include <stdint.h>   // uint64_t
#include <stddef.h>   // size_t
#include "cm_ApkVulFuzz.h" // For the populate function

#ifndef _APK_DS_H
#define _APK_DS_H

char *build_output_filename(const char *input_path);
int load_apk_into_mutator(my_mutator_t *data, const char *path);

#endif // _APK_DS_H
