#include "matching_engine.h"
#include "csv_manager.h"
#include "cli.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    const char *directory = "data";
    MatchingEngine *engine;
    MatchingStatus status;
    int loaded = 0;
    int result;

    if (argc == 2 && !strcmp(argv[1], "--help")) {
        puts("Usage: matching-engine [--data DIRECTORY]");
        puts("CSV snapshots are saved on clean exit only.");
        return 0;
    }

    if (argc == 3 && !strcmp(argv[1], "--data")) {
        directory = argv[2];
    } else if (argc != 1) {
        fputs("Use --help for usage.\n", stderr);
        return 1;
    }

    engine = matching_engine_create();
    if (!engine) {
        fputs("Could not initialize engine.\n", stderr);
        return 1;
    }

    status = csv_load(&engine, directory, &loaded);
    if (status != ME_OK) {
        fprintf(stderr, "Cannot load '%s': %s. Existing files were not changed.\n", directory, matching_status_string(status));
        matching_engine_destroy(engine);
        return 1;
    }

    puts(loaded ? "Saved state loaded." : "Starting a new order book.");
    result = cli_run(engine);
    status = csv_save(engine, directory);
    if (status != ME_OK) {
        fprintf(stderr, "Saving failed: %s. Operations completed in memory, but this session was not saved. Any previous published snapshot was kept.\n", matching_status_string(status));
        result = 1;
    } else {
        puts("Orders and trades saved. Session ended.");
    }

    matching_engine_destroy(engine);
    return result;
}
