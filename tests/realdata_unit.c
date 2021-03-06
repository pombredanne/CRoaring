/*
 * realdata_unit.c
 */
#define _GNU_SOURCE
#include "../benchmarks/numbersfromtextfiles.h"
#include "config.h"
#include "roaring.h"

void show_structure(roaring_array_t *);  // debug

/**
 * Once you have collected all the integers, build the bitmaps.
 */
static roaring_bitmap_t **create_all_bitmaps(size_t *howmany,
                                             uint32_t **numbers, size_t count, bool copy_on_write) {
    if (numbers == NULL) return NULL;
    printf("Constructing %d  bitmaps.\n", (int)count);
    roaring_bitmap_t **answer = malloc(sizeof(roaring_bitmap_t *) * count);
    for (size_t i = 0; i < count; i++) {
        printf(".");
        fflush(stdout);
        answer[i] = roaring_bitmap_of_ptr(howmany[i], numbers[i]);
        answer[i]->copy_on_write = copy_on_write;
    }
    printf("\n");
    return answer;
}

const char *datadir[] = {
    "census-income",       "census-income_srt",  "census1881",
    "census1881_srt",      "uscensus2000",       "weather_sept_85",
    "weather_sept_85_srt", "wikileaks-noquotes", "wikileaks-noquotes_srt"};

bool serialize_correctly(roaring_bitmap_t *r) {
    uint32_t expectedsize = roaring_bitmap_portable_size_in_bytes(r);
    char *serialized = malloc(expectedsize);
    if (serialized == NULL) {
        printf("failure to allocate memory!\n");
        return false;
    }
    uint32_t serialize_len = roaring_bitmap_portable_serialize(r, serialized);
    if (serialize_len != expectedsize) {
        printf("Bad serialized size!\n");
        free(serialized);
        return false;
    }
    roaring_bitmap_t *r2 = roaring_bitmap_portable_deserialize(serialized);
    free(serialized);
    if (!roaring_bitmap_equals(r, r2)) {
        printf("Won't recover original bitmap!\n");
        roaring_bitmap_free(r2);
        return false;
    }
    if (!roaring_bitmap_equals(r2, r)) {
        printf("Won't recover original bitmap!\n");
        roaring_bitmap_free(r2);
        return false;
    }
    roaring_bitmap_free(r2);
    return true;
}

// arrays expected to both be sorted.
bool array_equals(uint32_t *a1, int32_t size1, uint32_t *a2, int32_t size2) {
    if (size1 != size2) {
        printf("they differ since sizes differ %d %d\n", size1, size2);
        return false;
    }
    for (int i = 0; i < size1; ++i)
        if (a1[i] != a2[i]) {
            printf("same sizes %d %d but they differ at %d \n", size1, size2,
                   i);
            return false;
        }
    return true;
}

bool is_union_correct(roaring_bitmap_t *bitmap1, roaring_bitmap_t *bitmap2) {
    roaring_bitmap_t *temp = roaring_bitmap_or(bitmap1, bitmap2);
    uint32_t card1, card2, card;
    uint32_t *arr1 = roaring_bitmap_to_uint32_array(bitmap1, &card1);
    uint32_t *arr2 = roaring_bitmap_to_uint32_array(bitmap2, &card2);
    uint32_t *arr = roaring_bitmap_to_uint32_array(temp, &card);
    uint32_t *buffer = (uint32_t *)malloc(sizeof(uint32_t) * (card1 + card2));
    size_t cardtrue = union_uint32(arr1, card1, arr2, card2, buffer);
    bool answer = array_equals(arr, card, buffer, cardtrue);
    if (!answer) {
        printf("\n\nbitmap1:\n");
        show_structure(bitmap1->high_low_container);  // debug
        printf("\n\nbitmap2:\n");
        show_structure(bitmap2->high_low_container);  // debug
        printf("\n\nresult:\n");
        show_structure(temp->high_low_container);  // debug
        roaring_bitmap_t *ca = roaring_bitmap_of_ptr(cardtrue, buffer);
        printf("\n\ncorrect result:\n");
        show_structure(ca->high_low_container);  // debug
        free(ca);
    }
    free(buffer);
    free(arr1);
    free(arr2);
    free(arr);
    roaring_bitmap_free(temp);
    return answer;
}

bool is_intersection_correct(roaring_bitmap_t *bitmap1,
                             roaring_bitmap_t *bitmap2) {
    roaring_bitmap_t *temp = roaring_bitmap_and(bitmap1, bitmap2);
    uint32_t card1, card2, card;
    uint32_t *arr1 = roaring_bitmap_to_uint32_array(bitmap1, &card1);
    uint32_t *arr2 = roaring_bitmap_to_uint32_array(bitmap2, &card2);
    uint32_t *arr = roaring_bitmap_to_uint32_array(temp, &card);
    uint32_t *buffer = (uint32_t *)malloc(sizeof(uint32_t) * (card1 + card2));
    size_t cardtrue = intersection_uint32(arr1, card1, arr2, card2, buffer);
    bool answer = array_equals(arr, card, buffer, cardtrue);
    if (!answer) {
        printf("\n\nbitmap1:\n");
        show_structure(bitmap1->high_low_container);  // debug
        printf("\n\nbitmap2:\n");
        show_structure(bitmap2->high_low_container);  // debug
        printf("\n\nresult:\n");
        show_structure(temp->high_low_container);  // debug
        roaring_bitmap_t *ca = roaring_bitmap_of_ptr(cardtrue, buffer);
        printf("\n\ncorrect result:\n");
        show_structure(ca->high_low_container);  // debug
        free(ca);
    }
    free(buffer);
    free(arr1);
    free(arr2);
    free(arr);
    roaring_bitmap_free(temp);
    return answer;
}

roaring_bitmap_t *inplace_union(roaring_bitmap_t *bitmap1,
                                roaring_bitmap_t *bitmap2) {
    roaring_bitmap_t *answer = roaring_bitmap_copy(bitmap1);
    roaring_bitmap_or_inplace(answer, bitmap2);
    return answer;
}

roaring_bitmap_t *inplace_intersection(roaring_bitmap_t *bitmap1,
                                       roaring_bitmap_t *bitmap2) {
    roaring_bitmap_t *answer = roaring_bitmap_copy(bitmap1);
    roaring_bitmap_and_inplace(answer, bitmap2);
    return answer;
}

bool slow_bitmap_equals(roaring_bitmap_t *bitmap1, roaring_bitmap_t *bitmap2) {
    uint32_t card1, card2;
    uint32_t *arr1 = roaring_bitmap_to_uint32_array(bitmap1, &card1);
    uint32_t *arr2 = roaring_bitmap_to_uint32_array(bitmap2, &card2);
    bool answer = array_equals(arr1, card1, arr2, card2);
    free(arr1);
    free(arr2);
    return answer;
}

bool compare_intersections(roaring_bitmap_t **rnorun, roaring_bitmap_t **rruns,
                           size_t count) {
    roaring_bitmap_t *tempandnorun;
    roaring_bitmap_t *tempandruns;
    for (size_t i = 0; i + 1 < count; ++i) {
        tempandnorun = roaring_bitmap_and(rnorun[i], rnorun[i + 1]);
        if (!is_intersection_correct(rnorun[i], rnorun[i + 1])) {
            printf("no run intersection incorrect\n");
            return false;
        }
        tempandruns = roaring_bitmap_and(rruns[i], rruns[i + 1]);
        if (!is_intersection_correct(rruns[i], rruns[i + 1])) {
            printf("runs intersection incorrect\n");
            return false;
        }
        if (!slow_bitmap_equals(tempandnorun, tempandruns)) {
            printf("Intersections don't agree! (slow) \n");
            return false;
        }

        if (!roaring_bitmap_equals(tempandnorun, tempandruns)) {
            printf("Intersections don't agree!\n");
            printf("\n\nbitmap1:\n");
            show_structure(tempandnorun->high_low_container);  // debug
            printf("\n\nbitmap2:\n");
            show_structure(tempandruns->high_low_container);  // debug
            return false;
        }
        roaring_bitmap_free(tempandnorun);
        roaring_bitmap_free(tempandruns);

        tempandnorun = inplace_intersection(rnorun[i], rnorun[i + 1]);
        if (!is_intersection_correct(rnorun[i], rnorun[i + 1])) {
            printf("[inplace] no run intersection incorrect\n");
            return false;
        }
        tempandruns = inplace_intersection(rruns[i], rruns[i + 1]);
        if (!is_intersection_correct(rruns[i], rruns[i + 1])) {
            printf("[inplace] runs intersection incorrect\n");
            return false;
        }
        if (!slow_bitmap_equals(tempandnorun, tempandruns)) {
            printf("[inplace] Intersections don't agree! (slow) \n");
            return false;
        }

        if (!roaring_bitmap_equals(tempandnorun, tempandruns)) {
            printf("[inplace] Intersections don't agree!\n");
            printf("\n\nbitmap1:\n");
            show_structure(tempandnorun->high_low_container);  // debug
            printf("\n\nbitmap2:\n");
            show_structure(tempandruns->high_low_container);  // debug
            return false;
        }
        roaring_bitmap_free(tempandnorun);
        roaring_bitmap_free(tempandruns);
    }
    return true;
}

bool compare_unions(roaring_bitmap_t **rnorun, roaring_bitmap_t **rruns,
                    size_t count) {
    roaring_bitmap_t *tempornorun;
    roaring_bitmap_t *temporruns;
    for (size_t i = 0; i + 1 < count; ++i) {
        tempornorun = roaring_bitmap_or(rnorun[i], rnorun[i + 1]);
        if (!is_union_correct(rnorun[i], rnorun[i + 1])) {
            printf("no-run union incorrect\n");
            return false;
        }
        temporruns = roaring_bitmap_or(rruns[i], rruns[i + 1]);
        if (!is_union_correct(rruns[i], rruns[i + 1])) {
            printf("runs unions incorrect\n");
            return false;
        }
        if (!slow_bitmap_equals(tempornorun, temporruns)) {
            printf("Unions don't agree! (slow) \n");
            return false;
        }

        if (!roaring_bitmap_equals(tempornorun, temporruns)) {
            printf("Unions don't agree!\n");
            printf("\n\nbitmap1:\n");
            show_structure(tempornorun->high_low_container);  // debug
            printf("\n\nbitmap2:\n");
            show_structure(temporruns->high_low_container);  // debug
            return false;
        }
        roaring_bitmap_free(tempornorun);
        roaring_bitmap_free(temporruns);
        tempornorun = inplace_union(rnorun[i], rnorun[i + 1]);
        if (!is_union_correct(rnorun[i], rnorun[i + 1])) {
            printf("[inplace] no-run union incorrect\n");
            return false;
        }
        temporruns = inplace_union(rruns[i], rruns[i + 1]);
        if (!is_union_correct(rruns[i], rruns[i + 1])) {
            printf("[inplace] runs unions incorrect\n");
            return false;
        }

        if (!slow_bitmap_equals(tempornorun, temporruns)) {
            printf("[inplace] Unions don't agree! (slow) \n");
            return false;
        }

        if (!roaring_bitmap_equals(tempornorun, temporruns)) {
            printf("[inplace] Unions don't agree!\n");
            printf("\n\nbitmap1:\n");
            show_structure(tempornorun->high_low_container);  // debug
            printf("\n\nbitmap2:\n");
            show_structure(temporruns->high_low_container);  // debug
            return false;
        }
        roaring_bitmap_free(tempornorun);
        roaring_bitmap_free(temporruns);
    }
    return true;
}

bool compare_wide_unions(roaring_bitmap_t **rnorun, roaring_bitmap_t **rruns,
                         size_t count) {
    roaring_bitmap_t *tempornorun =
        roaring_bitmap_or_many(count, (const roaring_bitmap_t **)rnorun);
    roaring_bitmap_t *temporruns =
        roaring_bitmap_or_many(count, (const roaring_bitmap_t **)rruns);
    if (!slow_bitmap_equals(tempornorun, temporruns)) {
        printf("[compare_wide_unions] Unions don't agree! (fast run-norun) \n");
        return false;
    }
    assert(roaring_bitmap_equals(tempornorun, temporruns));

    roaring_bitmap_t *tempornorunheap =
        roaring_bitmap_or_many_heap(count, (const roaring_bitmap_t **)rnorun);
    roaring_bitmap_t *temporrunsheap =
        roaring_bitmap_or_many_heap(count, (const roaring_bitmap_t **)rruns);
    //assert(slow_bitmap_equals(tempornorun, tempornorunheap));
    //assert(slow_bitmap_equals(temporruns,temporrunsheap));

    assert(roaring_bitmap_equals(tempornorun, tempornorunheap));
    assert(roaring_bitmap_equals(temporruns,temporrunsheap));
    roaring_bitmap_free(tempornorunheap);
    roaring_bitmap_free(temporrunsheap);

    roaring_bitmap_t *longtempornorun;
    roaring_bitmap_t *longtemporruns;
    if (count == 1) {
        longtempornorun = rnorun[0];
        longtemporruns = rruns[0];
    } else {
        assert(roaring_bitmap_equals(rnorun[0], rruns[0]));
        assert(roaring_bitmap_equals(rnorun[1], rruns[1]));
        longtempornorun = roaring_bitmap_or(rnorun[0], rnorun[1]);
        longtemporruns = roaring_bitmap_or(rruns[0], rruns[1]);
        assert(roaring_bitmap_equals(longtempornorun, longtemporruns));
        for (int i = 2; i < (int)count; ++i) {
            assert(roaring_bitmap_equals(rnorun[i], rruns[i]));
            assert(roaring_bitmap_equals(longtempornorun, longtemporruns));

            roaring_bitmap_t *t1 =
                roaring_bitmap_or(rnorun[i], longtempornorun);
            roaring_bitmap_t *t2 = roaring_bitmap_or(rruns[i], longtemporruns);
            assert(roaring_bitmap_equals(t1, t2));
            roaring_bitmap_free(longtempornorun);
            longtempornorun = t1;
            roaring_bitmap_free(longtemporruns);
            longtemporruns = t2;
            assert(roaring_bitmap_equals(longtempornorun, longtemporruns));
        }
    }
    if (!slow_bitmap_equals(longtempornorun, tempornorun)) {
        printf("[compare_wide_unions] Unions don't agree! (regular) \n");
        return false;
    }
    if (!slow_bitmap_equals(temporruns, longtemporruns)) {
        printf("[compare_wide_unions] Unions don't agree! (runs) \n");
        return false;
    }
    roaring_bitmap_free(tempornorun);
    roaring_bitmap_free(temporruns);

    roaring_bitmap_free(longtempornorun);
    roaring_bitmap_free(longtemporruns);

    return true;
}

bool is_bitmap_equal_to_array(roaring_bitmap_t *bitmap, uint32_t *vals,
                              size_t numbers) {
    uint32_t card;
    uint32_t *arr = roaring_bitmap_to_uint32_array(bitmap, &card);
    bool answer = array_equals(arr, card, vals, numbers);
    free(arr);
    return answer;
}

bool loadAndCheckAll(const char *dirname, bool copy_on_write) {
    printf("[%s] %s datadir=%s %s\n", __FILE__, __func__, dirname, copy_on_write ? "copy-on-write":"hard-copies");

    char *extension = ".txt";
    size_t count;

    size_t *howmany = NULL;
    uint32_t **numbers =
        read_all_integer_files(dirname, extension, &howmany, &count);
    if (numbers == NULL) {
        printf(
            "I could not find or load any data file with extension %s in "
            "directory %s.\n",
            extension, dirname);
        return false;
    }

    roaring_bitmap_t **bitmaps = create_all_bitmaps(howmany, numbers, count, copy_on_write);
    for (size_t i = 0; i < count; i++) {
        if (!is_bitmap_equal_to_array(bitmaps[i], numbers[i], howmany[i])) {
            printf("arrays don't agree with set values\n");
            return false;
        }
    }

    roaring_bitmap_t **bitmapswrun = malloc(sizeof(roaring_bitmap_t *) * count);
    for (int i = 0; i < (int)count; i++) {
        bitmapswrun[i] = roaring_bitmap_copy(bitmaps[i]);
        roaring_bitmap_run_optimize(bitmapswrun[i]);
        if(roaring_bitmap_get_cardinality(bitmaps[i]) !=
        		roaring_bitmap_get_cardinality(bitmapswrun[i])) {
            printf("cardinality change due to roaring_bitmap_run_optimize\n");
            return false;
        }
    }
    for (size_t i = 0; i < count; i++) {
        if (!is_bitmap_equal_to_array(bitmapswrun[i], numbers[i], howmany[i])) {
            printf("arrays don't agree with set values\n");
            return false;
        }
    }
    for (int i = 0; i < (int)count; i++) {
        if (!serialize_correctly(bitmaps[i])) {
            return false;  //  memory leaks
        }
        if (!serialize_correctly(bitmapswrun[i])) {
            return false;  //  memory leaks
        }
    }
    if (!compare_intersections(bitmaps, bitmapswrun, count)) {
        return false;  //  memory leaks
    }
    if (!compare_unions(bitmaps, bitmapswrun, count)) {
        return false;  //  memory leaks
    }
    if (!compare_wide_unions(bitmaps, bitmapswrun, count)) {
        return false;  //  memory leaks
    }

    for (int i = 0; i < (int)count; ++i) {
        free(numbers[i]);
        numbers[i] = NULL;  // paranoid
        roaring_bitmap_free(bitmaps[i]);
        bitmaps[i] = NULL;  // paranoid
        roaring_bitmap_free(bitmapswrun[i]);
        bitmapswrun[i] = NULL;  // paranoid
    }
    free(bitmapswrun);
    free(bitmaps);
    free(howmany);
    free(numbers);

    return true;
}

int main() {
    char dirbuffer[1024];
    size_t bddl = strlen(BENCHMARK_DATA_DIR);
    strcpy(dirbuffer, BENCHMARK_DATA_DIR);
    for (size_t i = 0; i < sizeof(datadir) / sizeof(const char *); i++) {
        strcpy(dirbuffer + bddl, datadir[i]);
        if (!loadAndCheckAll(dirbuffer,false)) {
            printf("failure\n");
            return -1;
        }
        if (!loadAndCheckAll(dirbuffer,true)) {
            printf("failure\n");
            return -1;
        }

    }

    return EXIT_SUCCESS;
}
