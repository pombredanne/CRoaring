#include "roaring.h"
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "array_util.h"
#include "roaring_array.h"

roaring_bitmap_t *roaring_bitmap_create() {
    roaring_bitmap_t *ans = (roaring_bitmap_t *)malloc(sizeof(roaring_bitmap_t));
    if (!ans) {
        return NULL;
    }
    ans->high_low_container = ra_create();
    if (!ans->high_low_container) {
        free(ans);
        return NULL;
    }
    ans->copy_on_write = false;
    return ans;
}

roaring_bitmap_t *roaring_bitmap_create_with_capacity(uint32_t cap) {
    roaring_bitmap_t *ans = (roaring_bitmap_t *)malloc(sizeof(roaring_bitmap_t));
    if (!ans) {
        return NULL;
    }
    ans->high_low_container = ra_create_with_capacity(cap);
    if (!ans->high_low_container) {
        free(ans);
        return NULL;
    }
    ans->copy_on_write = false;
    return ans;
}

roaring_bitmap_t *roaring_bitmap_of_ptr(size_t n_args, const uint32_t *vals) {
    // todo: could be greatly optimized
    roaring_bitmap_t *answer = roaring_bitmap_create();
    for (size_t i = 0; i < n_args; i++) {
        roaring_bitmap_add(answer, vals[i]);
    }
    return answer;
}

roaring_bitmap_t *roaring_bitmap_of(size_t n_args, ...) {
    // todo: could be greatly optimized
    roaring_bitmap_t *answer = roaring_bitmap_create();
    va_list ap;
    va_start(ap, n_args);
    for (size_t i = 1; i <= n_args; i++) {
        uint32_t val = va_arg(ap, uint32_t);
        roaring_bitmap_add(answer, val);
    }
    va_end(ap);
    return answer;
}

static inline int32_t minimum(uint32_t a, uint32_t b) { return (a < b) ? a : b; }

roaring_bitmap_t *roaring_bitmap_from_range(uint32_t min, uint32_t max, uint32_t step) {
    if(step == 0)
        return NULL;
    if(max <= min)
        return NULL;
    roaring_bitmap_t *answer = roaring_bitmap_create();
    if(step >= (1<<16)) {
        for(uint32_t value = min ; value < max ; value += step) {
                roaring_bitmap_add(answer, value);
        }
        return answer;
    }
    uint32_t min_tmp = min;
    do {
        uint32_t key = min_tmp>>16;
        uint32_t container_min = min_tmp & 0xFFFF;
        uint32_t container_max = minimum(max-(key<<16),1<<16);
        uint8_t type;
        void *container = container_from_range(&type, container_min, container_max,
                                                          (uint16_t)step);
        ra_append(answer->high_low_container, key, container, type);
        uint32_t gap = container_max - container_min + step - 1;
        min_tmp += gap - (gap % step);
    } while(min_tmp < max);
    // cardinality of bitmap will be ((uint64_t) max - min + step - 1 ) / step
    return answer;
}

void roaring_bitmap_printf(const roaring_bitmap_t *ra) {
    printf("{");
    for (int i = 0; i < ra->high_low_container->size; ++i) {
        container_printf_as_uint32_array(
            ra->high_low_container->containers[i],
            ra->high_low_container->typecodes[i],
            ((uint32_t)ra->high_low_container->keys[i]) << 16);
        if (i + 1 < ra->high_low_container->size) printf(",");
    }
    printf("}");
}

void roaring_bitmap_printf_describe(const roaring_bitmap_t *ra) {
    printf("{");
    for (int i = 0; i < ra->high_low_container->size; ++i) {
    	printf("%d: %s (%d)",ra->high_low_container->keys[i],
    			get_full_container_name(ra->high_low_container->containers[i],
    			ra->high_low_container->typecodes[i]),
    			container_get_cardinality(ra->high_low_container->containers[i],
    		            ra->high_low_container->typecodes[i]));
        if(ra->high_low_container->typecodes[i] == SHARED_CONTAINER_TYPE_CODE) {
          printf("(shared count = %d )",((shared_container_t*)(ra->high_low_container->containers[i]))->counter);
        }

        if (i + 1 < ra->high_low_container->size) printf(", ");
    }
    printf("}");
}

roaring_bitmap_t *roaring_bitmap_copy(const roaring_bitmap_t *r) {
    roaring_bitmap_t *ans = (roaring_bitmap_t *)malloc(sizeof(roaring_bitmap_t));
    if (!ans) {
        return NULL;
    }
    ans->high_low_container = ra_copy(r->high_low_container,r->copy_on_write);
    if (!ans->high_low_container) {
        free(ans);
        return NULL;
    }
    ans->copy_on_write = r->copy_on_write;
    return ans;
}

static void roaring_bitmap_overwrite(roaring_bitmap_t *dest,
                              const roaring_bitmap_t *src) {
    ra_free(dest->high_low_container);
    dest->high_low_container = ra_copy(src->high_low_container,src->copy_on_write);
    // TODO any better error handling
    assert(dest->high_low_container);
}

void roaring_bitmap_free(roaring_bitmap_t *r) {
    ra_free(r->high_low_container);
    r->high_low_container = NULL;  // paranoid
    free(r);
}

void roaring_bitmap_add(roaring_bitmap_t *r, uint32_t val) {
    const uint16_t hb = val >> 16;
    const int i = ra_get_index(r->high_low_container, hb);
    uint8_t typecode;
    if (i >= 0) {
    	ra_unshare_container_at_index(r->high_low_container, i);
        void *container =
            ra_get_container_at_index(r->high_low_container, i, &typecode);
        uint8_t newtypecode = typecode;
        void *container2 =
            container_add(container, val & 0xFFFF, typecode, &newtypecode);
        if (container2 != container) {
            container_free(container, typecode);
            ra_set_container_at_index(r->high_low_container, i, container2,
                                      newtypecode);
        }
    } else {
        array_container_t *newac = array_container_create();
        void *container = container_add(newac, val & 0xFFFF,
                                        ARRAY_CONTAINER_TYPE_CODE, &typecode);
        // we could just assume that it stays an array container
        ra_insert_new_key_value_at(r->high_low_container, -i - 1, hb, container,
                                   typecode);
    }
}

bool roaring_bitmap_contains(const roaring_bitmap_t *r, uint32_t val) {
    const uint16_t hb = val >> 16;
    const int i = ra_get_index(r->high_low_container, hb);
    uint8_t typecode;
    if (i >= 0) {
        void *container =
            ra_get_container_at_index(r->high_low_container, i, &typecode);
        return container_contains(container, val & 0xFFFF, typecode);
    } else {
        return false;
    }
}

// there should be some SIMD optimizations possible here
roaring_bitmap_t *roaring_bitmap_and(const roaring_bitmap_t *x1,
                                     const roaring_bitmap_t *x2) {
    uint8_t container_result_type = 0;
    const int length1 = x1->high_low_container->size,
              length2 = x2->high_low_container->size;
    uint32_t neededcap = length1 > length2 ? length2 : length1;
    roaring_bitmap_t *answer = roaring_bitmap_create_with_capacity(neededcap);
    answer->copy_on_write = x1->copy_on_write && x2->copy_on_write;

    int pos1 = 0, pos2 = 0;

    while (pos1 < length1 && pos2 < length2) {
        const uint16_t s1 = ra_get_key_at_index(x1->high_low_container, pos1);
        const uint16_t s2 = ra_get_key_at_index(x2->high_low_container, pos2);

        if (s1 == s2) {
            uint8_t container_type_1, container_type_2;
            void *c1 = ra_get_container_at_index(x1->high_low_container, pos1,
                                                 &container_type_1);
            void *c2 = ra_get_container_at_index(x2->high_low_container, pos2,
                                                 &container_type_2);
            void *c = container_and(c1, container_type_1, c2, container_type_2,
                                    &container_result_type);
            if (container_nonzero_cardinality(c, container_result_type)) {
                ra_append(answer->high_low_container, s1, c,
                          container_result_type);
            } else {
                container_free(
                    c, container_result_type);  // otherwise:memory leak!
            }
            ++pos1;
            ++pos2;
        } else if (s1 < s2) {  // s1 < s2
            pos1 = ra_advance_until(x1->high_low_container, s2, pos1);
        } else {  // s1 > s2
            pos2 = ra_advance_until(x2->high_low_container, s1, pos2);
        }
    }
    return answer;
}

/**
 * Compute the union of 'number' bitmaps.
 */
roaring_bitmap_t *roaring_bitmap_or_many(size_t number,
                                         const roaring_bitmap_t **x) {
    if (number == 0) {
        return roaring_bitmap_create();
    }
    if (number == 1) {
        return roaring_bitmap_copy(x[0]);
    }
    roaring_bitmap_t *answer = roaring_bitmap_lazy_or(x[0], x[1]);
    for (size_t i = 2; i < number; i++) {
        roaring_bitmap_lazy_or_inplace(answer, x[i]);
    }
    roaring_bitmap_repair_after_lazy(answer);
    return answer;
}

// inplace and (modifies its first argument).
void roaring_bitmap_and_inplace(roaring_bitmap_t *x1,
                                const roaring_bitmap_t *x2) {
    int pos1 = 0, pos2 = 0, intersection_size = 0;
    const int length1 = ra_get_size(x1->high_low_container);
    const int length2 = ra_get_size(x2->high_low_container);

    // any skipped-over or newly emptied containers in x1
    // have to be freed.
    while (pos1 < length1 && pos2 < length2) {
        const uint16_t s1 = ra_get_key_at_index(x1->high_low_container, pos1);
        const uint16_t s2 = ra_get_key_at_index(x2->high_low_container, pos2);

        if (s1 == s2) {
            uint8_t typecode1, typecode2, typecode_result;
            void *c1 = ra_get_container_at_index(x1->high_low_container, pos1,
                                                 &typecode1);
            c1 = get_writable_copy_if_shared(c1,&typecode1);
            void *c2 = ra_get_container_at_index(x2->high_low_container, pos2,
                                                 &typecode2);
            void *c =
                container_iand(c1, typecode1, c2, typecode2, &typecode_result);
            if (c != c1) {  // in this instance a new container was created, and
                            // we need to free the old one
                container_free(c1, typecode1);
            }
            if (container_nonzero_cardinality(c, typecode_result)) {
                ra_replace_key_and_container_at_index(x1->high_low_container,
                                                      intersection_size, s1, c,
                                                      typecode_result);
                intersection_size++;
            } else {
                container_free(c, typecode_result);
            }
            ++pos1;
            ++pos2;
        } else if (s1 < s2) {
            pos1 = ra_advance_until_freeing(x1->high_low_container, s2, pos1);
        } else {  // s1 > s2
            pos2 = ra_advance_until(x2->high_low_container, s1, pos2);
        }
    }

    // if we ended early because x2 ran out, then all remaining in x1 should be
    // freed
    while (pos1 < length1) {
        container_free(x1->high_low_container->containers[pos1],
                       x1->high_low_container->typecodes[pos1]);
        ++pos1;
    }

    // all containers after this have either been copied or freed
    ra_downsize(x1->high_low_container, intersection_size);
}

roaring_bitmap_t *roaring_bitmap_or(const roaring_bitmap_t *x1,
                                    const roaring_bitmap_t *x2) {
    uint8_t container_result_type = 0;
    const int length1 = x1->high_low_container->size,
              length2 = x2->high_low_container->size;
    roaring_bitmap_t *answer =
        roaring_bitmap_create_with_capacity(length1 + length2);
    answer->copy_on_write = x1->copy_on_write && x2->copy_on_write;
    if (0 == length1) {
        return roaring_bitmap_copy(x2);
    }
    if (0 == length2) {
        return roaring_bitmap_copy(x1);
    }
    int pos1 = 0, pos2 = 0;
    uint8_t container_type_1, container_type_2;
    uint16_t s1 = ra_get_key_at_index(x1->high_low_container, pos1);
    uint16_t s2 = ra_get_key_at_index(x2->high_low_container, pos2);
    while (true) {
        if (s1 == s2) {
            void *c1 = ra_get_container_at_index(x1->high_low_container, pos1,
                                                 &container_type_1);
            void *c2 = ra_get_container_at_index(x2->high_low_container, pos2,
                                                 &container_type_2);
            void *c = container_or(c1, container_type_1, c2, container_type_2,
                                   &container_result_type);
            // since we assume that the initial containers are non-empty, the
            // result here
            // can only be non-empty
            ra_append(answer->high_low_container, s1, c, container_result_type);
            ++pos1;
            ++pos2;
            if (pos1 == length1) break;
            if (pos2 == length2) break;
            s1 = ra_get_key_at_index(x1->high_low_container, pos1);
            s2 = ra_get_key_at_index(x2->high_low_container, pos2);

        } else if (s1 < s2) {  // s1 < s2
            void *c1 = ra_get_container_at_index(x1->high_low_container, pos1,
                                                 &container_type_1);
            //c1 = container_clone(c1, container_type_1);
            c1 = get_copy_of_container(c1, & container_type_1,x1->copy_on_write);
            if(x1->copy_on_write) {
              ra_set_container_at_index(x1->high_low_container, pos1, c1, container_type_1);
            }
            ra_append(answer->high_low_container, s1, c1, container_type_1);
            pos1++;
            if (pos1 == length1) break;
            s1 = ra_get_key_at_index(x1->high_low_container, pos1);

        } else {  // s1 > s2
        	void *c2 = ra_get_container_at_index(x2->high_low_container, pos2,
                                                 &container_type_2);
            //c2 = container_clone(c2, container_type_2);
            c2 = get_copy_of_container(c2, &container_type_2,x2->copy_on_write);
            if(x2->copy_on_write) {
              ra_set_container_at_index(x2->high_low_container, pos2, c2, container_type_2);
            }
            ra_append(answer->high_low_container, s2, c2, container_type_2);
            pos2++;
            if (pos2 == length2) break;
            s2 = ra_get_key_at_index(x2->high_low_container, pos2);
        }
    }
    if (pos1 == length1) {
        ra_append_copy_range(answer->high_low_container, x2->high_low_container,
                             pos2, length2, x2->copy_on_write);
    } else if (pos2 == length2) {
        ra_append_copy_range(answer->high_low_container, x1->high_low_container,
                             pos1, length1, x1->copy_on_write);
    }
    return answer;
}

// inplace or (modifies its first argument).
void roaring_bitmap_or_inplace(roaring_bitmap_t *x1,
                               const roaring_bitmap_t *x2) {
    uint8_t container_result_type = 0;
    int length1 = x1->high_low_container->size;
    const int length2 = x2->high_low_container->size;

    if (0 == length2) return;

    if (0 == length1) {
        roaring_bitmap_overwrite(x1, x2);
        return;
    }
    int pos1 = 0, pos2 = 0;
    uint8_t container_type_1, container_type_2;
    uint16_t s1 = ra_get_key_at_index(x1->high_low_container, pos1);
    uint16_t s2 = ra_get_key_at_index(x2->high_low_container, pos2);
    while (true) {
        if (s1 == s2) {
            void *c1 = ra_get_container_at_index(x1->high_low_container, pos1,
                                                 &container_type_1);
            c1 = get_writable_copy_if_shared(c1,&container_type_1);

            void *c2 = ra_get_container_at_index(x2->high_low_container, pos2,
                                                 &container_type_2);
            void *c = container_ior(c1, container_type_1, c2, container_type_2,
                                    &container_result_type);
            if (c != c1) {  // in this instance a new container was created, and
                            // we need to free the old one
                container_free(c1, container_type_1);
            }

            ra_set_container_at_index(x1->high_low_container, pos1, c,
                                      container_result_type);
            ++pos1;
            ++pos2;
            if (pos1 == length1) break;
            if (pos2 == length2) break;
            s1 = ra_get_key_at_index(x1->high_low_container, pos1);
            s2 = ra_get_key_at_index(x2->high_low_container, pos2);

        } else if (s1 < s2) {  // s1 < s2
            pos1++;
            if (pos1 == length1) break;
            s1 = ra_get_key_at_index(x1->high_low_container, pos1);

        } else {  // s1 > s2
            void *c2 = ra_get_container_at_index(x2->high_low_container, pos2,
                                                 &container_type_2);
            c2 = get_copy_of_container(c2,  &container_type_2,x2->copy_on_write);
            if(x2->copy_on_write) {
              ra_set_container_at_index(x2->high_low_container, pos2, c2, container_type_2);
            }

            //void *c2_clone = container_clone(c2, container_type_2);
            ra_insert_new_key_value_at(x1->high_low_container, pos1, s2,
                                       c2, container_type_2);
            pos1++;
            length1++;
            pos2++;
            if (pos2 == length2) break;
            s2 = ra_get_key_at_index(x2->high_low_container, pos2);
        }
    }
    if (pos1 == length1) {
        ra_append_copy_range(x1->high_low_container, x2->high_low_container,
                             pos2, length2, x2->copy_on_write);
    }
}

uint64_t roaring_bitmap_get_cardinality(const roaring_bitmap_t *ra) {
    uint64_t card = 0;
    for (int i = 0; i < ra->high_low_container->size; ++i)
        card += container_get_cardinality(ra->high_low_container->containers[i],
                                          ra->high_low_container->typecodes[i]);
    return card;
}

uint32_t *roaring_bitmap_to_uint32_array(const roaring_bitmap_t *ra,
                                         uint32_t *cardinality) {
    uint32_t card1 = roaring_bitmap_get_cardinality(ra);

    uint32_t *ans = malloc((card1 + 10) * sizeof(uint32_t));  //+20??
    // TODO Valgrind reports we write beyond the end of this array (?) with an
    // 8-byte write (?)
    // but it may just be an AVX2 instruction needing a little extra space.  Add
    // 40 bytes...seems to fix problem, but adding 8 didn't
    uint32_t ctr = 0;

    for (int i = 0; i < ra->high_low_container->size; ++i) {
        int num_added = container_to_uint32_array(
            ans + ctr, ra->high_low_container->containers[i],
            ra->high_low_container->typecodes[i],
            ((uint32_t)ra->high_low_container->keys[i]) << 16);
        ctr += num_added;
    }
    assert(ctr == card1);
    *cardinality = ctr;
    return ans;
}

/** convert array and bitmap containers to run containers when it is more
 * efficient;
 * also convert from run containers when more space efficient.  Returns
 * true if the result has at least one run container.
*/
bool roaring_bitmap_run_optimize(roaring_bitmap_t *r) {
    bool answer = false;
    for (int i = 0; i < r->high_low_container->size; i++) {
    	uint8_t typecode_original, typecode_after;
    	ra_unshare_container_at_index(r->high_low_container, i);//TODO: this introduces extra cloning!
        void *c = ra_get_container_at_index(r->high_low_container, i,
                                            &typecode_original);
        void *c1 = convert_run_optimize(c, typecode_original, &typecode_after);
        if (typecode_after == RUN_CONTAINER_TYPE_CODE) answer = true;
        ra_set_container_at_index(r->high_low_container, i, c1, typecode_after);
    }
    return answer;
}

/**
 *  Remove run-length encoding even when it is more space efficient
 *  return whether a change was applied
 */
bool roaring_bitmap_remove_run_compression(roaring_bitmap_t *r) {
    bool answer = false;
    for (int i = 0; i < r->high_low_container->size; i++) {
        uint8_t typecode_original, typecode_after;
        void *c = ra_get_container_at_index(r->high_low_container, i,
                                            &typecode_original);
        if (get_container_type(c,typecode_original) == RUN_CONTAINER_TYPE_CODE) {
        	answer = true;
			if (typecode_original == SHARED_CONTAINER_TYPE_CODE) {
				void* truec = ((shared_container_t *) c)->container;
				int32_t card = run_container_cardinality(truec);
				void *c1 = convert_to_bitset_or_array_container(truec, card,
						&typecode_after);
				shared_container_free(c);
				ra_set_container_at_index(r->high_low_container, i, c1,
						typecode_after);

			} else {
				int32_t card = run_container_cardinality(c);
				void *c1 = convert_to_bitset_or_array_container(c, card,
						&typecode_after);
				ra_set_container_at_index(r->high_low_container, i, c1,
						typecode_after);
			}
        }
    }
    return answer;
}

char *roaring_bitmap_serialize(roaring_bitmap_t *ra, uint32_t *serialize_len) {
    uint8_t retry_with_array;
    char *ret =
        ra_serialize(ra->high_low_container, serialize_len, &retry_with_array);

    if (retry_with_array) {
        /*
           In this case, space-wise, it's more efficient to represent the bitmap
           as an array of uint32_t rather than a serialized bitmap.
        */
        free(ret);
        uint32_t cardinality;
        unsigned char *a =
            (unsigned char *)roaring_bitmap_to_uint32_array(ra, &cardinality);

        /*
           In roaring_bitmap_to_uint32_array() the allocated memory is more than
           the necessary amount. So we shift data of one byte to mark it as
           a "non-standard serialization" instead of reallocating all the memory
        */
        *serialize_len = 1 + cardinality * sizeof(uint32_t);
        memmove(&a[1], a, *serialize_len);
        a[0] = SERIALIZATION_ARRAY_UINT32, *serialize_len += 1;
        return ((char *)a);
    } else
        return (ret);
}

size_t roaring_bitmap_portable_size_in_bytes(const roaring_bitmap_t *ra) {
    return ra_portable_size_in_bytes(ra->high_low_container);
}

roaring_bitmap_t *roaring_bitmap_portable_deserialize(const char *buf) {
    roaring_bitmap_t *ans = (roaring_bitmap_t *)malloc(sizeof(roaring_bitmap_t));
    if (ans == NULL) {
        return NULL;
    }
    ans->high_low_container =
        ra_portable_deserialize(buf);  // todo: handle the case where it is NULL
    ans->copy_on_write = false;
    return ans;
}

size_t roaring_bitmap_portable_serialize(const roaring_bitmap_t *ra,
                                         char *buf) {
    return ra_portable_serialize(ra->high_low_container, buf);
}

roaring_bitmap_t *roaring_bitmap_deserialize(const void *buf,
                                             uint32_t buf_len) {
    roaring_bitmap_t *b;

    if (buf_len < 4) return (NULL);

    if (*(const unsigned char *)buf == SERIALIZATION_ARRAY_UINT32) {
        /* This looks like a compressed set of uint32_t elements */
        uint32_t i, card = (buf_len - 1) / sizeof(uint32_t);
        const uint32_t *elems = (const uint32_t *)((const char *)buf + 1);

        b = roaring_bitmap_create();

        for (i = 0; i < card; i++) {
            uint32_t val;
            memcpy(&val, elems + i, sizeof(val));
            roaring_bitmap_add(b, val);
        }
        return (b);
    } else if (*(const unsigned char *)buf == SERIALIZATION_CONTAINER) {
        uint32_t len;

        memcpy(&len, &((const unsigned char *)buf)[1], 4);

        if (len != buf_len) return (NULL);

        b = (roaring_bitmap_t *)malloc(sizeof(roaring_bitmap_t));
        if (b) {
            b->high_low_container =
                ra_deserialize((const char *)buf + 5, buf_len - 5);
            if (b->high_low_container == NULL) {
                free(b);
                b = NULL;
            }
        }
        b->copy_on_write = false;

        return (b);
    } else
      return(NULL);
}

void roaring_iterate(roaring_bitmap_t *ra, roaring_iterator iterator,
                     void *ptr) {
    for (int i = 0; i < ra->high_low_container->size; ++i)
        container_iterate(ra->high_low_container->containers[i],
                          ra->high_low_container->typecodes[i],
                          ((uint32_t)ra->high_low_container->keys[i]) << 16,
                          iterator, ptr);
}

bool roaring_bitmap_equals(roaring_bitmap_t *ra1, roaring_bitmap_t *ra2) {
    if (ra1->high_low_container->size != ra2->high_low_container->size) {
        return false;
    }
    for (int i = 0; i < ra1->high_low_container->size; ++i) {
        if (ra1->high_low_container->keys[i] !=
            ra2->high_low_container->keys[i]) {
            return false;
        }
    }
    for (int i = 0; i < ra1->high_low_container->size; ++i) {
        bool areequal = container_equals(ra1->high_low_container->containers[i],
                                         ra1->high_low_container->typecodes[i],
                                         ra2->high_low_container->containers[i],
                                         ra2->high_low_container->typecodes[i]);
        if (!areequal) {
            return false;
        }
    }
    return true;
}

static void insert_flipped_container(roaring_array_t *ans_arr,
                                     roaring_array_t *x1_arr, uint16_t hb,
                                     uint16_t lb_start, uint16_t lb_end) {
    const int i = ra_get_index(x1_arr, hb);
    const int j = ra_get_index(ans_arr, hb);
    uint8_t ctype_in, ctype_out;
    void *flipped_container = NULL;
    if (i >= 0) {
        void *container_to_flip =
            ra_get_container_at_index(x1_arr, i, &ctype_in);
        flipped_container =
            container_not_range(container_to_flip, ctype_in, (uint32_t)lb_start,
                                (uint32_t)(lb_end + 1), &ctype_out);

        if (container_get_cardinality(flipped_container, ctype_out))
            ra_insert_new_key_value_at(ans_arr, -j - 1, hb, flipped_container,
                                       ctype_out);
    } else {
        flipped_container = container_range_of_ones(
            (uint32_t)lb_start, (uint32_t)(lb_end + 1), &ctype_out);
        ra_insert_new_key_value_at(ans_arr, -j - 1, hb, flipped_container,
                                   ctype_out);
    }
}

static void inplace_flip_container(roaring_array_t *x1_arr, uint16_t hb,
                                   uint16_t lb_start, uint16_t lb_end) {
    const int i = ra_get_index(x1_arr, hb);
    uint8_t ctype_in, ctype_out;
    void *flipped_container = NULL;
    if (i >= 0) {
        void *container_to_flip =
            ra_get_container_at_index(x1_arr, i, &ctype_in);
        flipped_container = container_inot_range(
            container_to_flip, ctype_in, (uint32_t)lb_start,
            (uint32_t)(lb_end + 1), &ctype_out);
        // if a new container was created, the old one was already freed
        if (container_get_cardinality(flipped_container, ctype_out)) {
            ra_set_container_at_index(x1_arr, i, flipped_container, ctype_out);
        } else {
            container_free(flipped_container, ctype_out);
            ra_remove_at_index(x1_arr, i);
        }

    } else {
        flipped_container = container_range_of_ones(
            (uint32_t)lb_start, (uint32_t)(lb_end + 1), &ctype_out);
        ra_insert_new_key_value_at(x1_arr, -i - 1, hb, flipped_container,
                                   ctype_out);
    }
}

static void insert_fully_flipped_container(roaring_array_t *ans_arr,
                                           roaring_array_t *x1_arr,
                                           uint16_t hb) {
    const int i = ra_get_index(x1_arr, hb);
    const int j = ra_get_index(ans_arr, hb);
    uint8_t ctype_in, ctype_out;
    void *flipped_container = NULL;
    if (i >= 0) {
        void *container_to_flip =
            ra_get_container_at_index(x1_arr, i, &ctype_in);
        flipped_container =
            container_not(container_to_flip, ctype_in, &ctype_out);
        if (container_get_cardinality(flipped_container, ctype_out))
            ra_insert_new_key_value_at(ans_arr, -j - 1, hb, flipped_container,
                                       ctype_out);
    } else {
        flipped_container = container_range_of_ones(0U, 0x10000U, &ctype_out);
        ra_insert_new_key_value_at(ans_arr, -j - 1, hb, flipped_container,
                                   ctype_out);
    }
}

static void inplace_fully_flip_container(roaring_array_t *x1_arr, uint16_t hb) {
    const int i = ra_get_index(x1_arr, hb);
    uint8_t ctype_in, ctype_out;
    void *flipped_container = NULL;
    if (i >= 0) {
        void *container_to_flip =
            ra_get_container_at_index(x1_arr, i, &ctype_in);
        flipped_container =
            container_inot(container_to_flip, ctype_in, &ctype_out);

        if (container_get_cardinality(flipped_container, ctype_out)) {
            ra_set_container_at_index(x1_arr, i, flipped_container, ctype_out);
        } else {
            container_free(flipped_container, ctype_out);
            ra_remove_at_index(x1_arr, i);
        }

    } else {
        flipped_container = container_range_of_ones(0U, 0x10000U, &ctype_out);
        ra_insert_new_key_value_at(x1_arr, -i - 1, hb, flipped_container,
                                   ctype_out);
    }
}

roaring_bitmap_t *roaring_bitmap_flip(const roaring_bitmap_t *x1,
                                      uint64_t range_start,
                                      uint64_t range_end) {
    if (range_start >= range_end) {
        return roaring_bitmap_copy(x1);
    }

    roaring_bitmap_t *ans = roaring_bitmap_create();
    ans->copy_on_write = x1->copy_on_write;

    uint16_t hb_start = (uint16_t)(range_start >> 16);
    const uint16_t lb_start = (uint16_t)range_start;  // & 0xFFFF;
    uint16_t hb_end = (uint16_t)((range_end - 1) >> 16);
    const uint16_t lb_end = (uint16_t)(range_end - 1);  // & 0xFFFF;

    ra_append_copies_until(ans->high_low_container, x1->high_low_container,
                           hb_start, x1->copy_on_write);
    if (hb_start == hb_end) {
        insert_flipped_container(ans->high_low_container,
                                 x1->high_low_container, hb_start, lb_start,
                                 lb_end);
    } else {
        // start and end containers are distinct
        if (lb_start > 0) {
            // handle first (partial) container
            insert_flipped_container(ans->high_low_container,
                                     x1->high_low_container, hb_start, lb_start,
                                     0xFFFF);
            ++hb_start;  // for the full containers.  Can't wrap.
        }

        if (lb_end != 0xFFFF) --hb_end;  // later we'll handle the partial block

        for (uint16_t hb = hb_start; hb <= hb_end; ++hb) {
            insert_fully_flipped_container(ans->high_low_container,
                                           x1->high_low_container, hb);
        }

        // handle a partial final container
        if (lb_end != 0xFFFF) {
            insert_flipped_container(ans->high_low_container,
                                     x1->high_low_container, hb_end + 1, 0,
                                     lb_end);
            ++hb_end;
        }
    }
    ra_append_copies_after(ans->high_low_container, x1->high_low_container,
                           hb_end, x1->copy_on_write);
    return ans;
}

void roaring_bitmap_flip_inplace(roaring_bitmap_t *x1, uint64_t range_start,
                                 uint64_t range_end) {
    if (range_start >= range_end) {
        return;  // empty range
    }

    uint16_t hb_start = (uint16_t)(range_start >> 16);
    const uint16_t lb_start = (uint16_t)range_start;
    uint16_t hb_end = (uint16_t)((range_end - 1) >> 16);
    const uint16_t lb_end = (uint16_t)(range_end - 1);

    if (hb_start == hb_end) {
        inplace_flip_container(x1->high_low_container, hb_start, lb_start,
                               lb_end);
    } else {
        // start and end containers are distinct
        if (lb_start > 0) {
            // handle first (partial) container
            inplace_flip_container(x1->high_low_container, hb_start, lb_start,
                                   0xFFFF);
            ++hb_start;  // for the full containers.  Can't wrap.
        }

        if (lb_end != 0xFFFF) --hb_end;

        for (uint16_t hb = hb_start; hb <= hb_end; ++hb) {
            inplace_fully_flip_container(x1->high_low_container, hb);
        }

        // handle a partial final container
        if (lb_end != 0xFFFF) {
            inplace_flip_container(x1->high_low_container, hb_end + 1, 0,
                                   lb_end);
            ++hb_end;
        }
    }
}

roaring_bitmap_t *roaring_bitmap_lazy_or(const roaring_bitmap_t *x1,
                                         const roaring_bitmap_t *x2) {
    uint8_t container_result_type = 0;
    const int length1 = x1->high_low_container->size,
              length2 = x2->high_low_container->size;
    roaring_bitmap_t *answer =
        roaring_bitmap_create_with_capacity(length1 + length2);
    answer->copy_on_write = x1->copy_on_write && x2->copy_on_write;
    if (0 == length1) {
        return roaring_bitmap_copy(x2);
    }
    if (0 == length2) {
        return roaring_bitmap_copy(x1);
    }
    int pos1 = 0, pos2 = 0;
    uint8_t container_type_1, container_type_2;
    uint16_t s1 = ra_get_key_at_index(x1->high_low_container, pos1);
    uint16_t s2 = ra_get_key_at_index(x2->high_low_container, pos2);
    while (true) {
        if (s1 == s2) {
            void *c1 = ra_get_container_at_index(x1->high_low_container, pos1,
                                                 &container_type_1);
            void *c2 = ra_get_container_at_index(x2->high_low_container, pos2,
                                                 &container_type_2);
            void *c =
                container_lazy_or(c1, container_type_1, c2, container_type_2,
                                  &container_result_type);
            // since we assume that the initial containers are non-empty,
            // the
            // result here
            // can only be non-empty
            ra_append(answer->high_low_container, s1, c, container_result_type);
            ++pos1;
            ++pos2;
            if (pos1 == length1) break;
            if (pos2 == length2) break;
            s1 = ra_get_key_at_index(x1->high_low_container, pos1);
            s2 = ra_get_key_at_index(x2->high_low_container, pos2);

        } else if (s1 < s2) {  // s1 < s2
            void *c1 = ra_get_container_at_index(x1->high_low_container, pos1,
                                                 &container_type_1);
            c1 = get_copy_of_container(c1, &container_type_1,x1->copy_on_write);
            if(x1->copy_on_write) {
              ra_set_container_at_index(x1->high_low_container, pos1, c1, container_type_1);
            }
            ra_append(answer->high_low_container, s1, c1, container_type_1);
            pos1++;
            if (pos1 == length1) break;
            s1 = ra_get_key_at_index(x1->high_low_container, pos1);

        } else {  // s1 > s2
            void *c2 = ra_get_container_at_index(x2->high_low_container, pos2,
                                                 &container_type_2);
            c2 = get_copy_of_container(c2, &container_type_2,x2->copy_on_write);
            if(x2->copy_on_write) {
              ra_set_container_at_index(x2->high_low_container, pos2, c2, container_type_2);
            }
            ra_append(answer->high_low_container, s2, c2, container_type_2);
            pos2++;
            if (pos2 == length2) break;
            s2 = ra_get_key_at_index(x2->high_low_container, pos2);
        }
    }
    if (pos1 == length1) {
        ra_append_copy_range(answer->high_low_container, x2->high_low_container,
                             pos2, length2, x2->copy_on_write);
    } else if (pos2 == length2) {
        ra_append_copy_range(answer->high_low_container, x1->high_low_container,
                             pos1, length1, x1->copy_on_write);
    }
    return answer;
}

void roaring_bitmap_lazy_or_inplace(roaring_bitmap_t *x1,
                                    const roaring_bitmap_t *x2) {
    uint8_t container_result_type = 0;
    int length1 = x1->high_low_container->size;
    const int length2 = x2->high_low_container->size;

    if (0 == length2) return;

    if (0 == length1) {
        roaring_bitmap_overwrite(x1, x2);
        return;
    }
    int pos1 = 0, pos2 = 0;
    uint8_t container_type_1, container_type_2;
    uint16_t s1 = ra_get_key_at_index(x1->high_low_container, pos1);
    uint16_t s2 = ra_get_key_at_index(x2->high_low_container, pos2);
    while (true) {
        if (s1 == s2) {
            void *c1 = ra_get_container_at_index(x1->high_low_container, pos1,
                                                 &container_type_1);
            c1 = get_writable_copy_if_shared(c1,&container_type_1);
            void *c2 = ra_get_container_at_index(x2->high_low_container, pos2,
                                                 &container_type_2);
            void *c =
                container_lazy_ior(c1, container_type_1, c2, container_type_2,
                                   &container_result_type);
            if (c != c1) {  // in this instance a new container was created, and
                            // we need to free the old one
                container_free(c1, container_type_1);
            }

            ra_set_container_at_index(x1->high_low_container, pos1, c,
                                      container_result_type);
            ++pos1;
            ++pos2;
            if (pos1 == length1) break;
            if (pos2 == length2) break;
            s1 = ra_get_key_at_index(x1->high_low_container, pos1);
            s2 = ra_get_key_at_index(x2->high_low_container, pos2);

        } else if (s1 < s2) {  // s1 < s2
            pos1++;
            if (pos1 == length1) break;
            s1 = ra_get_key_at_index(x1->high_low_container, pos1);

        } else {  // s1 > s2
            void *c2 = ra_get_container_at_index(x2->high_low_container, pos2,
                                                 &container_type_2);
            //void *c2_clone = container_clone(c2, container_type_2);
            c2 = get_copy_of_container(c2, &container_type_2,x2->copy_on_write);
            if(x2->copy_on_write) {
              ra_set_container_at_index(x2->high_low_container, pos2, c2, container_type_2);
            }
            ra_insert_new_key_value_at(x1->high_low_container, pos1, s2,
                                       c2, container_type_2);
            pos1++;
            length1++;
            pos2++;
            if (pos2 == length2) break;
            s2 = ra_get_key_at_index(x2->high_low_container, pos2);
        }
    }
    if (pos1 == length1) {
        ra_append_copy_range(x1->high_low_container, x2->high_low_container,
                             pos2, length2, x2->copy_on_write);
    }
}

void roaring_bitmap_repair_after_lazy(roaring_bitmap_t *ra) {
    for (int i = 0; i < ra->high_low_container->size; ++i) {
        const uint8_t original_typecode = ra->high_low_container->typecodes[i];
        void *container = ra->high_low_container->containers[i];
        uint8_t new_typecode = original_typecode;
        void *newcontainer =
            container_repair_after_lazy(container, &new_typecode);
        ra->high_low_container->containers[i] = newcontainer;
        ra->high_low_container->typecodes[i] = new_typecode;
    }
}
