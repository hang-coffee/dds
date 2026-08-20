#ifndef DCC_C99_PREPROCESSOR_H
#define DCC_C99_PREPROCESSOR_H

typedef struct {
    char **include_dirs;
    int n_include_dirs;
    int cap_include_dirs;
    int hosted;              /* 1 = -fhosted（默认），0 = -ffreestanding */
    char *lib_include_dir;   /* dcc 根目录/lib/{hosted,freestanding}/include */
} PreprocessOptions;

void preprocess_options_init(PreprocessOptions *opt);
void preprocess_options_add_include_dir(PreprocessOptions *opt, const char *dir);
void preprocess_options_free(PreprocessOptions *opt);

typedef struct {
    char *source;
    char **headers;
    int nheaders;
} PreprocessResult;

/* 对 path 指向的源文件做预处理，返回展开后的源码文本（调用者 free）。失败返回 NULL 并设置 *err。 */
char *preprocess_file(const char *path, const PreprocessOptions *opt, char **err);

/* 预处理并记录所有实际包含的头文件绝对/相对路径。返回 result.source 和 result.headers。 */
PreprocessResult preprocess_file_result(const char *path, const PreprocessOptions *opt, char **err);
void preprocess_result_free(PreprocessResult *r);

#endif
