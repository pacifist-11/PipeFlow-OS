CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lreadline -pthread

SRC_ENGINE = src/main.c src/record.c src/stage.c src/parser.c src/executor.c src/signals.c src/repl.c
TARGET_ENGINE = pipeflow

STAGE_BINS = bin/pf_source bin/pf_transform bin/pf_filter bin/pf_head bin/pf_sink \
             bin/pf_file_source bin/pf_file_sink bin/pf_parallel_worker bin/pf_tee bin/pf_merge

all: directories $(TARGET_ENGINE) $(STAGE_BINS)

directories:
	@mkdir -p bin logs

$(TARGET_ENGINE): $(SRC_ENGINE)
	$(CC) $(CFLAGS) $(SRC_ENGINE) $(LDFLAGS) -o $(TARGET_ENGINE)

bin/pf_source: stages/pf_source.c src/record.c
	$(CC) $(CFLAGS) stages/pf_source.c src/record.c -o bin/pf_source

bin/pf_transform: stages/pf_transform.c src/record.c
	$(CC) $(CFLAGS) stages/pf_transform.c src/record.c -o bin/pf_transform

bin/pf_filter: stages/pf_filter.c src/record.c
	$(CC) $(CFLAGS) stages/pf_filter.c src/record.c -o bin/pf_filter

bin/pf_head: stages/pf_head.c src/record.c
	$(CC) $(CFLAGS) stages/pf_head.c src/record.c -o bin/pf_head

bin/pf_sink: stages/pf_sink.c src/record.c
	$(CC) $(CFLAGS) stages/pf_sink.c src/record.c -o bin/pf_sink

bin/pf_file_source: stages/pf_file_source.c src/record.c
	$(CC) $(CFLAGS) stages/pf_file_source.c src/record.c -o bin/pf_file_source

bin/pf_file_sink: stages/pf_file_sink.c src/record.c
	$(CC) $(CFLAGS) stages/pf_file_sink.c src/record.c -o bin/pf_file_sink

bin/pf_parallel_worker: stages/pf_parallel_worker.c src/record.c
	$(CC) $(CFLAGS) stages/pf_parallel_worker.c src/record.c -pthread -o bin/pf_parallel_worker

bin/pf_tee: stages/pf_tee.c src/record.c
	$(CC) $(CFLAGS) stages/pf_tee.c src/record.c -o bin/pf_tee

bin/pf_merge: stages/pf_merge.c src/record.c
	$(CC) $(CFLAGS) stages/pf_merge.c src/record.c -o bin/pf_merge

test: all
	@chmod +x tests/*.sh
	@./tests/run_all.sh

clean:
	rm -rf $(TARGET_ENGINE) bin/ logs/

.PHONY: all clean test directories
