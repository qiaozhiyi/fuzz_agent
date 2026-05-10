#include <stddef.h>

typedef struct afl_state afl_state_t;

#ifdef __cplusplus
extern "C" {
#endif

void *fp_mutator_init(unsigned int seed);
size_t fp_mutator_fuzz(void *data,
                       unsigned char *buf,
                       size_t buf_size,
                       unsigned char **out_buf,
                       unsigned char *add_buf,
                       size_t add_buf_size,
                       size_t max_size);
void fp_mutator_deinit(void *data);
unsigned char fp_mutator_queue_get(void *data, const char *filename);
void fp_mutator_queue_new_entry(void *data, const char *filename_new_queue,
                                const char *filename_orig_queue);

void *afl_custom_init(afl_state_t *afl, unsigned int seed) {
  (void)afl;
  return fp_mutator_init(seed);
}

size_t afl_custom_fuzz(void *data,
                       unsigned char *buf,
                       size_t buf_size,
                       unsigned char **out_buf,
                       unsigned char *add_buf,
                       size_t add_buf_size,
                       size_t max_size) {
  return fp_mutator_fuzz(data, buf, buf_size, out_buf, add_buf, add_buf_size, max_size);
}

void afl_custom_deinit(void *data) {
  fp_mutator_deinit(data);
}

unsigned char afl_custom_queue_get(void *data, const unsigned char *filename) {
  return fp_mutator_queue_get(data, (const char *)filename);
}

void afl_custom_queue_new_entry(void *data,
                                const unsigned char *filename_new_queue,
                                const unsigned char *filename_orig_queue) {
  fp_mutator_queue_new_entry(data, (const char *)filename_new_queue,
                             (const char *)filename_orig_queue);
}

#ifdef __cplusplus
}
#endif

