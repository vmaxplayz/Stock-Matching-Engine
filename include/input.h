#ifndef MATCHING_INPUT_H
#define MATCHING_INPUT_H
#include "matching_types.h"
int input_parse_price(const char *, Price *);
int input_parse_quantity(const char *, Quantity *);
int input_parse_id(const char *, uint64_t *);
#endif
