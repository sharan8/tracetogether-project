DEFINES+=PROJECT_CONF_H=\"project-conf.h\"
CONTIKI_PROJECT = nbr_discovery
APPS+=powertrace
all: $(CONTIKI_PROJECT)

CONTIKI_WITH_RIME = 1

CONTIKI = ../..
include $(CONTIKI)/Makefile.include

TARGET_LIBFILES += hashmap/libhashmap.a
LDFLAGS += -static -L./hashmap -l:libhashmap.a
