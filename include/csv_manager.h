#ifndef CSV_MANAGER_H
#define CSV_MANAGER_H
#include "matching_engine.h"
/* Save on clean exit. Loading replaces the engine only on success. */
MatchingStatus csv_save(const MatchingEngine *, const char *directory);
MatchingStatus csv_load(MatchingEngine **, const char *directory, int *loaded);
#endif
