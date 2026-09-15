CC = clang-18
CFLAGS = -O2 -std=c11 -fopenmp -Wall -Wextra
TARGET = region_timer_ut

.PHONY: all test clean

all: $(TARGET)

$(TARGET): region_timer_ut.c region_timer.c region_timer.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG region_timer_ut.c region_timer.c \
		$(LDFLAGS) -Wl,--wrap=omp_get_wtime -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)
